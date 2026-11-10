//! One expedition instance: a task that owns a [`Run`], applies its pilots'
//! inputs and actions, and sends every pilot a snapshot each tick until the run
//! ends.
//!
//! Each launch starts its own instance (design section 10: an instance ends
//! when every pilot has docked or been destroyed). Stage 1's first build
//! launches solo; crews joining one instance arrive in plan M6.

use std::collections::HashMap;
use std::time::Duration;

use frontier_protocol::input::InputDatagram;
use frontier_protocol::snapshot::{
    Drive as WireDrive, EntityKind, EntityState, ExtractorMode, MAX_DEPOSITS, MAX_EVENTS,
    OwnStatus, RunPhase, Snapshot, SnapshotEvent, quantize_heading, quantize_position,
    quantize_velocity,
};
use frontier_protocol::{
    AccountInfo, DepositInfo, ExtractRefusal, ObstacleInfo, RunSummary, ServerMessage, WaveInfo,
};
use frontier_sim::TICKS_PER_SECOND;
use frontier_sim::combat::Event;
use frontier_sim::extraction::{EXTRACT_RANGE, ExtractorState, Refusal};
use frontier_sim::outcome;
use frontier_sim::profile::ShipKind;
use frontier_sim::run::{
    Checkpoint, Drive, INSTANCE_LIMIT_TICKS, LIMIT_WARNING_TICKS, Options, Phase, Run, RunEvent,
    Summary, seconds,
};
use frontier_sim::ship::ShipInput;
use frontier_sim::site::{Capability, Site, SiteTemplate, ThreatTier, crew_scaling};
use frontier_sim::world::{ShipId, World};
use tokio::sync::{mpsc, oneshot};
use tokio::time::{MissedTickBehavior, interval};
use tracing::{debug, info, warn};
use web_transport_quinn::Session;

use crate::home;
use crate::input_queue::InputQueue;
use crate::ledger::{Item, Npc, Party, Reason, Transfer};
use crate::runs::{Flying, Runs};
use crate::store::{AccountId, SavedRun, Store};
use crate::wire;

/// Pilots per expedition (design section 2).
pub const MAX_PILOTS: usize = 4;

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

/// What the expedition tells one pilot's session, besides snapshots.
#[derive(Debug, Clone, PartialEq)]
pub enum PilotEvent {
    /// A message to pass on as it is. Boxed: an account is far larger than
    /// the other events.
    Message(Box<ServerMessage>),
    /// The pilot's run is over; they are home. The session offers a new contract.
    RunOver(RunSummary),
}

/// What a launch needs: the contract the pilot accepted, and where to bank
/// what they bring home.
#[derive(Clone)]
pub struct LaunchOrder {
    pub template: SiteTemplate,
    pub seed: u32,
    pub tier: ThreatTier,
    pub options: Options,
    /// Names the run in the log and the ledger.
    pub run: String,
    /// The account the pilot flies for.
    pub pilot: AccountId,
    /// What the pilot's powered layout grants: the pockets they may work.
    pub access: Vec<Capability>,
    /// The rigs and modules the pilot flies, by fitting identifier.
    pub fitted: Vec<String>,
    pub store: Store,
    pub runs: Runs,
}

/// What an instance needs besides its run to report outcomes.
struct Books {
    run: String,
    store: Store,
    runs: Runs,
    accounts: HashMap<ShipId, AccountId>,
}

impl Books {
    fn crew(&self) -> Vec<(AccountId, u16)> {
        let mut crew: Vec<(AccountId, u16)> = self
            .accounts
            .iter()
            .map(|(ship, account)| (*account, ship.0))
            .collect();
        crew.sort_unstable();
        crew
    }
}

enum Command {
    Input {
        ship: ShipId,
        datagram: InputDatagram,
    },
    Lock {
        ship: ShipId,
        target: Option<ShipId>,
    },
    Extract {
        ship: ShipId,
        on: bool,
    },
    Vent {
        ship: ShipId,
    },
    Scan {
        ship: ShipId,
    },
    Leave {
        ship: ShipId,
    },
    Disconnect {
        ship: ShipId,
    },
    Reconnect {
        ship: ShipId,
        sink: Box<dyn SnapshotSink>,
        events: mpsc::UnboundedSender<PilotEvent>,
        reply: oneshot::Sender<Option<ServerMessage>>,
    },
}

