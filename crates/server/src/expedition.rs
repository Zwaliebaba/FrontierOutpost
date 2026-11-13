//! One expedition instance: a task that owns a [`Run`], applies its pilots'
//! inputs and actions, and sends every pilot a snapshot each tick until the run
//! ends.
//!
//! A solo launch, or a crew host's, starts an instance; the host's crew
//! follows into it within the join window (design sections 2 and 10). When
//! the instance ends is the run's rule: see `Run::check_end`.

use std::collections::HashMap;
use std::time::Duration;

use frontier_protocol::input::InputDatagram;
use frontier_protocol::snapshot::{
    Drive as WireDrive, EntityKind, EntityState, ExtractorMode, MAX_DEPOSITS, MAX_EVENTS,
    OwnStatus, RELAY_LINKED, RunPhase, Snapshot, SnapshotEvent, quantize_heading,
    quantize_position, quantize_velocity,
};
use frontier_protocol::{
    AccountInfo, DepositInfo, ExtractRefusal, JumpCancel as WireJumpCancel,
    JumpRefusal as WireJumpRefusal, JumpTarget as WireJumpTarget, ObstacleInfo, PotInfo,
    RunSummary, ScanRefusal as WireScanRefusal, ServerMessage, WaveInfo,
};
use frontier_sim::TICKS_PER_SECOND;
use frontier_sim::combat::Event;
use frontier_sim::crew::split;
use frontier_sim::extraction::{EXTRACT_RANGE, ExtractorState, Refusal};
use frontier_sim::jump::{
    Cancel as JumpCancel, Refusal as JumpRefusal, SPOOL_TICKS, Target as JumpTarget,
};
use frontier_sim::profile::ShipKind;
use frontier_sim::run::{
    CONTRACT_MIN_CARGO, Checkpoint, Drive, INSTANCE_LIMIT_TICKS, LIMIT_WARNING_TICKS, Options,
    Phase, Pot, Relay, Run, RunEvent, ScanRefusal, Summary, seconds,
};
use frontier_sim::ship::ShipInput;
use frontier_sim::site::{Capability, Site, SiteTemplate, ThreatTier, crew_scaling};
use frontier_sim::world::{ShipId, World};
use tokio::sync::{mpsc, oneshot};
use tokio::time::{MissedTickBehavior, interval};
use tracing::{debug, info, warn};
use web_transport_quinn::Session;

use crate::events::{GateEvent, Recorded, now_ms, now_secs};
use crate::home;
use crate::input_queue::InputQueue;
use crate::ledger::{Item, Npc, Party, Reason, Transfer};
use crate::runs::{Flying, Runs};
use crate::store::{AccountId, SavedRun, Seat, Store};
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
        // Never waits. quinn queues what the link cannot take yet, and when
        // its buffer (`transport::DATAGRAM_SEND_BUFFER`, a few snapshots) is
        // full it drops the oldest: a stalled client gets the newest state.
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
    /// The pilot's crew changed: members, split, contract or launch.
    CrewChanged,
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
    /// The pilot's share of the pot, in whole percent: 100 flying solo.
    pub share: u32,
    /// The crew the site is scaled for: 1 solo, or every member of the
    /// host's crew, who may follow within the join window.
    pub crew_size: u32,
    pub store: Store,
    pub runs: Runs,
}

/// What an instance needs besides its run to report outcomes.
struct Books {
    run: String,
    store: Store,
    runs: Runs,
    accounts: HashMap<ShipId, AccountId>,
    /// Each pilot's share of the pot, a whole percentage of the host's split.
    shares: HashMap<ShipId, u32>,
    /// Where to tell pilots who have gone home about the pot.
    homes: HashMap<ShipId, mpsc::UnboundedSender<PilotEvent>>,
}

