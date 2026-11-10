//! One expedition instance: a task that owns the [`World`], applies pilots'
//! inputs and sends every pilot a snapshot each tick.
//!
//! Sessions talk to it through an [`Expedition`] handle. Stage 1 runs a single
//! instance for the whole server; contracts and crews (plan M6) come later.

use std::collections::HashMap;
use std::time::Duration;

use frontier_protocol::input::InputDatagram;
use frontier_protocol::snapshot::{
    EntityKind, EntityState, Snapshot, quantize_heading, quantize_position, quantize_velocity,
};
use frontier_sim::TICKS_PER_SECOND;
use frontier_sim::collision::{Obstacle, asteroid_field};
use frontier_sim::profile::ShipKind;
use frontier_sim::ship::ShipInput;
use frontier_sim::skirmish::Skirmish;
use frontier_sim::world::{ShipId, World};
use tokio::sync::{mpsc, oneshot};
use tokio::time::{MissedTickBehavior, interval};
use tracing::{debug, warn};
use web_transport_quinn::Session;

use crate::input_queue::InputQueue;

/// Pilots per expedition (design section 2).
pub const MAX_PILOTS: usize = 4;

/// Seed of the asteroid field, until sites are generated from their own seeds (plan M3).
const ASTEROID_SEED: u32 = 2266;

/// Sends a snapshot datagram to one pilot. A WebTransport session in the
/// server; a channel in tests.
pub trait SnapshotSink: Send + 'static {
    /// Returns false once the pilot can no longer be reached.
    fn send(&self, datagram: Vec<u8>) -> bool;
}

impl SnapshotSink for Session {
    fn send(&self, datagram: Vec<u8>) -> bool {
        // A full send buffer drops this snapshot; the next one replaces it.
        match self.send_datagram(datagram.into()) {
            Ok(()) => true,
            Err(error) => {
                debug!(%error, "snapshot not sent");
                !matches!(error, web_transport_quinn::SessionError::ConnectionError(_))
            }
        }
    }
}

enum Command {
    Join {
        sink: Box<dyn SnapshotSink>,
        reply: oneshot::Sender<Option<ShipId>>,
    },
    Input {
        ship: ShipId,
        datagram: InputDatagram,
    },
    Leave {
        ship: ShipId,
    },
    Lock {
        ship: ShipId,
        target: Option<ShipId>,
    },
}

/// A handle to a running expedition. Cheap to clone.
#[derive(Clone)]
pub struct Expedition {
    commands: mpsc::UnboundedSender<Command>,
    obstacles: std::sync::Arc<[Obstacle]>,
}

impl Expedition {
    /// Starts an expedition on the current Tokio runtime.
    #[must_use]
    pub fn start() -> Self {
        let obstacles: std::sync::Arc<[Obstacle]> = asteroid_field(ASTEROID_SEED).into();
        let (commands, receiver) = mpsc::unbounded_channel();
        tokio::spawn(run(receiver, World::with_obstacles(obstacles.to_vec())));
        Self {
            commands,
            obstacles,
        }
    }

    /// The fixed obstacles, which every pilot is sent when they join.
    #[must_use]
    pub fn obstacles(&self) -> &[Obstacle] {
        &self.obstacles
    }

    /// Adds a pilot. Returns their ship, or `None` if the expedition is full
    /// or has stopped.
    pub async fn join(&self, sink: impl SnapshotSink) -> Option<ShipId> {
        let (reply, answer) = oneshot::channel();
        let sink = Box::new(sink);
        self.commands.send(Command::Join { sink, reply }).ok()?;
        answer.await.ok().flatten()
    }

    /// Hands over an input datagram from the pilot flying `ship`.
    pub fn input(&self, ship: ShipId, datagram: InputDatagram) {
        let _ = self.commands.send(Command::Input { ship, datagram });
    }

    /// Locks `target` for the pilot flying `ship`, or releases the lock. The
    /// world accepts only an existing hostile.
    pub fn lock(&self, ship: ShipId, target: Option<ShipId>) {
        let _ = self.commands.send(Command::Lock { ship, target });
    }

    /// Removes a pilot's ship.
    pub fn leave(&self, ship: ShipId) {
        let _ = self.commands.send(Command::Leave { ship });
    }
}