/// A handle to a running expedition. Cheap to clone; the instance stops once
/// its run is over.
#[derive(Clone)]
pub struct Expedition {
    commands: mpsc::UnboundedSender<Command>,
}

/// A launched pilot's ship and the run's fixed facts.
pub struct Launched {
    pub expedition: Expedition,
    pub ship: ShipId,
    pub joined: ServerMessage,
}

impl Expedition {
    /// Launches a solo run of `order` on the current Tokio runtime. The pilot
    /// is sent snapshots through `sink` and everything else through `events`.
    #[must_use]
    pub fn launch(
        order: LaunchOrder,
        sink: impl SnapshotSink,
        events: mpsc::UnboundedSender<PilotEvent>,
    ) -> Launched {
        let site = Site::generate(order.template, order.seed, order.tier, 1, &order.access);
        let (mut run, ships) = Run::launch(site, 1, order.options);
        let ship = ships[0];
        run.fit(ship, &order.fitted);
        let joined = joined(&run, ship, order.tier);
        let (commands, receiver) = mpsc::unbounded_channel();
        let mut pilots = HashMap::new();
        pilots.insert(
            ship,
            Pilot {
                sink: Box::new(sink),
                events,
                inputs: InputQueue::default(),
                last_input: None,
            },
        );
        info!(
            target: "frontier::run",
            run = order.run,
            seed = order.seed,
            tier = order.tier.number(),
            site = order.template.name,
            "launched"
        );
        let expedition = Self { commands };
        order.runs.insert(
            order.pilot,
            Flying {
                run: order.run.clone(),
                expedition: expedition.clone(),
                ship,
            },
        );
        let books = Books {
            run: order.run,
            store: order.store,
            runs: order.runs,
            accounts: HashMap::from([(ship, order.pilot)]),
        };
        tokio::spawn(fly(receiver, run, pilots, books));
        Launched {
            expedition,
            ship,
            joined,
        }
    }

    /// Restores a run from its checkpoint after a restart, with no pilot
    /// connected: each may rejoin within the grace window. Returns `None` if
    /// the checkpoint cannot be read.
    pub fn resume(saved: SavedRun, store: Store, runs: Runs) -> Option<Self> {
        let checkpoint: Checkpoint = match serde_json::from_str(&saved.document) {
            Ok(checkpoint) => checkpoint,
            Err(error) => {
                warn!(run = saved.run, %error, "checkpoint not readable");
                return None;
            }
        };
        let run = Run::restore(&checkpoint)?;
        let (commands, receiver) = mpsc::unbounded_channel();
        let expedition = Self { commands };
        let accounts: HashMap<ShipId, AccountId> = saved
            .crew
            .iter()
            .map(|&(account, ship)| (ShipId(ship), account))
            .collect();
        for (&ship, &account) in &accounts {
            if run.is_disconnected(ship) {
                runs.insert(
                    account,
                    Flying {
                        run: saved.run.clone(),
                        expedition: expedition.clone(),
                        ship,
                    },
                );
            }
        }
        info!(target: "frontier::run", run = saved.run, tick = checkpoint.elapsed, "restored");
        let books = Books {
            run: saved.run,
            store,
            runs,
            accounts,
        };
        tokio::spawn(fly(receiver, run, HashMap::new(), books));
        Some(expedition)
    }

    /// The pilot's connection dropped: their ship stays exposed for a while,
    /// then is held inert, and their run waits for them (design section 6).
    pub fn disconnect(&self, ship: ShipId) {
        let _ = self.commands.send(Command::Disconnect { ship });
    }

