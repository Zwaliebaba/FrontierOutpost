/**
 * The game client: takes the contract on offer, flies the player's ship through the run with the
 * keyboard, and draws every ship from the server's snapshots with the tactical camera (design §5).
 */
import { Vector3 } from "three";
import { connect, fetchConnectInfo } from "./net/connection";
import type { Connection } from "./net/connection";
import type { AccountInfo, OwnStatus, RunSummary, SnapshotEvent } from "./protocol";
import type { EntityPose } from "./net/interpolation";
import { Predictor } from "./game/prediction";
import { FrameTimer } from "./game/frameTimer";
import { Selection } from "./game/selection";
import { Shots } from "./game/shots";
import { Session } from "./game/session";
import type { Controls } from "./game/session";
import {
  HAZARD_TEXT,
  POCKET_NAME,
  POCKET_NEEDS,
  THREAT_TEXT,
  launch,
  refusalText,
  runLine,
  scanLine,
  summaryLines,
} from "./game/contract";
import type { Contract, Joined, Scanned } from "./game/contract";
import {
  SignInRefusedError,
  register,
  resume,
  signIn,
  storeToken,
  storedToken,
} from "./game/identity";
import type { SignedIn } from "./game/identity";
import { distanceText, placeMarkers } from "./game/navigation";
import type { NavTarget } from "./game/navigation";
import { ExpeditionView, RESTING_ELEVATION } from "./game/render";
import { loadSim } from "./sim";
import type { Sim } from "./sim";
import { headingRadians, positionMetres, velocityMetresPerSecond } from "./net/snapshot";
import { dragLook, isHome, settleLook, turnLook, zoomWidth } from "./view/look";
import type { LookState } from "./view/look";
import { pickNearest, worldToScreen } from "./view/picking";
import type { ScreenPoint } from "./view/picking";

const START_WIDTH = 40;
const PICK_RADIUS_PX = 24;
const KEY_TURN_RATE = 1.6;
/** Room kept clear for the HUD lines at the top and bottom of the screen. */
const NAV_INSETS = { top: 150, right: 12, bottom: 120, left: 12 };
const NOTICE_MS = 4_000;
/** How soon a dropped connection is retried, and how often an unreachable server. */
const RECONNECT_MS = 1_500;
const RETRY_MS = 3_000;
/** The reason the server closes a session with when the pilot signs in on another page. */
const SIGNED_IN_ELSEWHERE = "signed in elsewhere";
/** Within this distance of a deposit the run line names it, metres. */
const DEPOSIT_HINT = 25;

function element<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  id?: string,
  className?: string,
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (id !== undefined) node.id = id;
  if (className !== undefined) node.className = className;
  return node;
}

function line(id: string): HTMLParagraphElement {
  const node = element("p", id, "hud-line");
  document.body.append(node);
  return node;
}

// Status lines. `data-*` attributes are for tests.
const status = line("status");
const sim = line("sim");
const hud = line("hud");
const run = line("run");
const scan = line("scan");
const lock = line("lock");
const notice = line("notice");
const accountLine = line("account");
const nav = element("div", "nav");
const signInPanel = element("div", "signin", "panel");
const briefing = element("div", "briefing", "panel");
const summaryPanel = element("div", "summary", "panel");
document.body.append(nav, signInPanel, briefing, summaryPanel);

function showStatus(state: "connecting" | "connected" | "closed" | "offline", text: string) {
  status.dataset.state = state;
  status.textContent = text;
}

let noticeUntil = 0;
function showNotice(text: string): void {
  notice.textContent = text;
  noticeUntil = performance.now() + NOTICE_MS;
}

let simLine = "Simulation";
const rules = loadSim().then(
  (loaded) => {
    sim.dataset.state = "loaded";
    simLine = `Simulation · ${String(loaded.ticksPerSecond())} ticks per second`;
    sim.textContent = simLine;
    return loaded;
  },
  (error: unknown) => {
    sim.dataset.state = "failed";
    sim.textContent = `Simulation failed to load: ${String(error)}`;
    throw error;
  },
);