struct Pilot {
    sink: Box<dyn SnapshotSink>,
    inputs: InputQueue,
    /// The input applied on the latest tick, if any has arrived.
    last_input: Option<u16>,
}

async fn run(mut commands: mpsc::UnboundedReceiver<Command>, mut world: World) {
    // NPCs to fight until sites exist (plan M3).
    let mut skirmish = Skirmish::populate(&mut world);
    let mut pilots: HashMap<ShipId, Pilot> = HashMap::new();
    let mut ticker = interval(Duration::from_secs(1) / TICKS_PER_SECOND);
    // After a stall, carry on at the normal rate rather than racing to catch up.
    ticker.set_missed_tick_behavior(MissedTickBehavior::Skip);

    loop {
        tokio::select! {
            command = commands.recv() => match command {
                Some(command) => apply(command, &mut world, &mut pilots),
                // Every handle is gone: the server is shutting down.
                None => return,
            },
            _ = ticker.tick() => {
                // One input per pilot per tick, in order, as the client predicts.
                for (ship, pilot) in &mut pilots {
                    if let Some((sequence, controls)) = pilot.inputs.take_for_tick() {
                        pilot.last_input = Some(sequence);
                        world.set_input(
                            *ship,
                            ShipInput {
                                thrust: controls.thrust_unit(),
                                turn: controls.turn_unit(),
                            },
                        );
                    }
                }
                skirmish.step(&mut world);
                world.step();
                broadcast(&world, &mut pilots);
            }
        }
    }
}

fn apply(command: Command, world: &mut World, pilots: &mut HashMap<ShipId, Pilot>) {
    match command {
        Command::Join { sink, reply } => {
            let ship = (pilots.len() < MAX_PILOTS).then(|| world.spawn_ship());
            if let Some(ship) = ship {
                pilots.insert(
                    ship,
                    Pilot {
                        sink,
                        inputs: InputQueue::default(),
                        last_input: None,
                    },
                );
            }
            if reply.send(ship).is_err() {
                // The session went away while joining.
                if let Some(ship) = ship {
                    leave(ship, world, pilots);
                }
            }
        }
        Command::Input { ship, datagram } => {
            if let Some(pilot) = pilots.get_mut(&ship) {
                pilot.inputs.receive(&datagram);
            }
        }
        Command::Leave { ship } => leave(ship, world, pilots),
        Command::Lock { ship, target } => {
            if pilots.contains_key(&ship) {
                world.set_lock(ship, target);
            }
        }
    }
}

fn leave(ship: ShipId, world: &mut World, pilots: &mut HashMap<ShipId, Pilot>) {
    pilots.remove(&ship);
    world.remove_ship(ship);
}