    /// Puts a returning pilot back in their run. Returns the `Joined` message
    /// for them, or `None` if the run has ended or no longer holds them.
    pub async fn reconnect(
        &self,
        ship: ShipId,
        sink: impl SnapshotSink,
        events: mpsc::UnboundedSender<PilotEvent>,
    ) -> Option<ServerMessage> {
        let (reply, answer) = oneshot::channel();
        self.commands
            .send(Command::Reconnect {
                ship,
                sink: Box::new(sink),
                events,
                reply,
            })
            .ok()?;
        answer.await.ok().flatten()
    }

    /// Hands over an input datagram from the pilot flying `ship`.
    pub fn input(&self, ship: ShipId, datagram: InputDatagram) {
        let _ = self.commands.send(Command::Input { ship, datagram });
    }

    /// Locks `target` for the pilot flying `ship`, or releases the lock.
    pub fn lock(&self, ship: ShipId, target: Option<ShipId>) {
        let _ = self.commands.send(Command::Lock { ship, target });
    }

    /// Starts or stops the pilot's extractor.
    pub fn extract(&self, ship: ShipId, on: bool) {
        let _ = self.commands.send(Command::Extract { ship, on });
    }

    pub fn vent(&self, ship: ShipId) {
        let _ = self.commands.send(Command::Vent { ship });
    }

    pub fn scan(&self, ship: ShipId) {
        let _ = self.commands.send(Command::Scan { ship });
    }

    /// Takes the pilot out of the run, abandoning it.
    pub fn leave(&self, ship: ShipId) {
        let _ = self.commands.send(Command::Leave { ship });
    }

    /// Waits until the instance has stopped. For tests.
    pub async fn stopped(&self) {
        self.commands.closed().await;
    }
}

struct Pilot {
    sink: Box<dyn SnapshotSink>,
    events: mpsc::UnboundedSender<PilotEvent>,
    inputs: InputQueue,
    /// The input applied on the latest tick, if any has arrived.
    last_input: Option<u16>,
}

async fn fly(
    mut commands: mpsc::UnboundedReceiver<Command>,
    mut run: Run,
    mut pilots: HashMap<ShipId, Pilot>,
    books: Books,
) {
    let mut ticker = interval(Duration::from_secs(1) / TICKS_PER_SECOND);
    // After a stall, carry on at the normal rate rather than racing to catch up.
    ticker.set_missed_tick_behavior(MissedTickBehavior::Skip);
    loop {
        tokio::select! {
            command = commands.recv() => match command {
                Some(command) => apply(command, &mut run, &mut pilots),
                // Every handle is gone: the server is shutting down.
                None => return,
            },
            _ = ticker.tick() => {
                // One input per pilot per tick, in order, as the client predicts.
                for (ship, pilot) in &mut pilots {
                    if let Some((sequence, controls)) = pilot.inputs.take_for_tick() {
                        pilot.last_input = Some(sequence);
                        let input = ShipInput {
                            thrust: controls.thrust_unit(),
                            turn: controls.turn_unit(),
                            cruise: controls.cruise,
                        };
                        run.set_controls(*ship, input, controls.fire);
                    }
                }
                run.step();
                broadcast(&run, &mut pilots);
            }
        }
        report(&mut run, &mut pilots, &books).await;
        if let Some(checkpoint) = run.take_checkpoint() {
            save_checkpoint(&books, &checkpoint).await;
        }
        if run.ended().is_some() {
            let run_id = books.run.clone();
            if let Err(error) = books
                .store
                .call(move |s| s.delete_checkpoint(&run_id))
                .await
            {
                warn!(%error, run = books.run, "checkpoint not deleted");
            }
            for account in books.accounts.values() {
                books.runs.remove(*account, &books.run);
            }
            return;
        }
    }
}