// --- Input -------------------------------------------------------------------------------------

const held = new Set<string>();
window.addEventListener("keydown", (event) => held.add(event.code));
window.addEventListener("keyup", (event) => held.delete(event.code));
window.addEventListener("blur", () => {
  held.clear();
});
const axis = (positive: string[], negative: string[]) =>
  Number(positive.some((key) => held.has(key))) - Number(negative.some((key) => held.has(key)));
/**
 * This tick's controls. `driveReady` is whether the cruise drive may engage, as the client
 * judges it: the server last said so and the predicted ship is outside the site. The server
 * judges again; asking only when it will agree keeps prediction close.
 */
const readControls = (driveReady: boolean): Controls => ({
  thrust: axis(["KeyW", "ArrowUp"], ["KeyS", "ArrowDown"]),
  turn: axis(["KeyD", "ArrowRight"], ["KeyA", "ArrowLeft"]),
  // Hold to fire at the locked target (the owner's choice for M2).
  fire: held.has("Space"),
  // Hold Shift for the cruise drive on the legs (the owner's choice after playing M3).
  cruise: driveReady && (held.has("ShiftLeft") || held.has("ShiftRight")),
});

// --- Game --------------------------------------------------------------------------------------

async function start(): Promise<void> {
  showStatus("connecting", "Connecting…");
  const connection = await connect(await fetchConnectInfo());
  showStatus("connected", `Connected · protocol ${String(connection.protocolVersion)}`);
  // Browsers do not always close a WebTransport session with its page; without this, the
  // server would keep the run going until the session timed out.
  let leaving = false;
  window.addEventListener("pagehide", () => {
    leaving = true;
    connection.close();
  });
  // Displaced by a sign-in on another page: the server says so, then closes.
  let displaced = false;
  connection.onMessage((message) => {
    if (message.type === "signInRefused" && callsign !== "") {
      displaced = true;
      connection.close();
    }
  });
  void connection.closed.then((reason) => {
    if (leaving) return;
    if (displaced || reason === SIGNED_IN_ELSEWHERE) {
      // Reconnecting would take the run back from the other page, and it back again.
      showStatus("closed", "Signed in on another page. Reload this one to play here.");
      return;
    }
    // The run waits for its pilot (design §6): the tab's session token signs straight back in.
    showStatus("closed", "Connection lost · reconnecting…");
    window.setTimeout(() => {
      window.location.reload();
    }, RECONNECT_MS);
  });
  const loaded = await rules;
  const view = new ExpeditionView(document.body, -1, START_WIDTH);
  play(connection, view, loaded);
  await identify(connection);
}

/** Shows what the pilot's home holds. */
function showAccount(callsign: string, account: AccountInfo): void {
  accountLine.dataset.credits = String(account.credits);
  accountLine.textContent =
    `${callsign} · ${String(account.credits)} cr · ${String(account.ore)} ore · ` +
    `${String(account.salvage)} salvage · ${String(account.volatiles)} volatiles`;
}

let callsign = "";

/**
 * Signs the pilot in: with the tab's session token if it still works, else through the sign-in
 * panel, where they register a new pilot or sign in with their passkey.
 */
async function identify(connection: Connection): Promise<void> {
  const token = storedToken();
  if (token !== null) {
    try {
      signedIn(await resume(connection, token));
      return;
    } catch {
      storeToken(null);
    }
  }
  await new Promise<void>((done) => {
    showSignIn(connection, done);
  });
}

function signedIn(message: SignedIn): void {
  storeToken(message.token);
  callsign = message.callsign;
  showAccount(callsign, message.account);
  delete signInPanel.dataset.state;
  signInPanel.replaceChildren();
}