/// Sends every pilot the same entities, each with their own input ack.
fn broadcast(world: &World, pilots: &mut HashMap<ShipId, Pilot>) {
    let entities: Vec<EntityState> = world
        .ships()
        .iter()
        .map(|ship| {
            let (vx, vz) = ship.state.velocity();
            let max_hull = ship.kind.profile().max_hull;
            EntityState {
                id: ship.id.0,
                kind: match ship.kind {
                    ShipKind::Pilot => EntityKind::PlayerShip,
                    ShipKind::Drone => EntityKind::Drone,
                    ShipKind::Raider => EntityKind::Raider,
                    ShipKind::Warden => EntityKind::Warden,
                },
                x: quantize_position(ship.state.x),
                z: quantize_position(ship.state.z),
                heading: quantize_heading(ship.state.heading),
                vx: quantize_velocity(vx),
                vz: quantize_velocity(vz),
                // The clamp keeps the product within u8, so `as` cannot saturate.
                hull: ((ship.hull / max_hull).clamp(0.0, 1.0) * 255.0).round() as u8,
            }
        })
        .collect();
    // Snapshots carry the tick modulo 2^32: about 6.8 years at 20 per second.
    let tick = (world.tick().get() % (1 << 32)) as u32;

    pilots.retain(|ship, pilot| {
        let snapshot = Snapshot {
            tick,
            input_ack: pilot.last_input.unwrap_or(0),
            entities: entities.clone(),
        };
        match snapshot.encode() {
            Ok(datagram) => pilot.sink.send(datagram),
            Err(error) => {
                warn!(%error, ship = ship.0, "snapshot not encoded");
                true
            }
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::mpsc as std_mpsc;

    struct Channel(std_mpsc::Sender<Vec<u8>>);

    impl SnapshotSink for Channel {
        fn send(&self, datagram: Vec<u8>) -> bool {
            self.0.send(datagram).is_ok()
        }
    }

    fn channel() -> (Channel, std_mpsc::Receiver<Vec<u8>>) {
        let (send, receive) = std_mpsc::channel();
        (Channel(send), receive)
    }

    fn latest(receiver: &std_mpsc::Receiver<Vec<u8>>) -> Option<Snapshot> {
        receiver
            .try_iter()
            .last()
            .map(|bytes| Snapshot::decode(&bytes).unwrap())
    }

    #[tokio::test(start_paused = true)]
    async fn sends_twenty_snapshots_a_second_to_each_pilot() {
        let expedition = Expedition::start();
        let (sink, snapshots) = channel();
        expedition.join(sink).await.unwrap();
        tokio::time::sleep(Duration::from_millis(1_025)).await;
        let count = snapshots.try_iter().count();
        assert!((20..=22).contains(&count), "{count} snapshots");
    }

    #[tokio::test(start_paused = true)]
    async fn turns_away_a_fifth_pilot() {
        let expedition = Expedition::start();
        for _ in 0..MAX_PILOTS {
            assert!(expedition.join(channel().0).await.is_some());
        }
        assert!(expedition.join(channel().0).await.is_none());
    }

    #[tokio::test(start_paused = true)]
    async fn applies_inputs_and_acknowledges_the_last_applied() {
        use frontier_protocol::input::Controls;
        let expedition = Expedition::start();
        let (sink, snapshots) = channel();
        let ship = expedition.join(sink).await.unwrap();
        let thrust = Controls {
            thrust: 127,
            turn: 0,
        };
        expedition.input(
            ship,
            InputDatagram {
                sequence: 5,
                controls: vec![thrust; 4],
            },
        );
        tokio::time::sleep(Duration::from_millis(500)).await;

        let snapshot = latest(&snapshots).unwrap();
        assert_eq!(snapshot.input_ack, 5);
        let me = snapshot.entities.iter().find(|e| e.id == ship.0).unwrap();
        assert!(me.vx != 0 || me.vz != 0, "the ship should be moving");
    }

    #[tokio::test(start_paused = true)]
    async fn a_departed_pilot_disappears_from_snapshots() {
        let expedition = Expedition::start();
        let (sink, snapshots) = channel();
        let stays = expedition.join(sink).await.unwrap();
        let goes = expedition.join(channel().0).await.unwrap();
        expedition.leave(goes);
        tokio::time::sleep(Duration::from_millis(200)).await;
        let ids: Vec<u16> = latest(&snapshots)
            .unwrap()
            .entities
            .iter()
            .filter(|e| e.kind == EntityKind::PlayerShip)
            .map(|e| e.id)
            .collect();
        assert_eq!(ids, vec![stays.0]);
    }

    #[tokio::test(start_paused = true)]
    async fn snapshots_carry_the_skirmish_npcs_at_full_hull() {
        let expedition = Expedition::start();
        let (sink, snapshots) = channel();
        expedition.join(sink).await.unwrap();
        tokio::time::sleep(Duration::from_millis(100)).await;
        let snapshot = latest(&snapshots).unwrap();
        let npcs: Vec<&EntityState> = snapshot
            .entities
            .iter()
            .filter(|e| e.kind != EntityKind::PlayerShip)
            .collect();
        assert_eq!(npcs.len(), Skirmish::size());
        for kind in [EntityKind::Drone, EntityKind::Raider, EntityKind::Warden] {
            assert!(npcs.iter().any(|e| e.kind == kind), "{kind:?} missing");
        }
        assert!(snapshot.entities.iter().all(|e| e.hull == u8::MAX));
    }

    #[tokio::test(start_paused = true)]
    async fn stops_sending_to_a_pilot_who_cannot_be_reached() {
        let expedition = Expedition::start();
        let (sink, snapshots) = channel();
        expedition.join(sink).await.unwrap();
        drop(snapshots);
        // The next join only succeeds if the unreachable pilot was dropped.
        tokio::time::sleep(Duration::from_millis(100)).await;
        for _ in 0..MAX_PILOTS {
            assert!(expedition.join(channel().0).await.is_some());
        }
    }
}
