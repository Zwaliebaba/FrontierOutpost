//! The WebTransport endpoint: accepts sessions and runs each one.
//!
//! A session opens with one bidirectional stream, the control stream, which
//! carries framed JSON messages ([`frontier_protocol::frame`]). The first
//! client message must be `Hello`; the server answers with [`handshake`].

use std::hash::{BuildHasher, RandomState};
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::{Duration, Instant};

use frontier_protocol::input::InputDatagram;
use frontier_protocol::{ClientMessage, ServerMessage, frame};
use frontier_sim::run::Options;
use frontier_sim::site::{Capability, FRONTIER_SITE, SiteTemplate, ThreatTier};
use frontier_sim::world::ShipId;
use tokio::sync::{Notify, mpsc};
use tracing::{debug, info, warn};
use web_transport_quinn::{RecvStream, SendStream, Server, Session, crypto, quinn};

use crate::auth::{Auth, AuthError, Ceremony};
use crate::cert::DevCertificate;
use crate::crews::{self, Crews};
use crate::events::{GateEvent, Placed, Recorded, now_ms};
use crate::expedition::{Expedition, LaunchOrder, PilotEvent};
use crate::home::{self, Action};
use crate::ledger::{Item, Npc, Party, Reason, Transfer};
use crate::runs::{Flying, Runs};
use crate::store::{AccountId, Store, StoreError, random_bytes, random_hex};
use crate::{handshake, wire};

/// What the server offers every pilot, and how runs launch.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Settings {
    /// The site every contract offers.
    pub template: SiteTemplate,
    /// Tiers every pilot may choose, up to 3, whatever their facilities have
    /// opened (design section 10): a switch for trying higher tiers early.
    pub open_tiers: u32,
    /// A fixed seed for every contract, for tests; random otherwise.
    pub seed: Option<u32>,
    /// Replaces the opening threat, for benchmarks.
    pub opening_threat: Option<u32>,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            template: FRONTIER_SITE,
            open_tiers: 1,
            seed: None,
            opening_threat: None,
        }
    }
}

impl Settings {
    fn next_seed(&self) -> u32 {
        // A per-process random hasher of a varying value: a seed nobody can guess.
        self.seed.unwrap_or_else(|| {
            let nanos = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map_or(0, |since| since.as_nanos());
            RandomState::new().hash_one(nanos) as u32
        })
    }
}