impl Books {
    fn crew(&self) -> Vec<Seat> {
        let mut crew: Vec<Seat> = self
            .accounts
            .iter()
            .map(|(ship, account)| Seat {
                account: *account,
                ship: ship.0,
                share: self.shares.get(ship).copied().unwrap_or(0),
            })
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
    Jump {
        ship: ShipId,
        target: Option<WireJumpTarget>,
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
    Join {
        account: AccountId,
        slot: u32,
        fitted: Vec<String>,
        share: u32,
        sink: Box<dyn SnapshotSink>,
        events: mpsc::UnboundedSender<PilotEvent>,
        reply: oneshot::Sender<Option<(ShipId, ServerMessage)>>,
    },
    Stop {
        reply: oneshot::Sender<()>,
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
        let site = Site::generate(
            order.template,
            order.seed,
            order.tier,
            order.crew_size.max(1),
            &order.access,
        );
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
            shares: HashMap::from([(ship, order.share)]),
            homes: HashMap::new(),
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
            .map(|seat| (ShipId(seat.ship), seat.account))
            .collect();
        let shares: HashMap<ShipId, u32> = saved
            .crew
            .iter()
            .map(|seat| (ShipId(seat.ship), seat.share))
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
            shares,
            homes: HashMap::new(),
        };
        tokio::spawn(fly(receiver, run, HashMap::new(), books));
        Some(expedition)
    }

    /// Saves the run as it stands and stops the instance, for a server that
    /// is shutting down: a restart restores every ship where it was, not
    /// where its last checkpoint happened to fall (design section 6).
    /// Resolves once the checkpoint is written, or at once if the run is over.
    pub async fn stop(&self) {
        let (reply, stopped) = oneshot::channel();
        if self.commands.send(Command::Stop { reply }).is_ok() {
            let _ = stopped.await;
        }
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

    /// Launches a crew member into the host's instance, flying `fitted` for
    /// `share` percent of the pot. Returns their ship and `Joined`, or `None`
    /// once the join window has closed or the crew is all here.
    pub async fn join(
        &self,
        account: AccountId,
        slot: u32,
        fitted: Vec<String>,
        share: u32,
        sink: impl SnapshotSink,
        events: mpsc::UnboundedSender<PilotEvent>,
    ) -> Option<(ShipId, ServerMessage)> {
        let (reply, answer) = oneshot::channel();
        self.commands
            .send(Command::Join {
                account,
                slot,
                fitted,
                share,
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

    /// Spools the pilot's jump drive toward `target`, or cancels it.
    pub fn jump(&self, ship: ShipId, target: Option<WireJumpTarget>) {
        let _ = self.commands.send(Command::Jump { ship, target });
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
    mut books: Books,
) {
    let mut ticker = interval(Duration::from_secs(1) / TICKS_PER_SECOND);
    // After a stall, carry on at the normal rate rather than racing to catch up.
    ticker.set_missed_tick_behavior(MissedTickBehavior::Skip);
    loop {
        tokio::select! {
            command = commands.recv() => match command {
                Some(Command::Join { account, slot, fitted, share, sink, events, reply }) => {
                    let joined = run.join_at(slot).map(|ship| {
                        run.fit(ship, &fitted);
                        pilots.insert(
                            ship,
                            Pilot {
                                sink,
                                events,
                                inputs: InputQueue::default(),
                                last_input: None,
                            },
                        );
                        books.accounts.insert(ship, account);
                        books.shares.insert(ship, share);
                        (ship, joined(&run, ship, run.site().tier))
                    });
                    let _ = reply.send(joined);
                }
                Some(Command::Stop { reply }) => {
                    // Everything settled so far is already banked, at the
                    // end of the turn that settled it; this saves the rest.
                    if run.ended().is_none() {
                        save_checkpoint(&books, &run.checkpoint()).await;
                    }
                    let _ = reply.send(());
                    return;
                }
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
        report(&mut run, &mut pilots, &mut books).await;
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
            if let Err(refusal) = run.scan(ship) {
                let reason = match refusal {
                    ScanRefusal::NotFlying => WireScanRefusal::NotFlying,
                    ScanRefusal::Revealed => WireScanRefusal::Revealed,
                    ScanRefusal::Scanning => WireScanRefusal::Scanning,
                    ScanRefusal::OutOfRange => WireScanRefusal::OutOfRange,
                    ScanRefusal::Moving => WireScanRefusal::Moving,
                };
                tell(pilots, ship, ServerMessage::ScanRefused { reason });
            }
        }
        Command::Jump { ship, target } => {
            let target = target.map(|target| match target {
                WireJumpTarget::Dock => JumpTarget::Dock,
                WireJumpTarget::Pocket { index } => {
                    // An index past usize's range names no pocket either way.
                    JumpTarget::Pocket(usize::try_from(index).unwrap_or(usize::MAX))
                }
            });
            if let Err(refusal) = run.jump(ship, target) {
                let reason = match refusal {
                    JumpRefusal::NotFlying => WireJumpRefusal::NotFlying,
                    JumpRefusal::Hunted => WireJumpRefusal::Hunted,
                    JumpRefusal::Extracting => WireJumpRefusal::Extracting,
                    JumpRefusal::Unavailable => WireJumpRefusal::Unavailable,
                    JumpRefusal::AlreadyThere => WireJumpRefusal::AlreadyThere,
                };
                tell(pilots, ship, ServerMessage::JumpRefused { reason });
            }
        }
        Command::Leave { ship } => {
            pilots.remove(&ship);
            run.leave(ship);
        }
        Command::Disconnect { ship } => {
            pilots.remove(&ship);
            run.disconnect(ship);
        }
        // Joining needs the books, so the instance loop handles it first.
        Command::Join { reply, .. } => {
            let _ = reply.send(None);
        }
        // Stopping saves through the books; the instance loop handles it too.
        Command::Stop { reply } => {
            let _ = reply.send(());
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

/// A jump target as the wire names it.
fn wire_jump_target(target: JumpTarget) -> WireJumpTarget {
    match target {
        JumpTarget::Dock => WireJumpTarget::Dock,
        JumpTarget::Pocket(index) => WireJumpTarget::Pocket {
            // A site has a handful of pockets.
            index: u32::try_from(index).unwrap_or(u32::MAX),
        },
    }
}

fn tell(pilots: &HashMap<ShipId, Pilot>, ship: ShipId, message: ServerMessage) {
    if let Some(pilot) = pilots.get(&ship) {
        let _ = pilot.events.send(PilotEvent::Message(Box::new(message)));
    }
}

/// Saves the run's checkpoint with its crew's seats, replacing the last one,
/// for a restarted server to restore.
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
async fn report(run: &mut Run, pilots: &mut HashMap<ShipId, Pilot>, books: &mut Books) {
    let mut finished: Vec<(ShipId, Summary)> = Vec::new();
    let mut settled: Option<Pot> = None;
    let mut gate: Vec<(ShipId, GateEvent)> = Vec::new();
    for (tick, event) in run.take_events() {
        info!(target: "frontier::run", run = books.run, tick, event = ?event);
        if let Some(recorded) = gate_event(run, &event) {
            gate.push(recorded);
        }
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
                bank(books, pilot, &summary, lost).await;
                if let Some(account) = books.accounts.get(&pilot) {
                    books.runs.remove(*account, &books.run);
                }
                // A pilot whose run is over goes home; the crew may carry on.
                if let Some(home) = pilots.remove(&pilot) {
                    books.homes.insert(pilot, home.events);
                }
                finished.push((pilot, summary));
            }
            RunEvent::PotSettled { pot } => settled = Some(pot),
            RunEvent::JumpSpooling { pilot, target } => {
                let message = ServerMessage::JumpSpooling {
                    ship: u32::from(pilot.0),
                    target: wire_jump_target(target),
                    seconds: seconds(SPOOL_TICKS),
                };
                for observer in pilots.values() {
                    let _ = observer
                        .events
                        .send(PilotEvent::Message(Box::new(message.clone())));
                }
            }
            RunEvent::JumpCancelled { pilot, reason } => {
                let reason = match reason {
                    JumpCancel::Pilot => WireJumpCancel::Pilot,
                    JumpCancel::Hunted => WireJumpCancel::Hunted,
                    JumpCancel::Extracting => WireJumpCancel::Extracting,
                };
                for observer in pilots.values() {
                    let _ = observer.events.send(PilotEvent::Message(Box::new(
                        ServerMessage::JumpCancelled {
                            ship: u32::from(pilot.0),
                            reason,
                        },
                    )));
                }
            }
            RunEvent::Jumped { pilot, target } => {
                let message = ServerMessage::Jumped {
                    ship: u32::from(pilot.0),
                    target: wire_jump_target(target),
                };
                for observer in pilots.values() {
                    let _ = observer
                        .events
                        .send(PilotEvent::Message(Box::new(message.clone())));
                }
            }
            _ => {}
        }
    }
    let shares = match settled {
        Some(pot) => pay_pot(books, &pot).await,
        None => HashMap::new(),
    };
    // The instance ended with the contract unpaid: those who docked still
    // have the supplies they launched with (design section 10).
    if settled.is_some_and(|pot| pot.contract == 0) {
        let docked: Vec<ShipId> = books
            .accounts
            .keys()
            .copied()
            .filter(|&ship| {
                run.status(ship)
                    .is_some_and(|status| status.phase == Phase::Docked)
            })
            .collect();
        carry_supplies(books, &docked).await;
    }
    if settled.is_some() {
        let crew = u32::try_from(books.accounts.len()).unwrap_or(u32::MAX);
        for (&pilot, &share) in &shares {
            gate.push((pilot, GateEvent::PotShare { share, crew }));
        }
    }
    record(books, gate).await;
    // Each pilot hears once, with the account as it stands after the pot.
    let mut told: Vec<ShipId> = Vec::new();
    for (pilot, summary) in finished {
        let payout = shares.get(&pilot).copied().unwrap_or(0);
        if let Some(events) = books.homes.get(&pilot) {
            if let Some(holdings) = holdings(books, pilot).await {
                let message = ServerMessage::Account { account: holdings };
                let _ = events.send(PilotEvent::Message(Box::new(message)));
            }
            let _ = events.send(PilotEvent::RunOver(wire::summary(
                &summary, payout, &books.run, settled,
            )));
        }
        told.push(pilot);
    }
    // Pilots who went home before the end hear about the pot now; those who
    // came home with it saw their share in their summary.
    if let Some(pot) = settled {
        for (&pilot, &share) in &shares {
            if told.contains(&pilot) {
                continue;
            }
            let Some(events) = books.homes.get(&pilot) else {
                continue;
            };
            if let Some(holdings) = holdings(books, pilot).await {
                let message = ServerMessage::Account { account: holdings };
                let _ = events.send(PilotEvent::Message(Box::new(message)));
            }
            let message = ServerMessage::PotPaid {
                run: books.run.clone(),
                pot: PotInfo {
                    contract: pot.contract,
                    bounty: pot.bounty,
                    relay: pot.relay,
                },
                share,
            };
            let _ = events.send(PilotEvent::Message(Box::new(message)));
        }
    }
}

/// The gate event (plan 7.1) a run event stands for, if any, and whose it is.
fn gate_event(run: &Run, event: &RunEvent) -> Option<(ShipId, GateEvent)> {
    let name = |kind: ShipKind| {
        match kind {
            ShipKind::Pilot => "pilot",
            ShipKind::Drone => "drone",
            ShipKind::Raider => "raider",
            ShipKind::Warden => "warden",
            ShipKind::Core => "core",
        }
        .to_owned()
    };
    Some(match *event {
        RunEvent::EnteredPocket { pilot, pocket } => {
            let kind = run.site().pockets.get(pocket)?.template.kind;
            (
                pilot,
                GateEvent::PocketEntered {
                    pocket: format!("{kind:?}"),
                },
            )
        }
        RunEvent::ScanStarted { pilot } => (pilot, GateEvent::ScanStarted),
        RunEvent::ExtractorStarted { pilot, material } => (
            pilot,
            GateEvent::ExtractorStarted {
                material: format!("{material:?}").to_lowercase(),
            },
        ),
        RunEvent::Locked {
            pilot,
            target,
            distance,
            taken,
        } => (
            pilot,
            GateEvent::Locked {
                target: name(target),
                distance,
                taken,
            },
        ),
        RunEvent::Docked { pilot, summary }
        | RunEvent::Destroyed { pilot, summary }
        | RunEvent::Abandoned { pilot, summary } => (
            pilot,
            GateEvent::RunEnded {
                ending: format!("{:?}", summary.ending).to_lowercase(),
                banked: summary.banked.iter().sum(),
                cargo_lost: summary.lost.cargo,
                lost: if summary.lost.upgraded_modules > 0 {
                    run.fitted(pilot).to_vec()
                } else {
                    Vec::new()
                },
                approach_seconds: seconds(summary.phases.approach),
                objective_seconds: seconds(summary.phases.objective),
                return_seconds: seconds(summary.phases.return_leg),
            },
        ),
        _ => return None,
    })
}

/// Records gate events for the pilots they belong to.
async fn record(books: &Books, events: Vec<(ShipId, GateEvent)>) {
    let at = now_ms();
    let recorded: Vec<Recorded> = events
        .into_iter()
        .filter_map(|(ship, event)| {
            Some(Recorded {
                at,
                account: books.accounts.get(&ship)?.0,
                run: Some(books.run.clone()),
                event,
            })
        })
        .collect();
    if recorded.is_empty() {
        return;
    }
    if let Err(error) = books.store.call(move |store| store.record(&recorded)).await {
        warn!(%error, run = books.run, "gate events not recorded");
    }
}

/// What a pilot's account holds now.
async fn holdings(books: &Books, pilot: ShipId) -> Option<AccountInfo> {
    let account = *books.accounts.get(&pilot)?;
    match books.store.call(move |store| store.load(account)).await {
        Ok(holdings) => Some(wire::account(&holdings, now_secs())),
        Err(error) => {
            warn!(%error, account = account.0, "could not load the account");
            None
        }
    }
}

/// Pays the crew's pot when the instance ends (design section 10): every
/// pilot who launched, docked or not, by the host's split (the owner's rule),
/// each part from its source through the ledger. Returns each pilot's total.
async fn pay_pot(books: &Books, pot: &Pot) -> HashMap<ShipId, u32> {
    let mut crew: Vec<(ShipId, AccountId, u32)> = books
        .accounts
        .iter()
        .map(|(&ship, &account)| (ship, account, books.shares.get(&ship).copied().unwrap_or(0)))
        .collect();
    crew.sort_unstable_by_key(|&(ship, ..)| ship);
    let weights: Vec<u32> = crew.iter().map(|&(.., share)| share).collect();
    let mut totals: HashMap<ShipId, u32> = crew.iter().map(|&(ship, ..)| (ship, 0)).collect();
    let mut batch = Vec::new();
    for (amount, npc, reason) in [
        (pot.contract, Npc::Contract, Reason::ContractPayout),
        (pot.bounty, Npc::Bounty, Reason::NestBounty),
        (pot.relay, Npc::Contract, Reason::RelayBonus),
    ] {
        for (&(ship, account, _), paid) in crew.iter().zip(split(amount, &weights)) {
            if paid == 0 {
                continue;
            }
            *totals.entry(ship).or_default() += paid;
            batch.push(Transfer {
                item: Item::Credits,
                amount: paid,
                from: Party::Npc(npc),
                to: Party::Account(account),
                reason,
                run: Some(books.run.clone()),
            });
        }
    }
    if !batch.is_empty()
        && let Err(error) = books.store.call(move |store| store.transfer(&batch)).await
    {
        warn!(%error, run = books.run, "could not pay the pot");
        return HashMap::new();
    }
    totals
}

/// Marks the supplies of the pilots flying `docked` as carried over: their
/// next launch costs nothing. No credits move, so the ledger records nothing.
async fn carry_supplies(books: &Books, docked: &[ShipId]) {
    for ship in docked {
        let Some(&account) = books.accounts.get(ship) else {
            continue;
        };
        let result = books
            .store
            .call(move |store| {
                store.update(account, |holdings| {
                    holdings.supplies_carried = true;
                    Ok(Vec::new())
                })
            })
            .await;
        match result {
            Ok(Ok(_)) => info!(
                target: "frontier::run",
                run = books.run,
                account = account.0,
                "supplies carried over"
            ),
            Ok(Err(reason)) => warn!(reason, account = account.0, "supplies not carried over"),
            Err(error) => warn!(%error, account = account.0, "supplies not carried over"),
        }
    }
}

/// Moves a docked pilot's cargo into their account through
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
    let now = now_secs();
    let result = books
        .store
        .call(move |store| {
            // Banking and processing are one transaction: the processor
            // worked while the pilot flew (design section 10), and they come
            // home to what it finished, however the run ended.
            store.update(account, |holdings| {
                batch.extend(home::settle(account, holdings, now));
                // Rigs and modules destroyed with the ship (design section 6).
                holdings.hangar.lose_kinds(&lost);
                Ok(batch)
            })
        })
        .await;
    match result {
        Ok(Ok(holdings)) => Some(wire::account(&holdings, now)),
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
        contract_cargo: CONTRACT_MIN_CARGO,
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
        relay: match status.relay {
            Relay::Idle => 0,
            Relay::Holding(ticks) => byte(ticks / TICKS_PER_SECOND).min(RELAY_LINKED - 1),
            Relay::Linked => RELAY_LINKED,
        },
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
            share: 100,
            crew_size: 1,
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
    async fn refuses_a_scan_from_the_dock() {
        let (events, mut told) = mpsc::unbounded_channel();
        // Held, or the pilot would be dropped as unreachable.
        let (sink, _snapshots) = channel();
        let launched = Expedition::launch(order(Some(0)), sink, events);
        launched.expedition.scan(launched.ship);
        let message = told.recv().await.unwrap();
        assert_eq!(
            message,
            PilotEvent::Message(Box::new(ServerMessage::ScanRefused {
                reason: WireScanRefusal::OutOfRange
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
    async fn docked_cargo_reaches_the_account_through_the_ledger() {
        use frontier_sim::outcome::{Ending, Loadout};
        use frontier_sim::run::PhaseTimes;
        let store = Store::in_memory().unwrap();
        let account = store.create_account("Vega").unwrap();
        let books = Books {
            run: "run-1".into(),
            store: store.clone(),
            runs: Runs::default(),
            accounts: HashMap::from([(ShipId(0), account)]),
            shares: HashMap::from([(ShipId(0), 100)]),
            homes: HashMap::new(),
        };
        let summary = Summary {
            ending: Ending::Docked,
            banked: [12, 0, 2],
            launch_cost: 40,
            lost: Loadout::default(),
            phases: PhaseTimes::default(),
        };
        let holdings = bank(&books, ShipId(0), &summary, Vec::new()).await.unwrap();
        assert_eq!(
            (holdings.credits, holdings.ore, holdings.volatiles),
            (100, 12, 2),
            "the payout waits for the pot"
        );
        let folded = store.ledger_balance(account).unwrap();
        assert!(folded.contains(&(crate::ledger::Item::ORE, 12)));
    }

    #[tokio::test(start_paused = true)]
    async fn a_launch_is_checkpointed_before_its_first_minute_or_pocket() {
        // Supplies are paid at launch: a server stopped seconds later must
        // still have the run for its pilot.
        let order = order(None);
        let store = order.store.clone();
        let (sink, _snapshots) = channel();
        let _launched = Expedition::launch(order, sink, mpsc::unbounded_channel().0);
        tokio::time::sleep(Duration::from_millis(200)).await;
        let saved = store.checkpoints().unwrap();
        assert_eq!(saved.len(), 1);
        assert_eq!(saved[0].run, "test");
        assert_eq!(saved[0].crew.len(), 1);
    }

    #[tokio::test(start_paused = true)]
    async fn stopping_saves_the_run_where_it_stands_and_ends_the_instance() {
        let order = order(None);
        let (store, runs, pilot) = (order.store.clone(), order.runs.clone(), order.pilot);
        let (sink, _snapshots) = channel();
        let launched = Expedition::launch(order, sink, mpsc::unbounded_channel().0);
        // Short of the first minute, and the pilot enters no pocket.
        tokio::time::sleep(Duration::from_secs(30)).await;
        let elapsed = |store: &Store| -> u32 {
            let saved = store.checkpoints().unwrap();
            let checkpoint: Checkpoint = serde_json::from_str(&saved[0].document).unwrap();
            checkpoint.elapsed
        };
        assert!(elapsed(&store) < TICKS_PER_SECOND, "only the launch so far");

        launched.expedition.stop().await;
        assert!(
            elapsed(&store) >= 29 * TICKS_PER_SECOND,
            "saved at the stop"
        );
        assert!(runs.get(pilot).is_some(), "the run is still the pilot's");
        // The instance is gone: nothing answers, and a second stop returns.
        let (sink, _snapshots) = channel();
        let rejoined = launched
            .expedition
            .reconnect(launched.ship, sink, mpsc::unbounded_channel().0)
            .await;
        assert!(rejoined.is_none());
        launched.expedition.stop().await;
    }

    #[tokio::test(start_paused = true)]
    async fn a_restored_crew_run_waits_for_the_pilot_still_out() {
        use crate::store::{SavedRun, Seat};
        let store = Store::in_memory().unwrap();
        let (vega, rigel) = (
            store.create_account("Vega").unwrap(),
            store.create_account("Rigel").unwrap(),
        );
        let site = Site::generate(COMPACT_SITE, 11, ThreatTier::I, 2, &[]);
        let (mut run, ships) = Run::launch(site, 1, Options::default());
        let (host, wing) = (ships[0], run.join().unwrap());
        let mut checkpoint = run.checkpoint();
        // The host had docked before the server stopped; the wing was out.
        checkpoint.crew[0].phase = Phase::Docked;
        let crew = [
            Seat {
                account: vega,
                ship: host.0,
                share: 50,
            },
            Seat {
                account: rigel,
                ship: wing.0,
                share: 50,
            },
        ];
        let document = serde_json::to_string(&checkpoint).unwrap();
        store.save_checkpoint("run-9", &crew, &document).unwrap();
        let saved = SavedRun {
            run: "run-9".into(),
            crew: crew.to_vec(),
            document,
        };
        let runs = Runs::default();
        let expedition = Expedition::resume(saved, store.clone(), runs.clone()).unwrap();

        // Well past the first tick, which once ended the instance and
        // abandoned the wing before they could come back.
        tokio::time::sleep(Duration::from_secs(20)).await;
        assert!(runs.get(vega).is_none(), "the host's run was over");
        assert!(runs.get(rigel).is_some(), "the wing's run waits for them");
        assert_eq!(store.checkpoints().unwrap().len(), 1);
        let (sink, snapshots) = channel();
        let joined = expedition
            .reconnect(wing, sink, mpsc::unbounded_channel().0)
            .await;
        assert!(matches!(joined, Some(ServerMessage::Joined { .. })));
        tokio::time::sleep(Duration::from_millis(200)).await;
        assert_eq!(latest(&snapshots).unwrap().own.phase, RunPhase::AtDock);
    }

    #[tokio::test]
    async fn the_pot_is_paid_to_every_crew_member_by_their_share_and_source() {
        let store = Store::in_memory().unwrap();
        let (vega, rigel) = (
            store.create_account("Vega").unwrap(),
            store.create_account("Rigel").unwrap(),
        );
        let books = Books {
            run: "run-3".into(),
            store: store.clone(),
            runs: Runs::default(),
            accounts: HashMap::from([(ShipId(0), vega), (ShipId(1), rigel)]),
            shares: HashMap::from([(ShipId(0), 60), (ShipId(1), 40)]),
            homes: HashMap::new(),
        };
        let pot = Pot {
            contract: 160,
            bounty: 120,
            relay: 80,
        };
        let paid = pay_pot(&books, &pot).await;
        assert_eq!(paid.get(&ShipId(0)), Some(&216));
        assert_eq!(paid.get(&ShipId(1)), Some(&144));
        assert_eq!(store.load(vega).unwrap().credits, 316);
        assert_eq!(store.load(rigel).unwrap().credits, 244);
        let reasons: Vec<String> = store
            .lock()
            .prepare("SELECT reason FROM ledger WHERE run = 'run-3' ORDER BY id")
            .unwrap()
            .query_map([], |row| row.get(0))
            .unwrap()
            .collect::<Result<_, _>>()
            .unwrap();
        assert_eq!(
            reasons,
            [
                "contract payout",
                "contract payout",
                "nest bounty",
                "nest bounty",
                "relay bonus",
                "relay bonus"
            ]
        );
    }

    #[tokio::test]
    async fn supplies_carry_over_for_the_pilots_named_and_the_next_launch_uses_them() {
        let store = Store::in_memory().unwrap();
        let (vega, rigel) = (
            store.create_account("Vega").unwrap(),
            store.create_account("Rigel").unwrap(),
        );
        let books = Books {
            run: "run-4".into(),
            store: store.clone(),
            runs: Runs::default(),
            accounts: HashMap::from([(ShipId(0), vega), (ShipId(1), rigel)]),
            shares: HashMap::from([(ShipId(0), 50), (ShipId(1), 50)]),
            homes: HashMap::new(),
        };
        // Vega docked; Rigel was destroyed, and their supplies with the ship.
        carry_supplies(&books, &[ShipId(0)]).await;
        assert!(store.load(vega).unwrap().supplies_carried);
        assert!(!store.load(rigel).unwrap().supplies_carried);
        assert_eq!(home::supplies_due(&store.load(vega).unwrap()), Ok(0));
        assert_eq!(
            wire::account(&store.load(vega).unwrap(), 0).launch.due,
            frontier_protocol::SuppliesDue::Carried
        );
        assert_eq!(store.load(vega).unwrap().credits, 100, "no credits moved");
    }

    #[tokio::test]
    async fn a_destroyed_pilot_loses_what_they_flew_and_keeps_what_the_processor_finished() {
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
                // Loaded five minutes ago, at four minutes a batch.
                holdings.processing.add("alloy", 2, now_secs() - 300);
                Ok(Vec::new())
            })
            .unwrap()
            .unwrap();
        let books = Books {
            run: "run-2".into(),
            store: store.clone(),
            runs: Runs::default(),
            accounts: HashMap::from([(ShipId(0), account)]),
            shares: HashMap::from([(ShipId(0), 100)]),
            homes: HashMap::new(),
        };
        let summary = Summary {
            ending: Ending::Destroyed,
            banked: [0; 3],
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
        assert_eq!(holdings.alloy, 1, "the processor worked while they flew");
        assert_eq!(holdings.processing[0].batches, 1);
        assert_eq!(
            holdings.working.map(|working| working.recipe),
            Some("alloy".to_owned()),
            "and is on the second batch"
        );
    }
}