fn apply(command: Command, run: &mut Run, pilots: &mut HashMap<ShipId, Pilot>) {
    match command {
        Command::Input { ship, datagram } => {
            if let Some(pilot) = pilots.get_mut(&ship) {
                pilot.inputs.receive(&datagram);
            }
        }
        Command::Lock { ship, target } => {
            run.set_lock(ship, target);
        }
        Command::Extract { ship, on } => {
            if let Err(refusal) = run.extract(ship, on) {
                let reason = match refusal {
                    Refusal::NotAtDeposit => ExtractRefusal::NotAtDeposit,
                    Refusal::Depleted => ExtractRefusal::Depleted,
                    Refusal::CargoFull => ExtractRefusal::CargoFull,
                    Refusal::Busy => ExtractRefusal::Busy,
                };
                tell(pilots, ship, ServerMessage::ExtractRefused { reason });
            }
        }
        Command::Vent { ship } => {
            run.vent(ship);
        }
        Command::Scan { ship } => {
            run.scan(ship);
        }
        Command::Leave { ship } => {
            pilots.remove(&ship);
            run.leave(ship);
        }
        Command::Disconnect { ship } => {
            pilots.remove(&ship);
            run.disconnect(ship);
        }
        Command::Reconnect {
            ship,
            sink,
            events,
            reply,
        } => {
            let rejoined = run.reconnect(ship).then(|| {
                pilots.insert(
                    ship,
                    Pilot {
                        sink,
                        events,
                        inputs: InputQueue::default(),
                        last_input: None,
                    },
                );
                joined(run, ship, run.site().tier)
            });
            let _ = reply.send(rejoined);
        }
    }
}

fn tell(pilots: &HashMap<ShipId, Pilot>, ship: ShipId, message: ServerMessage) {
    if let Some(pilot) = pilots.get(&ship) {
        let _ = pilot.events.send(PilotEvent::Message(Box::new(message)));
    }
}

/// Writes the run's new events to the log and tells pilots what concerns them.
async fn save_checkpoint(books: &Books, checkpoint: &Checkpoint) {
    let document = match serde_json::to_string(checkpoint) {
        Ok(document) => document,
        Err(error) => {
            warn!(%error, run = books.run, "checkpoint not encoded");
            return;
        }
    };
    let (run, crew) = (books.run.clone(), books.crew());
    if let Err(error) = books
        .store
        .call(move |s| s.save_checkpoint(&run, &crew, &document))
        .await
    {
        warn!(%error, run = books.run, "checkpoint not saved");
    }
}

/// Writes the run's new events to the log, banks what docked pilots brought
/// home, and tells pilots what concerns them.
async fn report(run: &mut Run, pilots: &mut HashMap<ShipId, Pilot>, books: &Books) {
    for (tick, event) in run.take_events() {
        info!(target: "frontier::run", run = books.run, tick, event = ?event);
        match event {
            RunEvent::Scanned { .. } => {
                let message = scanned(run);
                for pilot in pilots.values() {
                    let _ = pilot
                        .events
                        .send(PilotEvent::Message(Box::new(message.clone())));
                }
            }
            RunEvent::LimitWarning => {
                let seconds_left = seconds(INSTANCE_LIMIT_TICKS - LIMIT_WARNING_TICKS);
                for pilot in pilots.values() {
                    let message = ServerMessage::LimitWarning { seconds_left };
                    let _ = pilot.events.send(PilotEvent::Message(Box::new(message)));
                }
            }
            RunEvent::Docked { pilot, summary }
            | RunEvent::Destroyed { pilot, summary }
            | RunEvent::Abandoned { pilot, summary } => {
                let lost = if summary.lost.upgraded_modules > 0 {
                    run.fitted(pilot).to_vec()
                } else {
                    Vec::new()
                };
                let holdings = bank(books, pilot, &summary, lost).await;
                if let Some(account) = books.accounts.get(&pilot) {
                    books.runs.remove(*account, &books.run);
                }
                if let Some(home) = pilots.remove(&pilot) {
                    if let Some(holdings) = holdings {
                        let message = ServerMessage::Account { account: holdings };
                        let _ = home.events.send(PilotEvent::Message(Box::new(message)));
                    }
                    let _ = home
                        .events
                        .send(PilotEvent::RunOver(wire::summary(&summary)));
                }
            }
            _ => {}
        }
    }
}