/// Why a session ended other than by the client leaving normally.
#[derive(Debug, thiserror::Error)]
pub enum SessionError {
    #[error(transparent)]
    Session(#[from] web_transport_quinn::SessionError),
    #[error("could not read from the control stream: {0}")]
    Read(#[from] web_transport_quinn::ReadExactError),
    #[error("could not write to the control stream: {0}")]
    Write(#[from] web_transport_quinn::WriteError),
    #[error(transparent)]
    Frame(#[from] frame::FrameError),
    #[error("the control stream was closed")]
    Closed,
    #[error("the client sent a message out of turn")]
    Unexpected,
    #[error(transparent)]
    Store(#[from] StoreError),
}

/// Session close code for a client that broke the protocol.
const PROTOCOL_VIOLATION: u32 = 1;
/// Session close code for a client whose protocol version is not supported.
const VERSION_REJECTED: u32 = 2;
/// Session close code when the pilot signed in on another page. The client
/// does not reconnect after it, or two pages would keep displacing each other.
pub const SIGNED_IN_ELSEWHERE: u32 = 4;
/// How long the server waits for a displaced page to read why and close.
const PARTING_GRACE: Duration = Duration::from_secs(2);

/// How long a connection may go without hearing from the client before the
/// server drops it. Browsers do not always close a session when a page closes,
/// and QUIC's usual 30 s would leave a ghost ship that long.
pub const IDLE_TIMEOUT: Duration = Duration::from_secs(10);

/// How often the server pings a client. A pilot at home between runs sends
/// nothing, but their browser acknowledges each ping, which keeps the
/// connection alive; a closed page stops acknowledging and is dropped after
/// [`IDLE_TIMEOUT`]. A third of the timeout leaves room for a lost ping or two.
fn keep_alive(idle_timeout: Duration) -> Duration {
    idle_timeout / 3
}

/// Why the WebTransport endpoint could not be bound.
#[derive(Debug, thiserror::Error)]
pub enum BindError {
    #[error("could not configure TLS: {0}")]
    Tls(#[from] rustls::Error),
    #[error("TLS configuration is unusable for QUIC: {0}")]
    Quic(#[from] quinn::crypto::rustls::NoInitialCipherSuite),
    #[error("idle timeout out of range: {0}")]
    IdleTimeout(#[from] quinn::VarIntBoundsExceeded),
    #[error("could not bind the WebTransport socket: {0}")]
    Io(#[from] std::io::Error),
}

/// Binds the WebTransport endpoint with [`IDLE_TIMEOUT`].
///
/// # Errors
/// Fails if the address cannot be bound or the certificate is unusable.
pub fn bind(addr: SocketAddr, cert: &DevCertificate) -> Result<Server, BindError> {
    bind_with_idle_timeout(addr, cert, IDLE_TIMEOUT)
}

/// Binds the WebTransport endpoint, dropping sessions silent for `idle_timeout`.
///
/// This builds the QUIC endpoint itself, as `web_transport_quinn::ServerBuilder`
/// does, because the builder offers no way to set the idle timeout.
///
/// # Errors
/// Fails if the address cannot be bound or the certificate is unusable.
pub fn bind_with_idle_timeout(
    addr: SocketAddr,
    cert: &DevCertificate,
    idle_timeout: Duration,
) -> Result<Server, BindError> {
    let mut tls = rustls::ServerConfig::builder_with_provider(crypto::default_provider())
        .with_protocol_versions(&[&rustls::version::TLS13])?
        .with_no_client_auth()
        .with_single_cert(cert.chain.clone(), cert.key.clone_key())?;
    tls.alpn_protocols = vec![web_transport_quinn::ALPN.as_bytes().to_vec()];

    let mut transport = quinn::TransportConfig::default();
    transport.max_idle_timeout(Some(idle_timeout.try_into()?));
    transport.keep_alive_interval(Some(keep_alive(idle_timeout)));
    let mut config = quinn::ServerConfig::with_crypto(Arc::new(
        quinn::crypto::rustls::QuicServerConfig::try_from(tls)?,
    ));
    config.transport_config(Arc::new(transport));
    Ok(Server::new(quinn::Endpoint::server(config, addr)?))
}

/// The world's saves and the relying party that signs pilots in. Cheap to clone.
#[derive(Clone)]
pub struct Identity {
    pub store: Store,
    pub auth: Auth,
    /// Live runs and signed-in sessions, shared by every session.
    pub runs: Runs,
    /// Crews formed by invite code, shared by every session.
    pub crews: Crews,
}

/// Accepts sessions until the endpoint is closed, running each on its own task.
pub async fn serve(mut server: Server, settings: Settings, identity: Identity) {
    while let Some(request) = server.accept().await {
        let peer = request.conn().remote_address();
        let identity = identity.clone();
        tokio::spawn(async move {
            match request.ok().await {
                Ok(session) => {
                    info!(%peer, "session opened");
                    match run_session(&session, settings, identity).await {
                        Ok(()) => info!(%peer, "session closed"),
                        Err(error) => {
                            warn!(%peer, %error, "session ended");
                            session.close(PROTOCOL_VIOLATION, b"protocol violation");
                        }
                    }
                }
                Err(error) => debug!(%peer, %error, "WebTransport handshake failed"),
            }
        });
    }
}

async fn run_session(
    session: &Session,
    settings: Settings,
    identity: Identity,
) -> Result<(), SessionError> {
    let (mut send, mut recv) = session.accept_bi().await?;
    let hello: ClientMessage = read_frame(&mut recv).await?;
    let reply = handshake(&hello);
    write_frame(&mut send, &reply).await?;
    if let ServerMessage::Rejected { .. } = reply {
        let _ = send.finish();
        session.close(VERSION_REJECTED, b"protocol version not supported");
        return Ok(());
    }

    let (events_tx, events) = mpsc::unbounded_channel();
    let mut pilot = Pilot {
        session,
        settings,
        identity,
        send,
        events_tx,
        account: None,
        callsign: String::new(),
        ceremony: None,
        flying: None,
        seed: 0,
        offered_at: Instant::now(),
        home_since: None,
        signed_in_at: None,
        leave: None,
    };
    let result = pilot.fly(recv, events).await;
    if let (Some(account), Some(since)) = (pilot.account, pilot.signed_in_at) {
        let millis = u64::try_from(since.elapsed().as_millis()).unwrap_or(u64::MAX);
        pilot
            .gate(account, None, GateEvent::SessionEnded { millis })
            .await;
    }
    // The run waits for a pilot whose connection dropped (design section 6).
    match &pilot.flying {
        Some((expedition, ship)) => expedition.disconnect(*ship),
        None => pilot.log_unloading(),
    }
    if let (Some(account), Some(leave)) = (pilot.account, &pilot.leave) {
        pilot.identity.runs.sign_out(account, leave);
    }
    result
}

/// One connected pilot: signing in, at home with a contract on offer, or
/// flying a run.
struct Pilot<'a> {
    session: &'a Session,
    settings: Settings,
    identity: Identity,
    send: SendStream,
    events_tx: mpsc::UnboundedSender<PilotEvent>,
    /// The signed-in pilot's account; `None` until they sign in.
    account: Option<AccountId>,
    /// Their callsign, for their crew.
    callsign: String,
    /// A passkey ceremony waiting for the browser's credential.
    ceremony: Option<Ceremony>,
    /// The run being flown and the pilot's ship in it.
    flying: Option<(Expedition, ShipId)>,
    /// The seed of the contract on offer.
    seed: u32,
    /// When the contract was offered: preparation starts here.
    offered_at: Instant,
    /// When the pilot's last run ended: unloading starts here.
    home_since: Option<Instant>,
    /// When the pilot signed in on this page, for the session's length.
    signed_in_at: Option<Instant>,
    /// Tells this session to leave when the pilot signs in on another page.
    leave: Option<Arc<Notify>>,
}

impl Pilot<'_> {
    /// Offers a new contract: a crew member is offered the host's.
    async fn offer(&mut self) -> Result<(), SessionError> {
        self.offered_at = Instant::now();
        let crew = self
            .account
            .and_then(|account| self.identity.crews.view(account));
        match crew {
            Some(view) if !view.is_host() => self.seed = view.seed,
            Some(view) => {
                self.seed = self.settings.next_seed();
                self.identity.crews.set_seed(view.you, self.seed);
                let others: Vec<AccountId> = view
                    .accounts()
                    .into_iter()
                    .filter(|&a| a != view.you)
                    .collect();
                self.identity.crews.notify(&others);
            }
            None => self.seed = self.settings.next_seed(),
        }
        self.show_contract().await
    }

    /// What the pilot's crew opens between them, or the pilot alone: the
    /// pockets any member's powered layout opens (design section 2: access is
    /// shared) and the highest tier any member has opened.
    async fn crew_home(&self, account: AccountId) -> Result<(Vec<Capability>, u32), SessionError> {
        let members = self
            .identity
            .crews
            .view(account)
            .map_or_else(|| vec![account], |view| view.accounts());
        let floor = self.settings.open_tiers;
        let opened = self
            .identity
            .store
            .call(move |s| {
                let mut access: Vec<Capability> = Vec::new();
                let mut tiers = floor;
                for member in members {
                    let holdings = s.load(member)?;
                    access.extend(home::capabilities(&holdings));
                    tiers = tiers.max(home::open_tiers(&holdings));
                }
                access.sort_unstable();
                access.dedup();
                Ok((access, tiers))
            })
            .await?;
        Ok(opened)
    }

    /// Sends the contract on offer as the pilot's crew (or the pilot alone)
    /// opens it: the tiers and the pockets.
    async fn show_contract(&mut self) -> Result<(), SessionError> {
        let Some(account) = self.account else {
            return Ok(());
        };
        let (access, tiers) = self.crew_home(account).await?;
        let contract = wire::contract(self.settings.template, self.seed, tiers, &access);
        write_frame(&mut self.send, &contract).await
    }

    /// Shows the pilot their crew as it stands, and the crew's contract.
    async fn crew_changed(&mut self) -> Result<(), SessionError> {
        let Some(account) = self.account else {
            return Ok(());
        };
        let view = self.identity.crews.view(account);
        let message = ServerMessage::Crew {
            crew: view.as_ref().map(wire::crew),
        };
        write_frame(&mut self.send, &message).await?;
        if self.flying.is_some() {
            return Ok(());
        }
        if let Some(view) = view.filter(|view| !view.is_host()) {
            self.seed = view.seed;
        }
        self.show_contract().await
    }

    /// Forms, joins or leaves a crew, or sets its split.
    async fn crew(
        &mut self,
        account: AccountId,
        message: ClientMessage,
    ) -> Result<(), SessionError> {
        if self.flying.is_some() {
            let reason = "That waits until you are home.".to_owned();
            return write_frame(&mut self.send, &ServerMessage::CrewRefused { reason }).await;
        }
        let crews = self.identity.crews.clone();
        let outcome = match message {
            ClientMessage::CreateCrew => {
                crews.create(
                    account,
                    &self.callsign,
                    self.events_tx.clone(),
                    self.seed,
                    || random_bytes::<1>().map_or(0, |[byte]| byte),
                );
                Ok(Vec::new())
            }
            ClientMessage::JoinCrew { code } => crews
                .join(&code, account, &self.callsign, self.events_tx.clone())
                .map(|()| Vec::new()),
            ClientMessage::LeaveCrew => Ok(crews.leave(account)),
            ClientMessage::SetSplit { shares } => {
                crews.set_split(account, &shares).map(|()| Vec::new())
            }
            _ => return Err(SessionError::Unexpected),
        };
        match outcome {
            Ok(left_behind) => {
                // Everyone in the crew now, and anyone it left behind, hears.
                let mut tell = crews
                    .view(account)
                    .map(|view| view.accounts())
                    .unwrap_or_default();
                tell.extend(left_behind);
                tell.retain(|&member| member != account);
                crews.notify(&tell);
                self.crew_changed().await
            }
            Err(reason) => {
                write_frame(&mut self.send, &ServerMessage::CrewRefused { reason }).await
            }
        }
    }

    /// Does what the pilot asked at home, and shows them the outcome.
    async fn at_home(&mut self, account: AccountId, action: Action) -> Result<(), SessionError> {
        let changes_outpost = action.changes_outpost();
        let logged = format!("{action:?}");
        let outcome = self
            .identity
            .store
            .call(move |s| {
                let before = s.load(account)?;
                let socket = match &action {
                    Action::Install { socket, .. }
                    | Action::Upgrade { socket }
                    | Action::Remove { socket }
                    | Action::Power { socket, .. } => Some(socket.clone()),
                    _ => None,
                };
                let outcome =
                    s.update(account, |holdings| home::act(account, holdings, &action))?;
                // A change to the outpost, for gate 1 (design section 9).
                if let (Ok(after), Some(socket)) = (&outcome, socket) {
                    let event = outpost_changed(&before, after, &socket);
                    s.record(&[Recorded {
                        at: now_ms(),
                        account: account.0,
                        run: None,
                        event,
                    }])?;
                }
                Ok(outcome)
            })
            .await?;
        match outcome {
            Ok(holdings) => {
                info!(target: "frontier::home", account = account.0, action = logged, "done");
                let message = ServerMessage::Account {
                    account: wire::account(&holdings),
                };
                write_frame(&mut self.send, &message).await?;
                if changes_outpost {
                    self.show_contract().await?;
                }
                Ok(())
            }
            Err(reason) => {
                debug!(
                    account = account.0,
                    action = logged,
                    reason,
                    "refused at home"
                );
                write_frame(&mut self.send, &ServerMessage::HomeRefused { reason }).await
            }
        }
    }

    /// Logs the time since the last run ended: design section 2's unloading
    /// and decisions, which in Stage 1's first build runs into preparation.
    fn log_unloading(&mut self) {
        if let Some(since) = self.home_since.take() {
            info!(target: "frontier::run", seconds = since.elapsed().as_secs(), "unloading");
        }
    }

    /// Passes the pilot's datagrams and control messages to their run, and the
    /// run's news to the pilot, until the session ends.
    async fn fly(
        &mut self,
        mut recv: RecvStream,
        mut events: mpsc::UnboundedReceiver<PilotEvent>,
    ) -> Result<(), SessionError> {
        // Frames are read on their own task: reading one is not cancel-safe, so
        // it must not race other events in the select below.
        let (frames_tx, mut frames) = mpsc::unbounded_channel();
        tokio::spawn(async move {
            loop {
                let frame = read_frame::<ClientMessage>(&mut recv).await;
                let failed = frame.is_err();
                if frames_tx.send(frame).is_err() || failed {
                    return;
                }
            }
        });

        loop {
            tokio::select! {
                datagram = self.session.read_datagram() => match datagram {
                    Ok(bytes) => match (InputDatagram::decode(&bytes), &self.flying) {
                        (Ok(input), Some((expedition, ship))) => expedition.input(*ship, input),
                        // Inputs sent from home go nowhere.
                        (Ok(_), None) => {}
                        // A malformed datagram is dropped, as a lost one would be.
                        (Err(error), _) => debug!(%error, "datagram ignored"),
                    },
                    // The session closed while reading: the client left.
                    Err(_) => return Ok(()),
                },
                frame = frames.recv() => match frame {
                    Some(Ok(message)) => self.handle(message).await?,
                    // The client finished the control stream: it is leaving.
                    Some(Err(SessionError::Read(_))) | None => return Ok(()),
                    Some(Err(error)) => return Err(error),
                },
                Some(event) = events.recv() => match event {
                    PilotEvent::Message(message) => write_frame(&mut self.send, &message).await?,
                    PilotEvent::CrewChanged => self.crew_changed().await?,
                    PilotEvent::RunOver(summary) => {
                        self.flying = None;
                        self.home_since = Some(Instant::now());
                        write_frame(&mut self.send, &ServerMessage::RunOver { summary }).await?;
                        self.offer().await?;
                    }
                },
                () = told_to_leave(self.leave.clone()) => {
                    // The other page has the run now: this one must not
                    // report a disconnect that would hand it back.
                    self.flying = None;
                    let reason = "Signed in on another page.".to_owned();
                    let _ = write_frame(&mut self.send, &ServerMessage::SignInRefused { reason }).await;
                    // Closing at once can lose the message; the page closes when it reads it.
                    let _ = tokio::time::timeout(PARTING_GRACE, self.session.closed()).await;
                    self.session.close(SIGNED_IN_ELSEWHERE, b"signed in elsewhere");
                    return Ok(());
                },
                _ = self.session.closed() => return Ok(()),
            }
        }
    }

    async fn handle(&mut self, message: ClientMessage) -> Result<(), SessionError> {
        let Some(account) = self.account else {
            return self.sign_in(message).await;
        };
        let flying = self.flying.as_ref().map(|(e, s)| (e.clone(), *s));
        if let Some(action) = home_action(&message) {
            if flying.is_some() {
                let reason = "That waits until you are home.".to_owned();
                return write_frame(&mut self.send, &ServerMessage::HomeRefused { reason }).await;
            }
            return self.at_home(account, action).await;
        }
        if matches!(
            message,
            ClientMessage::CreateCrew
                | ClientMessage::JoinCrew { .. }
                | ClientMessage::LeaveCrew
                | ClientMessage::SetSplit { .. }
        ) {
            return self.crew(account, message).await;
        }
        match (message, flying) {
            (ClientMessage::Launch { tier }, None) => self.launch(account, tier).await?,
            (ClientMessage::Lock { target }, Some((expedition, ship))) => {
                expedition.lock(ship, target.map(ShipId));
            }
            (ClientMessage::Extract { on }, Some((expedition, ship))) => {
                expedition.extract(ship, on);
            }
            (ClientMessage::Vent, Some((expedition, ship))) => expedition.vent(ship),
            (ClientMessage::Scan, Some((expedition, ship))) => expedition.scan(ship),
            // Actions sent just as a run ends arrive too late to matter.
            (
                ClientMessage::Lock { .. }
                | ClientMessage::Extract { .. }
                | ClientMessage::Vent
                | ClientMessage::Scan,
                None,
            ) => {}
            (
                ClientMessage::Install { .. }
                | ClientMessage::Upgrade { .. }
                | ClientMessage::Remove { .. }
                | ClientMessage::Power { .. }
                | ClientMessage::Sell { .. }
                | ClientMessage::Buy { .. }
                | ClientMessage::Process { .. }
                | ClientMessage::Unqueue { .. }
                | ClientMessage::BuyFitting { .. }
                | ClientMessage::SaveFitting { .. }
                | ClientMessage::CreateCrew
                | ClientMessage::JoinCrew { .. }
                | ClientMessage::LeaveCrew
                | ClientMessage::SetSplit { .. },
                _,
            ) => unreachable!("home and crew actions are handled above"),
            (
                ClientMessage::Launch { .. }
                | ClientMessage::Hello { .. }
                | ClientMessage::Register { .. }
                | ClientMessage::SignIn { .. }
                | ClientMessage::PasskeyCredential { .. }
                | ClientMessage::Resume { .. },
                _,
            ) => return Err(SessionError::Unexpected),
        }
        Ok(())
    }

    /// Handles a message before the pilot has signed in: only the passkey
    /// ceremonies and resumption are allowed.
    async fn sign_in(&mut self, message: ClientMessage) -> Result<(), SessionError> {
        let (store, auth) = (self.identity.store.clone(), self.identity.auth.clone());
        let outcome = match message {
            ClientMessage::Register { callsign } => {
                let begun = store
                    .call(move |s| Ok(auth.begin_registration(s, &callsign)))
                    .await?;
                return self.begun(begun).await;
            }
            ClientMessage::SignIn { callsign } => {
                let begun = store
                    .call(move |s| Ok(auth.begin_sign_in(s, &callsign)))
                    .await?;
                return self.begun(begun).await;
            }
            ClientMessage::PasskeyCredential { credential } => {
                let Some(ceremony) = self.ceremony.take() else {
                    return Err(SessionError::Unexpected);
                };
                store
                    .call(move |s| Ok(auth.finish(s, ceremony, &credential)))
                    .await?
            }
            ClientMessage::Resume { token } => store
                .call(move |s| s.resume(&token))
                .await?
                .ok_or(AuthError::NoSuchPilot(String::new())),
            _ => return Err(SessionError::Unexpected),
        };
        match outcome {
            Ok(account) => self.signed_in(account).await,
            Err(error) => {
                debug!(%error, "sign-in refused");
                let reason = match error {
                    AuthError::NoSuchPilot(callsign) if callsign.is_empty() => {
                        "Your session has expired. Sign in with your passkey.".to_owned()
                    }
                    other => other.reason(),
                };
                write_frame(&mut self.send, &ServerMessage::SignInRefused { reason }).await
            }
        }
    }

    async fn begun(
        &mut self,
        begun: Result<(String, Ceremony), AuthError>,
    ) -> Result<(), SessionError> {
        match begun {
            Ok((options, ceremony)) => {
                self.ceremony = Some(ceremony);
                write_frame(&mut self.send, &ServerMessage::PasskeyOptions { options }).await
            }
            Err(error) => {
                debug!(%error, "ceremony not started");
                let reason = error.reason();
                write_frame(&mut self.send, &ServerMessage::SignInRefused { reason }).await
            }
        }
    }

    async fn signed_in(&mut self, account: AccountId) -> Result<(), SessionError> {
        let (token, holdings) = self
            .identity
            .store
            .call(move |s| Ok((s.issue_token(account)?, s.load(account)?)))
            .await?;
        info!(
            account = account.0,
            callsign = holdings.callsign,
            "signed in"
        );
        self.account = Some(account);
        self.leave = Some(self.identity.runs.sign_in(account));
        self.signed_in_at = Some(Instant::now());
        self.gate(account, None, GateEvent::SessionStarted).await;
        self.callsign.clone_from(&holdings.callsign);
        // A member whose page reloaded is still in their crew.
        self.identity
            .crews
            .reattach(account, self.events_tx.clone());
        let message = ServerMessage::SignedIn {
            callsign: holdings.callsign.clone(),
            token,
            account: wire::account(&holdings),
        };
        write_frame(&mut self.send, &message).await?;
        let catalog = ServerMessage::Catalog {
            catalog: wire::catalog(),
        };
        write_frame(&mut self.send, &catalog).await?;
        if let Some(view) = self.identity.crews.view(account) {
            let message = ServerMessage::Crew {
                crew: Some(wire::crew(&view)),
            };
            write_frame(&mut self.send, &message).await?;
        }
        // A pilot whose run is still going rejoins it (design section 6).
        if let Some(flying) = self.identity.runs.get(account) {
            let rejoined = flying
                .expedition
                .reconnect(flying.ship, self.session.clone(), self.events_tx.clone())
                .await;
            if let Some(joined) = rejoined {
                info!(account = account.0, run = flying.run, "rejoined");
                write_frame(&mut self.send, &joined).await?;
                self.flying = Some((flying.expedition, flying.ship));
                return Ok(());
            }
        }
        self.offer().await
    }

    /// Pays the pilot's launch supplies for the run `run`, if `tier` is open
    /// to them (`open_tiers`). Returns what they paid and their holdings
    /// after, or the reason to refuse.
    async fn pay_supplies(
        &self,
        account: AccountId,
        run: &str,
        tier: Option<ThreatTier>,
    ) -> Result<Result<(u32, crate::account::Account), String>, SessionError> {
        let paid_for = run.to_owned();
        Ok(self
            .identity
            .store
            .call(move |s| {
                if tier.is_none() {
                    return Ok(Err("That tier is not open.".to_owned()));
                }
                let holdings = s.load(account)?;
                let due = match home::supplies_due(&holdings) {
                    Ok(due) => due,
                    Err(reason) => return Ok(Err(reason)),
                };
                if due > 0 {
                    s.transfer(&[Transfer {
                        item: Item::Credits,
                        amount: due,
                        from: Party::Account(account),
                        to: Party::Npc(Npc::Supplies),
                        reason: Reason::LaunchSupplies,
                        run: Some(paid_for),
                    }])?;
                }
                Ok(Ok((due, s.load(account)?)))
            })
            .await?)
    }

    /// Launches the pilot: solo, as a crew's host, or following their host
    /// into the instance they launched (the owner's choice for plan M6).
    async fn launch(&mut self, account: AccountId, tier: u32) -> Result<(), SessionError> {
        let crew = self.identity.crews.view(account);
        if let Some(view) = crew.as_ref().filter(|view| !view.is_host()) {
            let share = view.share(account).unwrap_or(0);
            return self.follow(account, share).await;
        }
        let (access, open) = self.crew_home(account).await?;
        let tier = ThreatTier::from_number(tier).filter(|t| t.number() <= open);
        let run = random_hex(8)?;
        let (paid, holdings) = match self.pay_supplies(account, &run, tier).await? {
            Ok(paid) => paid,
            Err(reason) => {
                return write_frame(&mut self.send, &ServerMessage::LaunchRefused { reason }).await;
            }
        };
        let Some(tier) = tier else {
            return Err(SessionError::Unexpected);
        };
        self.log_unloading();
        info!(
            target: "frontier::run",
            run,
            seconds = self.offered_at.elapsed().as_secs(),
            "preparation"
        );
        let crew_size = crew.as_ref().map_or(1, |view| view.members.len());
        let run_id = run.clone();
        let order = LaunchOrder {
            template: self.settings.template,
            seed: self.seed,
            tier,
            options: Options {
                opening_threat: self.settings.opening_threat,
                supplies_paid: Some(paid),
            },
            run,
            pilot: account,
            share: crew
                .as_ref()
                .and_then(|view| view.share(account))
                .unwrap_or(100),
            crew_size: u32::try_from(crew_size).unwrap_or(1),
            access,
            fitted: fitted(&holdings),
            store: self.identity.store.clone(),
            runs: self.identity.runs.clone(),
        };
        let role = match &crew {
            Some(view) if view.members.len() > 1 => "host",
            _ => "solo",
        };
        let event = launched_event(&holdings, self.seed, tier, crew_size, role, paid);
        self.gate(account, Some(run_id.clone()), event).await;
        let launched = Expedition::launch(order, self.session.clone(), self.events_tx.clone());
        if let Some(view) = crew.filter(|view| view.members.len() > 1) {
            self.identity.crews.launched(
                account,
                crews::Launch {
                    run: run_id,
                    expedition: launched.expedition.clone(),
                    tier,
                    opened: Instant::now(),
                },
            );
            let others: Vec<AccountId> = view
                .accounts()
                .into_iter()
                .filter(|&a| a != account)
                .collect();
            self.identity.crews.notify(&others);
        }
        let message = ServerMessage::Account {
            account: wire::account(&holdings),
        };
        write_frame(&mut self.send, &message).await?;
        write_frame(&mut self.send, &launched.joined).await?;
        self.flying = Some((launched.expedition, launched.ship));
        Ok(())
    }

    /// Follows the crew's host into the instance they launched, for `share`
    /// percent of the pot.
    async fn follow(&mut self, account: AccountId, share: u32) -> Result<(), SessionError> {
        let Some(launch) = self.identity.crews.open_launch(account) else {
            let reason = "The host launches the crew; follow within a minute of them.".to_owned();
            return write_frame(&mut self.send, &ServerMessage::LaunchRefused { reason }).await;
        };
        let run = launch.run.clone();
        let (paid, holdings) = match self.pay_supplies(account, &run, Some(launch.tier)).await? {
            Ok(paid) => paid,
            Err(reason) => {
                return write_frame(&mut self.send, &ServerMessage::LaunchRefused { reason }).await;
            }
        };
        let joined = launch
            .expedition
            .join(
                account,
                fitted(&holdings),
                share,
                self.session.clone(),
                self.events_tx.clone(),
            )
            .await;
        let Some((ship, joined)) = joined else {
            let reason = "The crew's launch window has closed.".to_owned();
            return write_frame(&mut self.send, &ServerMessage::LaunchRefused { reason }).await;
        };
        info!(target: "frontier::run", account = account.0, ship = ship.0, "followed the host");
        let crew_size = self
            .identity
            .crews
            .view(account)
            .map_or(1, |view| view.members.len());
        let event = launched_event(&holdings, self.seed, launch.tier, crew_size, "member", paid);
        self.gate(account, Some(run.clone()), event).await;
        self.log_unloading();
        self.identity.runs.insert(
            account,
            Flying {
                run,
                expedition: launch.expedition.clone(),
                ship,
            },
        );
        let message = ServerMessage::Account {
            account: wire::account(&holdings),
        };
        write_frame(&mut self.send, &message).await?;
        write_frame(&mut self.send, &joined).await?;
        self.flying = Some((launch.expedition, ship));
        Ok(())
    }
}

impl Pilot<'_> {
    /// Records a gate event (plan 7.1) for this pilot. A failure is logged,
    /// never fatal: measurement must not stop play.
    async fn gate(&self, account: AccountId, run: Option<String>, event: GateEvent) {
        let recorded = Recorded {
            at: now_ms(),
            account: account.0,
            run,
            event,
        };
        if let Err(error) = self
            .identity
            .store
            .call(move |s| s.record(&[recorded]))
            .await
        {
            warn!(%error, "gate event not recorded");
        }
    }
}

/// What a launch records for the gates: the layout, unlocks and fitting the
/// pilot flies with, on which seed and tier, and what they stake.
fn launched_event(
    holdings: &crate::account::Account,
    seed: u32,
    tier: ThreatTier,
    crew: usize,
    role: &str,
    supplies: u32,
) -> GateEvent {
    let mut layout: Vec<Placed> = holdings
        .outpost
        .layout
        .iter()
        .map(|installed| Placed {
            socket: installed.socket.clone(),
            facility: installed.facility.clone(),
            run: installed.run,
        })
        .collect();
    layout.sort();
    let mut unlocks: Vec<String> = holdings
        .outpost
        .unlocks
        .iter()
        .map(|unlock| format!("{}:{}", unlock.facility, unlock.tier))
        .collect();
    unlocks.sort();
    let launch = home::launch(holdings);
    let mut fitted: Vec<String> = launch.flown.iter().map(|p| p.fitting.clone()).collect();
    fitted.sort();
    GateEvent::Launched {
        seed,
        tier: tier.number(),
        crew: u32::try_from(crew).unwrap_or(1),
        role: role.to_owned(),
        layout,
        unlocks,
        fitted,
        stake: launch.stake(home::discount(holdings)),
        supplies,
    }
}

/// What an outpost action did to `socket`, between `before` and `after`.
fn outpost_changed(
    before: &crate::account::Account,
    after: &crate::account::Account,
    socket: &str,
) -> GateEvent {
    let was = before.outpost.installed(socket);
    let now = after.outpost.installed(socket);
    let (action, facility, run) = match (was, now) {
        (None, Some(now)) if before.outpost.owned(&now.facility) == 0 => {
            ("bought", now.facility.clone(), now.run)
        }
        (None, Some(now)) => ("reinstalled", now.facility.clone(), now.run),
        (Some(was), None) => ("removed", was.facility.clone(), 0),
        (Some(was), Some(now))
            if after.outpost.owned(&now.facility) > before.outpost.owned(&was.facility) =>
        {
            ("upgraded", now.facility.clone(), now.run)
        }
        (_, Some(now)) => ("powered", now.facility.clone(), now.run),
        (None, None) => ("none", String::new(), 0),
    };
    GateEvent::OutpostChanged {
        action: action.to_owned(),
        socket: socket.to_owned(),
        facility,
        run,
    }
}

/// The rigs and modules a pilot's launch carries, by identifier.
fn fitted(holdings: &crate::account::Account) -> Vec<String> {
    home::launch(holdings)
        .flown
        .into_iter()
        .map(|piece| piece.fitting)
        .collect()
}

/// The home action a message asks for, if it is one.
fn home_action(message: &ClientMessage) -> Option<Action> {
    Some(match message.clone() {
        ClientMessage::Install { socket, facility } => Action::Install { socket, facility },
        ClientMessage::Upgrade { socket } => Action::Upgrade { socket },
        ClientMessage::Remove { socket } => Action::Remove { socket },
        ClientMessage::Power { socket, run } => Action::Power { socket, run },
        ClientMessage::Sell { good, amount } => Action::Sell {
            good: wire::from_good(good),
            amount,
        },
        ClientMessage::Buy { good, amount } => Action::Buy {
            good: wire::from_good(good),
            amount,
        },
        ClientMessage::Process { recipe, batches } => Action::Process { recipe, batches },
        ClientMessage::Unqueue { recipe, batches } => Action::Unqueue { recipe, batches },
        ClientMessage::BuyFitting { fitting } => Action::BuyFitting { fitting },
        ClientMessage::SaveFitting { rig, modules } => Action::SaveFitting { rig, modules },
        _ => return None,
    })
}

/// Resolves when a session is told to leave; never, for one not yet signed in.
async fn told_to_leave(leave: Option<Arc<Notify>>) {
    match leave {
        Some(leave) => leave.notified().await,
        None => std::future::pending().await,
    }
}

/// Reads one framed message from a stream.
///
/// # Errors
/// Fails if the stream ends, the frame is too large or the body is not a `T`.
pub async fn read_frame<T: serde::de::DeserializeOwned>(
    recv: &mut RecvStream,
) -> Result<T, SessionError> {
    let mut header = [0; frame::HEADER_BYTES];
    recv.read_exact(&mut header).await?;
    let mut body = vec![0; frame::body_length(header)?];
    recv.read_exact(&mut body).await?;
    Ok(frame::decode(&body)?)
}

/// Writes one framed message to a stream.
///
/// # Errors
/// Fails if the message cannot be encoded or the stream is closed.
pub async fn write_frame<T: serde::Serialize>(
    send: &mut SendStream,
    message: &T,
) -> Result<(), SessionError> {
    send.write_all(&frame::encode(message)?).await?;
    Ok(())
}