function showSignIn(connection: Connection, done: () => void): void {
  signInPanel.replaceChildren();
  signInPanel.dataset.state = "open";
  const title = element("h2");
  title.textContent = "Frontier Outpost";
  const label = element("label");
  label.textContent = "Callsign ";
  const input = element("input", "callsign");
  input.autocomplete = "username webauthn";
  input.maxLength = 16;
  label.append(input);
  const note = element("p");
  note.textContent = "Sign in with your passkey, or make a new pilot.";
  const signInButton = element("button");
  signInButton.dataset.action = "sign-in";
  signInButton.textContent = "Sign in with passkey";
  const registerButton = element("button");
  registerButton.dataset.action = "register";
  registerButton.textContent = "New pilot";
  const attempt = (how: typeof signIn) => {
    const name = input.value.trim();
    if (!name) {
      note.textContent = "Enter your callsign first.";
      return;
    }
    signInButton.disabled = true;
    registerButton.disabled = true;
    how(connection, name)
      .then((message) => {
        signedIn(message);
        done();
      })
      .catch((error: unknown) => {
        note.textContent =
          error instanceof SignInRefusedError
            ? error.message
            : `Not signed in: ${error instanceof Error ? error.message : String(error)}`;
        signInButton.disabled = false;
        registerButton.disabled = false;
      });
  };
  signInButton.addEventListener("click", () => {
    attempt(signIn);
  });
  registerButton.addEventListener("click", () => {
    attempt(register);
  });
  signInPanel.append(title, label, note, signInButton, registerButton);
  input.focus();
}

/** One run's flight, from launch until the run is over. */
interface Life {
  joined: Joined;
  session: Session;
  predictor: Predictor;
  /** Snapshot events not yet drawn: they are drawn where each ship is drawn. */
  events: SnapshotEvent[];
  /** The player's own hull, 0 to 255, from the latest snapshot. */
  hull: number;
  own: OwnStatus | null;
  deposits: number[];
}

function beginLife(connection: Connection, joined: Joined, loaded: Sim): Life {
  // Your own ship is predicted with the server's rules; others are drawn from snapshots.
  const predictor = new Predictor((state, controls) => loaded.stepShip(state, controls));
  const life: Life = {
    joined,
    predictor,
    events: [],
    hull: 255,
    own: null,
    deposits: joined.deposits.map((deposit) => deposit.units),
    session: new Session(
      connection,
      joined.shipId,
      joined.tickRate,
      () => performance.now(),
      (snapshot) => {
        life.events.push(...snapshot.events);
        life.own = snapshot.own;
        life.deposits = snapshot.deposits;
        const own = snapshot.entities.find((entity) => entity.id === joined.shipId);
        if (!own) return;
        life.hull = own.hull;
        const { x, z } = positionMetres(own);
        const velocity = velocityMetresPerSecond(own);
        predictor.reconcile(
          { x, z, heading: headingRadians(own), speed: Math.hypot(velocity.x, velocity.z) },
          snapshot.inputAck,
        );
      },
    ),
  };
  hud.dataset.ship = String(joined.shipId);
  return life;
}

const KIND_NAME = { playerShip: "Pilot", drone: "Drone", raider: "Raider", warden: "Warden" };
const TIER_NAME = ["", "I", "II", "III"];