/// Moves a docked pilot's cargo and contract payout into their account through
/// the ledger, and returns what the account holds afterwards. The instance
/// does this itself, so cargo is banked even if the pilot's connection drops
/// as they dock.
async fn bank(
    books: &Books,
    pilot: ShipId,
    summary: &Summary,
    lost: Vec<String>,
) -> Option<AccountInfo> {
    let account = *books.accounts.get(&pilot)?;
    let run = Some(books.run.clone());
    let mut batch: Vec<Transfer> = [Item::ORE, Item::SALVAGE, Item::VOLATILES]
        .into_iter()
        .zip(summary.banked)
        .filter(|&(_, units)| units > 0)
        .map(|(item, units)| Transfer {
            item,
            amount: units,
            from: Party::Site,
            to: Party::Account(account),
            reason: Reason::Banked,
            run: run.clone(),
        })
        .collect();
    if summary.payout > 0 {
        batch.push(Transfer {
            item: Item::Credits,
            amount: summary.payout,
            from: Party::Npc(Npc::Contract),
            to: Party::Account(account),
            reason: Reason::ContractPayout,
            run,
        });
    }
    let run = books.run.clone();
    let completed = summary.ending == outcome::Ending::Docked;
    let result = books
        .store
        .call(move |store| {
            // Banking and processing are one transaction: a completed run
            // finishes the queued batches (design section 10).
            store.update(account, |holdings| {
                if completed {
                    batch.extend(home::finish_processing(account, holdings, &run));
                }
                // Rigs and modules destroyed with the ship (design section 6).
                holdings.hangar.lose_kinds(&lost);
                Ok(batch)
            })
        })
        .await;
    match result {
        Ok(Ok(holdings)) => Some(wire::account(&holdings)),
        Ok(Err(reason)) => {
            warn!(reason, account = account.0, "could not bank the run");
            None
        }
        Err(error) => {
            warn!(%error, account = account.0, "could not bank the run");
            None
        }
    }
}

fn scanned(run: &Run) -> ServerMessage {
    let values = &run.site().values;
    let started = run.objective_started().unwrap_or(0);
    let threat = crew_scaling(run.site().pilots).threat;
    // Small counts, so `as` loses nothing.
    let scale = |count: u32| (count as f32 * threat).round() as u32;
    let waves = values
        .rises
        .iter()
        .enumerate()
        .map(|(step, rise)| {
            let level = step as u32 + 1;
            let wave = run.site().wave(level);
            WaveInfo {
                level,
                at_seconds: seconds(started + rise),
                drones: scale(wave.drones),
                raiders: scale(wave.raiders),
            }
        })
        .filter(|wave| wave.level > run.alert() && wave.at_seconds <= seconds(INSTANCE_LIMIT_TICKS))
        .collect();
    ServerMessage::Scanned {
        rich_pocket: values.rich_pocket as u32,
        relays: values.relays.iter().map(|&r| r as u32).collect(),
        waves,
    }
}

fn joined(run: &Run, ship: ShipId, tier: ThreatTier) -> ServerMessage {
    ServerMessage::Joined {
        site: wire::site_info(run.site()),
        ship_id: ship.0,
        tick_rate: TICKS_PER_SECOND,
        tier: tier.number(),
        obstacles: run
            .world()
            .obstacles()
            .iter()
            .map(|o| ObstacleInfo {
                x: o.x,
                z: o.z,
                radius: o.radius,
            })
            .collect(),
        deposits: run
            .site()
            .deposits
            .iter()
            .take(MAX_DEPOSITS)
            .map(|d| DepositInfo {
                x: d.x,
                z: d.z,
                pocket: d.pocket as u32,
                material: wire::material(d.material),
                units: units(d.remaining),
            })
            .collect(),
        weapon_range: ShipKind::Pilot.profile().weapon.range,
        lock_range: run.lock_range(),
        scan_range: run.scan_range(),
        extract_range: EXTRACT_RANGE,
        speed_factor: run.fitting(ship).map_or(1.0, |fitting| fitting.speed),
        max_shield: run.fitting(ship).map_or(0.0, |fitting| fitting.shield),
        fitted: run.fitted(ship).to_vec(),
    }
}