function play(connection: Connection, view: ExpeditionView, loaded: Sim): void {
  let life: Life | null = null;
  let contract: Contract | null = null;
  let reveal: Scanned | null = null;
  const shots = new Shots();

  const showBriefing = (offer: Contract) => {
    briefing.replaceChildren();
    briefing.dataset.state = "open";
    const title = element("h2");
    title.textContent = `Contract: ${offer.site.name}`;
    const facts = element("ul");
    for (const text of [HAZARD_TEXT[offer.hazard], THREAT_TEXT[offer.dominantThreat]]) {
      const item = element("li");
      item.textContent = text;
      facts.append(item);
    }
    for (const pocket of offer.site.pockets) {
      const item = element("li");
      const needs = POCKET_NEEDS[pocket.kind];
      item.textContent = pocket.locked
        ? `${POCKET_NAME[pocket.kind]}: locked, needs ${needs ?? "a facility"}`
        : `${POCKET_NAME[pocket.kind]}: open`;
      facts.append(item);
    }
    const note = element("p");
    note.textContent =
      "Fly out, extract, and dock with at least 10 units to collect the contract. " +
      "Supplies cost 40 cr a launch, or what you have if less. " +
      "Scanning in the site reveals the rest.";
    const tiers = element("div", undefined, "tiers");
    for (const tier of offer.tiers) {
      const button = element("button");
      button.dataset.tier = String(tier.tier);
      button.textContent = `Launch · tier ${TIER_NAME[tier.tier] ?? String(tier.tier)} · ${String(tier.payout)} cr`;
      button.disabled = !tier.open;
      if (!tier.open) button.title = "Opens with a tier 2 facility";
      button.addEventListener("click", () => {
        for (const other of tiers.querySelectorAll("button")) other.disabled = true;
        // The run begins when `joined` arrives (handled below, as for a rejoined run).
        launch(connection, tier.tier).catch((error: unknown) => {
          note.textContent = `Could not launch: ${error instanceof Error ? error.message : String(error)}`;
          for (const other of offer.tiers) {
            const b = tiers.querySelector<HTMLButtonElement>(`[data-tier="${String(other.tier)}"]`);
            if (b) b.disabled = !other.open;
          }
        });
      });
      tiers.append(button);
    }
    briefing.append(title, facts, note, tiers);
  };

  const showSummary = (summary: RunSummary) => {
    summaryPanel.replaceChildren();
    summaryPanel.dataset.ending = summary.ending;
    const lines = summaryLines(summary);
    const title = element("h2");
    title.textContent = lines[0] ?? "";
    summaryPanel.append(title);
    for (const text of lines.slice(1)) {
      const p = element("p");
      p.textContent = text;
      summaryPanel.append(p);
    }
    const next = element("button");
    next.textContent = "Next contract";
    next.addEventListener("click", () => {
      delete summaryPanel.dataset.ending;
      if (contract) showBriefing(contract);
    });
    summaryPanel.append(next);
  };

  const beginRun = (joined: Joined) => {
    loaded.setObstacles(joined.obstacles);
    view.beginRun(joined.shipId, joined.site, joined.obstacles, joined.deposits);
    life?.session.retire();
    life = beginLife(connection, joined, loaded);
    reveal = null;
    selection.release();
    delete briefing.dataset.state;
    briefing.replaceChildren();
  };

  connection.onMessage((message) => {
    switch (message.type) {
      case "joined":
        // A launch, or a run rejoined after the connection dropped or the server restarted.
        delete summaryPanel.dataset.ending;
        beginRun(message);
        break;
      case "contract":
        contract = message;
        // After a run the summary comes first; its button opens the new contract.
        if (summaryPanel.dataset.ending === undefined) showBriefing(message);
        break;
      case "runOver":
        life?.session.retire();
        life = null;
        selection.release();
        showSummary(message.summary);
        break;
      case "scanned":
        reveal = message;
        break;
      case "account":
        showAccount(callsign, message.account);
        break;
      case "extractRefused":
        showNotice(refusalText(message.reason));
        break;
      case "limitWarning":
        showNotice(`The instance ends in ${String(Math.round(message.secondsLeft / 60))} minutes`);
        break;
      default:
        break;
    }
  });

  const home: LookState = { yaw: 0, pitch: RESTING_ELEVATION };
  let look: LookState = { ...home };
  let viewWidth = START_WIDTH;
  let dragFrom: ScreenPoint | null = null;
  let poses: EntityPose[] = [];
  const selection = new Selection((target) => {
    void connection.send({ type: "lock", target }).catch(() => undefined);
  });
  const send = (message: Parameters<Connection["send"]>[0]) => {
    void connection.send(message).catch(() => undefined);
  };
  const lockNearest = (own: EntityPose, shipId: number) => {
    if (!life) return;
    const range = life.joined.lockRange;
    const others = poses.filter((pose) => pose.id !== shipId);
    if (!selection.cycle(own, others, range)) {
      showNotice(`No hostile within lock range (${range.toFixed(0)} m)`);
    }
  };
  window.addEventListener("keydown", (event) => {
    if (!life) return;
    const shipId = life.session.shipId;
    if (event.code === "Tab") {
      event.preventDefault();
      const own = poses.find((pose) => pose.id === shipId);
      if (!own) return;
      // Only what is within lock range can be locked (design §10: ion fog shortens it).
      lockNearest(own, shipId);
    } else if (event.code === "Escape") {
      selection.release();
    } else if (event.repeat) {
      return;
    } else if (event.code === "Space" && selection.lock === null) {
      // The trigger with nothing locked takes the nearest hostile it can.
      const own = poses.find((pose) => pose.id === shipId);
      if (own) lockNearest(own, shipId);
    } else if (event.code === "KeyX") {
      const working = life.own?.extractor === "on" || life.own?.extractor === "venting";
      send({ type: "extract", on: !working });
    } else if (event.code === "KeyV") {
      send({ type: "vent" });
    } else if (event.code === "KeyC") {
      send({ type: "scan" });
    }
  });
  const target = new Vector3();

  const resize = () => {
    view.resize(window.innerWidth, window.innerHeight, viewWidth);
  };
  window.addEventListener("resize", resize);
  resize();

  const canvas = view.canvas;
  canvas.addEventListener("contextmenu", (event) => {
    event.preventDefault();
  });
  canvas.addEventListener("pointerdown", (event) => {
    if (event.button === 2) {
      dragFrom = { x: event.clientX, y: event.clientY };
      canvas.setPointerCapture(event.pointerId);
      return;
    }
    if (event.button !== 0 || !life) return;
    // Clicking a hostile locks it and centres the view; an ally is only centred on; empty space
    // returns the view to your own ship.
    const shipId = life.session.shipId;
    const others = poses.filter((pose) => pose.id !== shipId);
    const candidates = others.map((pose) => ({
      id: String(pose.id),
      screen: worldToScreen(new Vector3(pose.x, 0, pose.z), view.camera, view.viewport),
    }));
    const picked = pickNearest({ x: event.clientX, y: event.clientY }, candidates, PICK_RADIUS_PX);
    const own = poses.find((pose) => pose.id === shipId);
    const range = life.joined.lockRange;
    const result = selection.click(
      others.find((pose) => String(pose.id) === picked) ?? null,
      own,
      range,
    );
    if (result === "beyond") {
      showNotice(`Beyond lock range (${range.toFixed(0)} m): centred on it, not locked`);
    }
  });
  canvas.addEventListener("pointermove", (event) => {
    if (!dragFrom) return;
    look = dragLook(look, event.clientX - dragFrom.x, event.clientY - dragFrom.y);
    dragFrom = { x: event.clientX, y: event.clientY };
  });
  const endDrag = () => {
    dragFrom = null;
  };
  canvas.addEventListener("pointerup", endDrag);
  canvas.addEventListener("pointercancel", endDrag);
  canvas.addEventListener(
    "wheel",
    (event) => {
      event.preventDefault();
      viewWidth = zoomWidth(viewWidth, event.deltaY, event.deltaMode);
      resize();
    },
    { passive: false },
  );

  const markers = new Map<string, HTMLDivElement>();
  const drawNav = (from: { x: number; z: number }) => {
    const site = life?.joined.site;
    if (!site) {
      nav.replaceChildren();
      markers.clear();
      return;
    }
    const targets: NavTarget[] = [
      { id: "dock", label: "Dock", x: site.dockX, z: site.dockZ },
      ...site.pockets.map((pocket, index) => ({
        id: pocket.kind,
        label:
          POCKET_NAME[pocket.kind] +
          (pocket.locked ? " (locked)" : "") +
          (reveal?.richPocket === index ? " (rich)" : ""),
        x: pocket.x,
        z: pocket.z,
      })),
    ];
    const project = (x: number, z: number) =>
      worldToScreen(new Vector3(x, 0, z), view.camera, view.viewport);
    for (const marker of placeMarkers(targets, from, project, view.viewport, NAV_INSETS)) {
      let node = markers.get(marker.id);
      if (!node) {
        node = element("div", undefined, "nav-marker");
        node.dataset.target = marker.id;
        nav.append(node);
        markers.set(marker.id, node);
      }
      node.dataset.onScreen = String(marker.onScreen);
      const arrow = element("span", undefined, "nav-arrow");
      arrow.textContent = "➤";
      arrow.style.transform = `rotate(${marker.angle.toFixed(2)}rad)`;
      const text = `${marker.label} · ${distanceText(marker.distance)}`;
      node.replaceChildren(...(marker.onScreen ? [text] : [arrow, " ", text]));
      // Above an on-screen target; inward from an edge, so the label stays whole.
      const lift = marker.onScreen ? 12 : 0;
      node.style.transform =
        `translate(${marker.screen.x.toFixed(0)}px, ${(marker.screen.y - lift).toFixed(0)}px) ` +
        `translate(${(-marker.anchor.x * 100).toFixed(0)}%, ${(-marker.anchor.y * 100).toFixed(0)}%)`;
    }
  };

  let last = performance.now();
  let untilTick = 0;
  const frames = new FrameTimer();
  const frame = (now: number) => {
    const workStart = performance.now();
    const dt = Math.min(0.1, (now - last) / 1_000);
    last = now;
    const current = life;

    // Controls go out once per simulation tick, as the server steps.
    const tickMs = 1_000 / (current?.session.tickRate ?? 20);
    untilTick -= dt * 1_000;
    if (untilTick <= 0) {
      untilTick += tickMs;
      if (untilTick < 0) untilTick = tickMs;
      if (current) {
        const site = current.joined.site;
        const at = current.predictor.state;
        const outside =
          at !== null && Math.hypot(at.x - site.centreX, at.z - site.centreZ) > site.radius;
        const controls = readControls(current.own?.drive === "ready" && outside);
        const { sequence, sent } = current.session.sendControls(controls);
        current.predictor.advance(sequence, sent);
        // Steering takes the view back to your own ship, so it is never flown off screen.
        if (controls.thrust !== 0 || controls.turn !== 0) selection.steer();
      }
    }

    // In flight the view springs back once the look control is released (design §5).
    if (held.has("KeyR")) {
      look = { ...home };
      viewWidth = START_WIDTH;
      selection.steer();
      resize();
    }
    const keyTurn = axis(["KeyE"], ["KeyQ"]);
    if (keyTurn !== 0) look = turnLook(look, keyTurn * KEY_TURN_RATE * dt);
    else if (!dragFrom && !isHome(look, home)) look = settleLook(look, home, dt);

    let own: EntityPose | undefined;
    if (current) {
      const { session, predictor } = current;
      const others = session.snapshots.sample(now).filter((pose) => pose.id !== session.shipId);
      const predicted = predictor.visual(1 - Math.max(0, untilTick) / tickMs, dt);
      own = predicted
        ? { id: session.shipId, kind: "playerShip", hull: current.hull, ...predicted }
        : undefined;
      poses = own ? [own, ...others] : others;
      const drawnAt = (id: number) => poses.find((pose) => pose.id === id);
      for (const event of current.events.splice(0)) {
        if (event.type === "fired") {
          const from = drawnAt(event.shooter);
          if (from) shots.fire(event.shooter, event.target, from, event.speed);
        } else {
          const at = drawnAt(event.ship);
          if (at) shots.blast(at);
        }
      }
      shots.advance(dt, drawnAt);
      selection.prune(new Set(others.map((pose) => pose.id)));
      view.world.setDeposits(current.deposits);
    } else {
      poses = [];
    }
    const focus = poses.find((pose) => pose.id === selection.focus);
    const centre = focus ?? own;
    if (centre) target.set(centre.x, 0, centre.z);
    view.render(poses, selection.lock, target, look.pitch, look.yaw, shots);

    const locked = poses.find((pose) => pose.id === selection.lock);
    if (locked && own && current) {
      const distance = Math.hypot(locked.x - own.x, locked.z - own.z);
      lock.dataset.target = String(locked.id);
      lock.textContent =
        `Locked: ${KIND_NAME[locked.kind]} ${String(locked.id)} · ${distance.toFixed(0)} m ` +
        `${distance <= current.joined.weaponRange ? "in range" : `out of range, fires within ${current.joined.weaponRange.toFixed(0)} m`} · ` +
        `hull ${String(Math.round((locked.hull / 255) * 100))}% · Tab next, Esc release`;
    } else {
      delete lock.dataset.target;
      lock.textContent = "";
    }

    if (own && current) {
      const { session, predictor } = current;
      hud.dataset.ships = poses.map((pose) => pose.id).join(" ");
      if (session.roundTripMs !== null) hud.dataset.roundTrip = session.roundTripMs.toFixed(0);
      hud.dataset.x = own.x.toFixed(2);
      hud.dataset.z = own.z.toFixed(2);
      hud.textContent =
        `Ship ${String(session.shipId)} · hull ${String(Math.round((current.hull / 255) * 100))}% · ` +
        `round trip ${session.roundTripMs === null ? "–" : `${session.roundTripMs.toFixed(0)} ms`} · ` +
        `correction ${(predictor.lastCorrection * 100).toFixed(0)} cm · ` +
        `W A S D fly · Shift cruise · Tab lock · Space fire · X extract · V vent · C scan`;
      const status = current.own;
      if (status) {
        const nearest = current.joined.deposits
          .map((deposit, index) => ({
            distance: Math.hypot(deposit.x - own.x, deposit.z - own.z),
            left: current.deposits[index] ?? deposit.units,
          }))
          .filter((deposit) => deposit.left > 0)
          .sort((a, b) => a.distance - b.distance)[0];
        const hint =
          nearest && nearest.distance <= DEPOSIT_HINT
            ? ` · deposit ${nearest.distance.toFixed(0)} m, ${String(nearest.left)} left` +
              (nearest.distance <= current.joined.extractRange ? " (in reach)" : "")
            : "";
        run.dataset.phase = status.phase;
        run.dataset.cargo = String(status.cargo);
        run.dataset.extractor = status.extractor;
        run.textContent = runLine(status) + hint;
        const pocketName = (index: number) => {
          const pocket = current.joined.site.pockets[index];
          return pocket ? POCKET_NAME[pocket.kind] : "?";
        };
        scan.dataset.revealed = String(reveal !== null);
        scan.textContent = scanLine(status, reveal, pocketName);
      }
      drawNav(own);
    } else {
      delete run.dataset.phase;
      delete hud.dataset.x;
      hud.textContent = "";
      run.textContent = "";
      scan.textContent = "";
      drawNav({ x: 0, z: 0 });
    }
    if (noticeUntil && now > noticeUntil) {
      notice.textContent = "";
      noticeUntil = 0;
    }

    const cost = frames.record(workStart, performance.now());
    if (cost) {
      sim.dataset.frameMs = cost.meanMs.toFixed(2);
      sim.dataset.worstFrameMs = cost.worstMs.toFixed(2);
      sim.dataset.fps = cost.fps.toFixed(0);
      sim.textContent =
        `${simLine} · frame ${cost.meanMs.toFixed(1)} ms, ` +
        `worst ${cost.worstMs.toFixed(1)} ms · ${cost.fps.toFixed(0)} fps`;
    }
    requestAnimationFrame(frame);
  };
  requestAnimationFrame(frame);
}

start().catch((error: unknown) => {
  const why = error instanceof Error ? error.message : String(error);
  showStatus("offline", `Offline: ${why} · retrying`);
  window.setTimeout(() => {
    window.location.reload();
  }, RETRY_MS);
});