/// Whole units left, rounded up so a nearly empty deposit still shows one.
fn units(remaining: f32) -> u32 {
    // A deposit holds tens of units, so `as` loses nothing.
    remaining.ceil().max(0.0) as u32
}

/// The snapshot of `world` as `own`'s pilot sees it.
#[must_use]
pub fn snapshot(world: &World, input_ack: u16, own: OwnStatus, deposits: Vec<u8>) -> Snapshot {
    let entities = world
        .ships()
        .iter()
        .map(|ship| {
            let (vx, vz) = ship.state.velocity();
            let max_hull = ship.kind.profile().max_hull;
            EntityState {
                id: ship.id.0,
                kind: entity_kind(ship.kind),
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
    // Events only drive visuals, so beyond what one snapshot carries they are dropped.
    let events = world
        .events()
        .iter()
        .take(MAX_EVENTS)
        .map(|event| match *event {
            Event::Fired {
                shooter,
                target,
                speed,
            } => SnapshotEvent::Fired {
                shooter: shooter.0,
                target: target.0,
                // Projectile speeds are tens of metres per second; `as` saturates beyond 255.
                speed: speed.round() as u8,
            },
            Event::Destroyed { ship, kind, by } => SnapshotEvent::Destroyed {
                ship: ship.0,
                by: by.map(|by| by.0),
                kind: entity_kind(kind),
            },
            Event::Intercepted { shooter, target } => SnapshotEvent::Intercepted {
                shooter: shooter.0,
                target: target.0,
            },
        })
        .collect();
    Snapshot {
        // Snapshots carry the tick modulo 2^32: about 6.8 years at 20 per second.
        tick: (world.tick().get() % (1 << 32)) as u32,
        input_ack,
        own,
        deposits,
        entities,
        events,
    }
}

/// What a pilot's client shows about their own ship and run.
fn own_status(run: &Run, ship: ShipId) -> OwnStatus {
    let Some(status) = run.status(ship) else {
        return OwnStatus::IDLE;
    };
    let (extractor, ticks_left) = match status.extractor {
        ExtractorState::Off => (ExtractorMode::Off, 0),
        ExtractorState::On { .. } => (ExtractorMode::On, 0),
        ExtractorState::Venting { ticks_left, .. } => (ExtractorMode::Venting, ticks_left),
        ExtractorState::CutOut { ticks_left } => (ExtractorMode::CutOut, ticks_left),
    };
    let byte = |value: u32| u8::try_from(value).unwrap_or(u8::MAX);
    OwnStatus {
        phase: match status.phase {
            Phase::AtDock => RunPhase::AtDock,
            Phase::Approach => RunPhase::Approach,
            Phase::Objective => RunPhase::Objective,
            Phase::Return => RunPhase::Return,
            Phase::Docked => RunPhase::Docked,
            Phase::Destroyed => RunPhase::Destroyed,
            Phase::Abandoned => RunPhase::Abandoned,
        },
        cargo: byte(status.cargo),
        capacity: byte(status.capacity),
        // Heat is 0 to 1, so the product fits a byte.
        heat: (status.heat.clamp(0.0, 1.0) * 255.0).round() as u8,
        extractor,
        extractor_seconds: byte(ticks_left.div_ceil(TICKS_PER_SECOND)),
        // Progress is 0 to 1; 254 is the most a scan in hand reports.
        scan: status
            .scan
            .map(|p| (p.clamp(0.0, 1.0) * 254.0).round() as u8),
        alert: byte(status.alert),
        elapsed_seconds: u16::try_from(seconds(status.elapsed)).unwrap_or(u16::MAX),
        drive: match status.drive {
            Drive::Ready => WireDrive::Ready,
            Drive::InSite => WireDrive::InSite,
            Drive::Hunted => WireDrive::Hunted,
            Drive::Extracting => WireDrive::Extracting,
        },
        // Tens of points, so `as` loses nothing.
        shield: run.shield(ship).round().clamp(0.0, 255.0) as u8,
    }
}

/// Sends every pilot the shared entities with their own status and input ack.
fn broadcast(run: &Run, pilots: &mut HashMap<ShipId, Pilot>) {
    let deposits: Vec<u8> = run
        .site()
        .deposits
        .iter()
        .take(MAX_DEPOSITS)
        .map(|d| u8::try_from(units(d.remaining)).unwrap_or(u8::MAX))
        .collect();
    let shared = snapshot(run.world(), 0, OwnStatus::IDLE, deposits);
    pilots.retain(|ship, pilot| {
        let snapshot = Snapshot {
            input_ack: pilot.last_input.unwrap_or(0),
            own: own_status(run, *ship),
            ..shared.clone()
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

fn entity_kind(kind: ShipKind) -> EntityKind {
    match kind {
        ShipKind::Pilot => EntityKind::PlayerShip,
        ShipKind::Drone => EntityKind::Drone,
        ShipKind::Raider => EntityKind::Raider,
        ShipKind::Warden => EntityKind::Warden,
        ShipKind::Core => EntityKind::Core,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use frontier_sim::site::COMPACT_SITE;
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

    fn order(opening_threat: Option<u32>) -> LaunchOrder {
        let store = Store::in_memory().unwrap();
        let pilot = store.create_account("Vega").unwrap();
        LaunchOrder {
            template: COMPACT_SITE,
            seed: 11,
            tier: ThreatTier::I,
            options: Options {
                opening_threat,
                ..Options::default()
            },
            run: "test".into(),
            pilot,
            access: Vec::new(),
            fitted: Vec::new(),
            store,
            runs: Runs::default(),
        }
    }

    #[tokio::test(start_paused = true)]
    async fn sends_twenty_snapshots_a_second_with_the_pilots_status() {
        let (sink, snapshots) = channel();
        let launched = Expedition::launch(order(None), sink, mpsc::unbounded_channel().0);
        tokio::time::sleep(Duration::from_millis(1_025)).await;
        let all: Vec<Vec<u8>> = snapshots.try_iter().collect();
        assert!((20..=22).contains(&all.len()), "{} snapshots", all.len());
        let last = Snapshot::decode(all.last().unwrap()).unwrap();
        assert_eq!(last.own.phase, RunPhase::AtDock);
        assert_eq!(last.own.capacity, 20);
        assert_eq!(last.own.elapsed_seconds, 1);
        assert_eq!(last.deposits.len(), 5, "the ore field's deposits");
        let npcs = last
            .entities
            .iter()
            .filter(|e| e.kind != EntityKind::PlayerShip);
        assert_eq!(npcs.count(), 3, "tier I's opening threat");
        let ServerMessage::Joined {
            deposits, ship_id, ..
        } = launched.joined
        else {
            panic!("expected Joined");
        };
        assert_eq!(ship_id, launched.ship.0);
        assert_eq!(deposits.len(), 5);
    }

    #[tokio::test(start_paused = true)]
    async fn applies_inputs_and_acknowledges_the_last_applied() {
        use frontier_protocol::input::Controls;
        let (sink, snapshots) = channel();
        let launched = Expedition::launch(order(Some(0)), sink, mpsc::unbounded_channel().0);
        let thrust = Controls {
            thrust: 127,
            turn: 0,
            fire: false,
            cruise: false,
        };
        launched.expedition.input(
            launched.ship,
            InputDatagram {
                sequence: 5,
                controls: vec![thrust; 4],
            },
        );
        tokio::time::sleep(Duration::from_millis(500)).await;
        let snapshot = latest(&snapshots).unwrap();
        assert_eq!(snapshot.input_ack, 5);
        let me = snapshot
            .entities
            .iter()
            .find(|e| e.id == launched.ship.0)
            .unwrap();
        assert!(me.vx != 0 || me.vz != 0, "the ship should be moving");
    }

    #[tokio::test(start_paused = true)]
    async fn refuses_extraction_away_from_a_deposit() {
        let (events, mut told) = mpsc::unbounded_channel();
        // Held, or the pilot would be dropped as unreachable.
        let (sink, _snapshots) = channel();
        let launched = Expedition::launch(order(Some(0)), sink, events);
        launched.expedition.extract(launched.ship, true);
        let message = told.recv().await.unwrap();
        assert_eq!(
            message,
            PilotEvent::Message(Box::new(ServerMessage::ExtractRefused {
                reason: ExtractRefusal::NotAtDeposit
            }))
        );
    }

    #[tokio::test(start_paused = true)]
    async fn leaving_abandons_the_run_and_stops_the_instance() {
        let (events, mut told) = mpsc::unbounded_channel();
        let launched = Expedition::launch(order(Some(0)), channel().0, events);
        launched.expedition.leave(launched.ship);
        tokio::time::timeout(Duration::from_secs(1), launched.expedition.stopped())
            .await
            .expect("the instance stops");
        // The pilot left, so they are told nothing.
        assert!(told.try_recv().is_err());
    }

    #[tokio::test(start_paused = true)]
    async fn an_idle_pilot_at_the_dock_is_not_attacked() {
        let (events, mut told) = mpsc::unbounded_channel();
        let (sink, snapshots) = channel();
        let launched = Expedition::launch(order(None), sink, events);
        tokio::time::sleep(Duration::from_secs(30)).await;
        assert!(told.try_recv().is_err(), "no run over");
        let me = latest(&snapshots)
            .unwrap()
            .entities
            .into_iter()
            .find(|e| e.id == launched.ship.0)
            .unwrap();
        assert_eq!(me.hull, u8::MAX);
    }

    #[tokio::test]
    async fn docked_cargo_and_the_payout_reach_the_account_through_the_ledger() {
        use frontier_sim::outcome::{Ending, Loadout};
        use frontier_sim::run::PhaseTimes;
        let store = Store::in_memory().unwrap();
        let account = store.create_account("Vega").unwrap();
        let books = Books {
            run: "run-1".into(),
            store: store.clone(),
            runs: Runs::default(),
            accounts: HashMap::from([(ShipId(0), account)]),
        };
        let summary = Summary {
            ending: Ending::Docked,
            banked: [12, 0, 2],
            payout: 100,
            launch_cost: 40,
            lost: Loadout::default(),
            phases: PhaseTimes::default(),
        };
        let holdings = bank(&books, ShipId(0), &summary, Vec::new()).await.unwrap();
        assert_eq!(
            (holdings.credits, holdings.ore, holdings.volatiles),
            (200, 12, 2)
        );
        let folded = store.ledger_balance(account).unwrap();
        assert!(folded.contains(&(crate::ledger::Item::ORE, 12)));
        assert!(folded.contains(&(crate::ledger::Item::Credits, 200)));
    }

    #[tokio::test]
    async fn a_destroyed_pilot_loses_what_they_flew_and_processing_waits() {
        use frontier_sim::outcome::{Ending, Loadout};
        use frontier_sim::run::PhaseTimes;
        let store = Store::in_memory().unwrap();
        let account = store.create_account("Vega").unwrap();
        store
            .update(account, |holdings| {
                for id in ["escort_rig", "point_defence", "cargo_frame"] {
                    holdings
                        .hangar
                        .add(frontier_sim::fitting::fitting(id).unwrap());
                }
                holdings.processing.add("alloy", 1);
                Ok(Vec::new())
            })
            .unwrap()
            .unwrap();
        let books = Books {
            run: "run-2".into(),
            store: store.clone(),
            runs: Runs::default(),
            accounts: HashMap::from([(ShipId(0), account)]),
        };
        let summary = Summary {
            ending: Ending::Destroyed,
            banked: [0; 3],
            payout: 0,
            launch_cost: 40,
            lost: Loadout {
                upgraded_modules: 2,
                cargo: 5,
            },
            phases: PhaseTimes::default(),
        };
        let lost = vec!["escort_rig".to_owned(), "point_defence".to_owned()];
        let holdings = bank(&books, ShipId(0), &summary, lost).await.unwrap();
        assert_eq!(
            holdings.hangar,
            ["cargo_frame"],
            "the spare in the hangar stays"
        );
        assert_eq!(
            holdings.processing.len(),
            1,
            "only a completed run processes"
        );
    }
}
