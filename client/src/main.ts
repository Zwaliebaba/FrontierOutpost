/**
 * The game client: takes the contract on offer, flies the player's ship through the run with the
 * keyboard, and draws every ship from the server's snapshots with the tactical camera (design §5).
 */
import "./command.css";
import { Raycaster, Vector2, Vector3 } from "three";
import { connect, fetchConnectInfo } from "./net/connection";
import type { Connection } from "./net/connection";
import type {
  AccountInfo,
  Catalog,
  CrewInfo,
  OwnStatus,
  RunSummary,
  SnapshotEvent,
} from "./protocol";
import { launchRole, renderCrew } from "./outpost/crew";
import { loadOutpostModels } from "./outpost/models";
import { fittingName, launchLines } from "./outpost/options";
import { BERTHS, placeStation } from "./outpost/placement";
import { OutpostScene } from "./outpost/scene";
import type { HomeTab } from "./outpost/ui";
import { renderHome } from "./outpost/ui";
import type { EntityPose } from "./net/interpolation";
import { Predictor } from "./game/prediction";
import { FrameTimer } from "./game/frameTimer";
import { onHidden } from "./game/hidden";
import { PendingEvents } from "./game/pending";
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
  tokenRefused,
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
import { useShipModel } from "./view/readability";
import { dragLook, glideEase, isHome, settleLook, turnLook, zoomWidth } from "./view/look";
import type { LookState } from "./view/look";
import { pickNearest, worldToScreen } from "./view/picking";
import { Written } from "./view/written";
import type { ScreenPoint } from "./view/picking";

const START_WIDTH = 40;
/** How long the camera takes between the station and the ship, seconds. */
const GLIDE_SECONDS = 0.9;

const PICK_RADIUS_PX = 24;
const KEY_TURN_RATE = 1.6;
/** Room kept clear for the HUD lines at the top and bottom of the screen. */
const NAV_INSETS = { top: 150, right: 12, bottom: 120, left: 12 };
const NOTICE_MS = 4_000;
/** How soon a dropped connection is retried, and how often an unreachable server. */
const RECONNECT_MS = 1_500;
/** How often the HUD's top line is rewritten, at most. */
const HUD_LINE_MS = 200;
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
const command = element("header", "command");
const brand = element("div", undefined, "brand");
brand.innerHTML =
  '<span class="insignia" aria-hidden="true">⌃</span><strong>FRONTIER / OUTPOST</strong><span class="command-title">STATION COMMAND</span>';
command.append(brand, status);
const utility = element("footer", "utility");
utility.textContent = "HOME OUTPOST   /   BERTHS 07–10";
const cameraHints = element("span");
cameraHints.textContent = "SCROLL  Zoom   /   RMB  Orbit   /   R  Reset";
utility.append(cameraHints, sim);
document.body.append(command, utility);
const stationLabels = element("div", "station-labels");
document.body.append(stationLabels);
const nav = element("div", "nav");
const signInPanel = element("div", "signin", "panel");
const briefing = element("div", "briefing", "panel");
const summaryPanel = element("div", "summary", "panel");
const homePanel = element("div", "home", "panel");
document.body.append(nav, signInPanel, homePanel, briefing, summaryPanel);

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
  showStatus("connected", "Connected");
  status.dataset.protocol = String(connection.protocolVersion);
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
  // The outpost's models load beside everything else; the home shows once they are in.
  loadOutpostModels()
    .then((models) => {
      // Player ships fly as the starter frigate from here on, those in flight included.
      useShipModel(models.frigate);
      view.world.redraw();
      outpost = new OutpostScene(models);
      homeChanged();
    })
    .catch((error: unknown) => {
      showNotice(`The outpost models did not load: ${String(error)}`);
    });
  play(connection, view, loaded);
  await identify(connection);
}

/** What the pilot's home holds, the outpost's rules, and the outpost as drawn. */
let holdings: AccountInfo | null = null;
let catalog: Catalog | null = null;
let outpost: OutpostScene | null = null;
let selectedSocket = "a";
let homeTab: HomeTab = "overview";
/** The pilot's crew, if they are in one. */
let crew: CrewInfo | null = null;
/** Redraws what depends on the home: set by `play`. */
let homeChanged = (): void => undefined;

/** Shows what the pilot's home holds. */
function showAccount(callsign: string, account: AccountInfo): void {
  holdings = account;
  accountLine.dataset.credits = String(account.credits);
  accountLine.dataset.ore = String(account.ore);
  accountLine.dataset.alloy = String(account.alloy);
  accountLine.replaceChildren();
  const pilot = element("span", undefined, "pilot-name");
  pilot.textContent = callsign;
  accountLine.append(pilot);
  for (const [label, value] of [
    ["Credits", `${String(account.credits)} cr`],
    ["Ore", account.ore],
    ["Salvage", account.salvage],
    ["Volatiles", account.volatiles],
    ["Alloy", account.alloy],
    ["Components", account.components],
  ] as const) {
    const cell = element("span", undefined, "resource");
    const name = element("span");
    name.textContent = label;
    const amount = element("strong");
    amount.textContent = String(value);
    cell.append(name, amount);
    accountLine.append(cell);
  }
  homeChanged();
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
    } catch (error) {
      // Only the server's refusal makes the token worthless. A connection that drops mid-way
      // keeps it, and the reconnect signs straight back in with it.
      if (!tokenRefused(error)) throw error;
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
  events: PendingEvents<SnapshotEvent>;
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
    events: new PendingEvents(),
    hull: 255,
    own: null,
    deposits: joined.deposits.map((deposit) => deposit.units),
    session: new Session(
      connection,
      joined.shipId,
      joined.tickRate,
      () => performance.now(),
      (snapshot) => {
        life.events.push(snapshot.tick, snapshot.events);
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

const KIND_NAME = {
  playerShip: "Pilot",
  drone: "Drone",
  raider: "Raider",
  warden: "Warden",
  core: "Nest core",
};
const TIER_NAME = ["", "I", "II", "III"];

function play(connection: Connection, view: ExpeditionView, loaded: Sim): void {
  let life: Life | null = null;
  let selectedTier = 1;
  let contract: Contract | null = null;
  let reveal: Scanned | null = null;
  /**
   * The camera moving between the station and the ship, when a run begins or ends. It starts
   * `from` this far from the station's centre, so it holds if the next dock moves the station.
   */
  let glide: { from: Vector3; toStation: boolean; t: number } | null = null;
  const glided = new Vector3();
  const shots = new Shots();

  const showBriefing = (offer: Contract) => {
    briefing.replaceChildren();
    briefing.dataset.state = "open";
    const title = element("h2");
    const eyebrow = element("p", undefined, "eyebrow");
    eyebrow.textContent = "EXPEDITION / CONTRACT BRIEFING";
    title.textContent = offer.site.name;
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
      "Scanning in the site reveals the rest.";
    const stake = element("div", undefined, "stake");
    if (catalog && holdings) {
      const known = catalog;
      for (const text of launchLines(known, holdings)) {
        const p = element("p");
        p.textContent = text;
        stake.append(p);
      }
      for (const missing of holdings.launch.missing) {
        if (missing.price === undefined) continue;
        const buy = element("button");
        buy.dataset.action = `rebuy:${missing.fitting}`;
        buy.textContent = `Buy ${fittingName(known, missing.fitting)} · ${String(missing.price)} cr`;
        buy.disabled = holdings.credits < missing.price;
        buy.addEventListener("click", () => {
          send({ type: "buyFitting", fitting: missing.fitting });
        });
        stake.append(buy);
      }
      stake.dataset.stake = String(holdings.launch.stake);
    }
    const crewSection = element("div");
    renderCrew(crewSection, crew, send);
    const role = launchRole(crew);
    if (role.note) {
      const p = element("p");
      p.className = "crew-note";
      p.textContent = role.note;
      crewSection.append(p);
    }
    const tiers = element("div", undefined, "tiers");
    selectedTier =
      role.follow ??
      (offer.tiers.some((tier) => tier.tier === selectedTier && tier.open)
        ? selectedTier
        : (offer.tiers.find((tier) => tier.open)?.tier ?? 1));
    const tierLabel = element("label");
    tierLabel.textContent = "THREAT TIER";
    const tierSelect = element("select", "threat-tier");
    tierSelect.disabled = !role.launches;
    for (const tier of offer.tiers) {
      const option = element("option");
      option.value = String(tier.tier);
      option.textContent = `Tier ${TIER_NAME[tier.tier] ?? String(tier.tier)} / ${String(tier.payout)} cr${tier.open ? "" : " / locked"}`;
      option.disabled = !tier.open && role.follow !== tier.tier;
      option.selected = tier.tier === selectedTier;
      tierSelect.append(option);
    }
    tierSelect.addEventListener("change", () => {
      selectedTier = Number(tierSelect.value);
      for (const button of tiers.querySelectorAll<HTMLButtonElement>("button[data-tier]")) {
        button.hidden = Number(button.dataset.tier) !== selectedTier;
      }
    });
    tierLabel.append(tierSelect);
    tiers.append(tierLabel);
    for (const tier of offer.tiers) {
      const button = element("button");
      button.dataset.tier = String(tier.tier);
      button.hidden = tier.tier !== selectedTier;
      const following = role.follow === tier.tier;
      button.textContent = following
        ? `Follow the host · tier ${TIER_NAME[tier.tier] ?? String(tier.tier)}`
        : `Launch · tier ${TIER_NAME[tier.tier] ?? String(tier.tier)} · ${String(tier.payout)} cr`;
      if (following) button.dataset.action = "follow";
      button.disabled = role.launches ? !tier.open : !following;
      if (!tier.open) button.title = "Opens with a tier 2 facility";
      else if (button.disabled && role.note) button.title = role.note;
      button.addEventListener("click", () => {
        for (const other of tiers.querySelectorAll("button")) other.disabled = true;
        // The run begins when `joined` arrives (handled below, as for a rejoined run).
        launch(connection, tier.tier).catch((error: unknown) => {
          note.textContent = `Could not launch: ${error instanceof Error ? error.message : String(error)}`;
          for (const other of offer.tiers) {
            const b = tiers.querySelector<HTMLButtonElement>(`[data-tier="${String(other.tier)}"]`);
            if (b) b.disabled = role.launches ? !other.open : role.follow !== other.tier;
          }
        });
      });
      tiers.append(button);
    }
    const content = element("div", undefined, "briefing-content");
    content.append(eyebrow, title, facts, note, stake, crewSection);
    briefing.append(content, tiers);
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

  // The station is one object (design §5: one camera, one plane): stood where the contract's dock
  // is, it is the outpost view at home and the dock in the run, and launching only moves the
  // camera from the station to the ship.
  const placeFor = (site: Joined["site"]) => {
    outpost?.place(
      placeStation({ x: site.dockX, z: site.dockZ }, { x: site.centreX, z: site.centreZ }),
    );
  };
  const stationWhere = () => {
    if (!outpost) return;
    if (life) {
      view.world.setStation(outpost.launch());
    } else {
      view.world.setStation(null);
      outpost.home();
    }
    document.body.dataset.station = life ? "dock" : "home";
    if (utility.firstChild)
      utility.firstChild.textContent = life
        ? "EXPEDITION / ACTIVE"
        : `HOME OUTPOST / ${String(crew?.members.length ?? 1)} OF 4 BERTHS OCCUPIED`;
    resize();
  };

  const beginRun = (joined: Joined) => {
    // Launching from the outpost view glides to the ship; a rejoined run starts on it.
    if (!life && outpost && homePanel.dataset.state === "open") {
      glide = { from: new Vector3(), toStation: false, t: 0 };
    }
    placeFor(joined.site);
    // Start the camera at this pilot's authored berth until the first snapshot arrives.
    const slot = crew?.members.findIndex((member) => member.you) ?? 0;
    const lateral = BERTHS[crew?.members[slot]?.berth ?? slot]?.x ?? BERTHS[0]?.x ?? 0;
    const heading = Math.atan2(
      joined.site.centreZ - joined.site.dockZ,
      joined.site.centreX - joined.site.dockX,
    );
    target.set(
      joined.site.dockX + Math.sin(heading) * lateral,
      0,
      joined.site.dockZ - Math.cos(heading) * lateral,
    );
    loaded.setObstacles(joined.obstacles);
    loaded.setSpeedFactor(joined.speedFactor);
    view.beginRun(joined.shipId, joined.site, joined.obstacles, joined.deposits);
    life?.session.retire();
    life = beginLife(connection, joined, loaded);
    reveal = null;
    selection.release();
    delete briefing.dataset.state;
    briefing.replaceChildren();
    delete homePanel.dataset.state;
    stationWhere();
  };

  // The home: the outpost drawn, the panel of what can be done there, and the stake on the
  // contract. Redrawn whenever the account or the catalog changes.
  homeChanged = () => {
    stationWhere();
    if (!catalog || !holdings) return;
    let power = document.getElementById("resource-power");
    if (!power) {
      power = element("span", "resource-power", "resource");
      accountLine.append(power);
    }
    power.replaceChildren();
    const powerLabel = element("span");
    powerLabel.textContent = "Power";
    const powerValue = element("strong");
    powerValue.textContent = `${String(holdings.outpost.powerUsed)} / ${String(catalog.powerCap)}`;
    power.append(powerLabel, powerValue);
    if (outpost) {
      outpost.setCrew(crew, callsign);
      outpost.apply(holdings);
      outpost.select(selectedSocket);
      homePanel.dataset.scene = JSON.stringify(outpost.report());
    }
    renderHome(
      homePanel,
      catalog,
      holdings,
      selectedSocket,
      {
        send,
        tab: (tab) => {
          homeTab = tab;
          homeChanged();
        },
        select: (socket) => {
          selectedSocket = socket;
          homeTab = "facilities";
          homeChanged();
        },
      },
      homeTab,
    );
    if (!life) homePanel.dataset.state = "open";
    if (briefing.dataset.state === "open" && contract) showBriefing(contract);
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
        if (!life) placeFor(message.site);
        // After a run the summary comes first; its button opens the new contract.
        if (summaryPanel.dataset.ending === undefined) showBriefing(message);
        break;
      case "runOver":
        if (outpost) glide = { from: target.clone().sub(outpost.centre), toStation: true, t: 0 };
        life?.session.retire();
        life = null;
        selection.release();
        showSummary(message.summary);
        homeChanged();
        break;
      case "scanned":
        reveal = message;
        break;
      case "account":
        showAccount(callsign, message.account);
        break;
      case "catalog":
        catalog = message.catalog;
        homeChanged();
        break;
      case "homeRefused":
      case "crewRefused":
        showNotice(message.reason);
        break;
      case "crew":
        crew = message.crew ?? null;
        homeChanged();
        break;
      case "potPaid": {
        const { pot, share } = message;
        showNotice(
          `The crew's pot is paid: ${String(pot.contract + pot.bounty + pot.relay)} cr ` +
            `(contract ${String(pot.contract)}, bounty ${String(pot.bounty)}, relays ` +
            `${String(pot.relay)}). Your share: ${String(share)} cr`,
        );
        break;
      }
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
    const width = window.innerWidth;
    const panels = life ? 0 : width <= 1100 ? 490 : 580;
    const available = Math.max(width * 0.35, width - panels);
    view.resize(width, window.innerHeight, (viewWidth * width * (life ? 1 : 0.8)) / available);
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
    if (event.button === 0 && !life && outpost) {
      // At home a click selects the socket under the pointer.
      const { width, height } = view.viewport;
      const ray = new Raycaster();
      ray.setFromCamera(
        new Vector2((event.clientX / width) * 2 - 1, -(event.clientY / height) * 2 + 1),
        view.camera,
      );
      const socket = outpost.pick(ray);
      if (socket) {
        selectedSocket = socket;
        homeTab = "facilities";
        homeChanged();
      }
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

  // The HUD is redrawn every frame, and written to the page only where it changed.
  const written = new Written();
  const setText = (node: HTMLElement, text: string) => {
    if (written.changed(`${node.id}.text`, text)) node.textContent = text;
  };
  const setData = (node: HTMLElement, name: string, value: string | null) => {
    if (!written.changed(`${node.id}.data.${name}`, value ?? "\u0000")) return;
    if (value === null) Reflect.deleteProperty(node.dataset, name);
    else node.dataset[name] = value;
  };

  interface Marker {
    node: HTMLDivElement;
    arrow: HTMLSpanElement;
    label: Text;
  }
  const markers = new Map<string, Marker>();
  const drawNav = (from: { x: number; z: number }) => {
    const site = life?.joined.site;
    if (!site) {
      if (markers.size > 0) {
        nav.replaceChildren();
        markers.clear();
        written.forget("nav.");
      }
      return;
    }
    const targets: NavTarget[] = [
      { id: "dock", label: "Dock", x: site.dockX, z: site.dockZ },
      ...site.pockets.map((pocket, index) => ({
        id: pocket.kind,
        label:
          POCKET_NAME[pocket.kind] +
          (pocket.locked ? " (locked)" : "") +
          (reveal?.richPocket === index ? " (rich)" : "") +
          (reveal?.relays.includes(index) ? " (relay)" : ""),
        x: pocket.x,
        z: pocket.z,
      })),
    ];
    const project = (x: number, z: number) =>
      worldToScreen(new Vector3(x, 0, z), view.camera, view.viewport);
    for (const marker of placeMarkers(targets, from, project, view.viewport, NAV_INSETS)) {
      let drawn = markers.get(marker.id);
      if (!drawn) {
        const node = element("div", undefined, "nav-marker");
        node.dataset.target = marker.id;
        const arrow = element("span", undefined, "nav-arrow");
        arrow.textContent = "➤";
        nav.append(node);
        drawn = { node, arrow, label: document.createTextNode("") };
        markers.set(marker.id, drawn);
      }
      const { node, arrow, label } = drawn;
      const place = `nav.${marker.id}`;
      // The arrow points the way to a target off screen; an on-screen one needs none.
      if (written.changed(`${place}.onScreen`, String(marker.onScreen))) {
        node.dataset.onScreen = String(marker.onScreen);
        node.replaceChildren(...(marker.onScreen ? [label] : [arrow, " ", label]));
      }
      const turned = `rotate(${marker.angle.toFixed(2)}rad)`;
      if (written.changed(`${place}.arrow`, turned)) arrow.style.transform = turned;
      const text = `${marker.label} · ${distanceText(marker.distance)}`;
      if (written.changed(`${place}.text`, text)) label.data = text;
      // Above an on-screen target; inward from an edge, so the label stays whole.
      const lift = marker.onScreen ? 12 : 0;
      const moved =
        `translate(${marker.screen.x.toFixed(0)}px, ${(marker.screen.y - lift).toFixed(0)}px) ` +
        `translate(${(-marker.anchor.x * 100).toFixed(0)}%, ${(-marker.anchor.y * 100).toFixed(0)}%)`;
      if (written.changed(`${place}.at`, moved)) node.style.transform = moved;
    }
  };

  const homeMarkers = new Map<string, HTMLElement>();
  const drawHomeLabels = () => {
    stationLabels.hidden = life !== null || !outpost || !holdings;
    if (stationLabels.hidden || !outpost) return;
    const station = outpost;
    const roster = crew?.members ?? [{ callsign, you: true, berth: 0 }];
    const targets = [
      ...BERTHS.map((b, i) => ({
        id: `berth-${b.id}`,
        label: `${b.id} / ${roster.find((member, index) => (member.berth ?? index) === i)?.callsign ?? "VACANT"}`,
        at: station.root.localToWorld(new Vector3(b.x, b.deck + 0.2, b.z + 1.8)),
      })),
      ...["a", "b"].map((id) => ({
        id: `socket-${id}`,
        label: `SOCKET ${id.toUpperCase()}`,
        at: station.socketPosition(id)?.add(new Vector3(0, 1.7, 0)),
      })),
    ];
    for (const marker of targets) {
      if (!marker.at) continue;
      let node = homeMarkers.get(marker.id);
      if (!node) {
        node = element("span", undefined, "station-label");
        node.dataset.marker = marker.id;
        homeMarkers.set(marker.id, node);
        stationLabels.append(node);
      }
      const screen = worldToScreen(marker.at, view.camera, view.viewport);
      const left = window.innerWidth <= 1100 ? 240 : 286;
      const right = window.innerWidth - (window.innerWidth <= 1100 ? 270 : 314);
      node.hidden =
        screen.x < left || screen.x > right || screen.y < 126 || screen.y > window.innerHeight - 70;
      if (written.changed(`home.${marker.id}.text`, marker.label)) node.textContent = marker.label;
      const transform = `translate(${screen.x.toFixed(0)}px, ${screen.y.toFixed(0)}px) translate(-50%, -100%)`;
      if (written.changed(`home.${marker.id}.at`, transform)) node.style.transform = transform;
    }
  };

  let last = performance.now();
  // A hidden tab draws no frames, so no more controls would go out: let go of them now.
  onHidden(document, () => {
    held.clear();
    if (!life) return;
    const { sequence, sent } = life.session.sendControls(readControls(false));
    life.predictor.advance(sequence, sent);
  });

  let lastControlAt = performance.now();
  const controlTimer = window.setInterval(() => {
    const current = life;
    if (!current || document.hidden) return;
    const site = current.joined.site;
    const at = current.predictor.state;
    const outside =
      at !== null && Math.hypot(at.x - site.centreX, at.z - site.centreZ) > site.radius;
    const controls = readControls(current.own?.drive === "ready" && outside);
    const { sequence, sent } = current.session.sendControls(controls);
    current.predictor.advance(sequence, sent);
    lastControlAt = performance.now();
    if (controls.thrust !== 0 || controls.turn !== 0) selection.steer();
  }, 1_000 / loaded.ticksPerSecond());
  window.addEventListener("pagehide", () => {
    window.clearInterval(controlTimer);
  });

  let hudLineAt = Number.NEGATIVE_INFINITY;
  const frames = new FrameTimer();
  const frame = (now: number) => {
    const workStart = performance.now();
    const dt = Math.min(0.1, (now - last) / 1_000);
    last = now;
    const current = life;

    // Drawing can be slower than the simulation; input delivery has its own clock.
    const tickMs = 1_000 / (current?.session.tickRate ?? 20);

    // In flight the view springs back once the look control is released (design §5).
    if (held.has("KeyR")) {
      look = { ...home };
      viewWidth = START_WIDTH;
      selection.steer();
      resize();
    }
    const keyTurn = axis(["KeyE"], ["KeyQ"]);
    if (keyTurn !== 0) look = turnLook(look, keyTurn * KEY_TURN_RATE * dt);
    // At the outpost the view stays where it is turned (design §5).
    else if (!dragFrom && current && !isHome(look, home)) look = settleLook(look, home, dt);

    let own: EntityPose | undefined;
    if (current) {
      const { session, predictor } = current;
      const others = session.snapshots.sample(now).filter((pose) => pose.id !== session.shipId);
      const predicted = predictor.visual(
        Math.min(1, Math.max(0, now - lastControlAt) / tickMs),
        dt,
      );
      own = predicted
        ? { id: session.shipId, kind: "playerShip", hull: current.hull, ...predicted }
        : undefined;
      poses = own ? [own, ...others] : others;
      const drawnAt = (id: number) => poses.find((pose) => pose.id === id);
      for (const event of current.events.take()) {
        if (event.type === "fired") {
          const from = drawnAt(event.shooter);
          if (from) shots.fire(event.shooter, event.target, from, event.speed);
        } else if (event.type === "intercepted") {
          const at = drawnAt(event.target);
          if (at) shots.intercept(event.shooter, event.target, at);
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
    const atHome = !current && outpost && holdings ? outpost : null;
    let aim = atHome ? atHome.centre : target;
    if (glide) {
      // A glide still under way when the view changes again (a run that ends at once) stops.
      const under = glide.toStation === (atHome !== null);
      glide.t = Math.min(1, glide.t + dt / GLIDE_SECONDS);
      // Into the run it follows the ship as it flies off the berth.
      if (under && outpost) {
        const from = glided.copy(outpost.centre).add(glide.from);
        aim = from.lerp(aim, glideEase(glide.t));
      }
      if (!under || glide.t >= 1) glide = null;
    }
    if (atHome) {
      view.renderScene(atHome.scene, aim, look.pitch, look.yaw);
    } else {
      view.render(poses, selection.lock, aim, look.pitch, look.yaw, shots, dt);
    }

    drawHomeLabels();

    const locked = poses.find((pose) => pose.id === selection.lock);
    if (locked && own && current) {
      const distance = Math.hypot(locked.x - own.x, locked.z - own.z);
      setData(lock, "target", String(locked.id));
      setText(
        lock,
        `Locked: ${KIND_NAME[locked.kind]} ${String(locked.id)} · ${distance.toFixed(0)} m ` +
          `${distance <= current.joined.weaponRange ? "in range" : `out of range, fires within ${current.joined.weaponRange.toFixed(0)} m`} · ` +
          `hull ${String(Math.round((locked.hull / 255) * 100))}% · Tab next, Esc release`,
      );
    } else {
      setData(lock, "target", null);
      setText(lock, "");
    }

    if (own && current) {
      const { session, predictor } = current;
      setData(hud, "ships", poses.map((pose) => pose.id).join(" "));
      if (session.roundTripMs !== null) setData(hud, "roundTrip", session.roundTripMs.toFixed(0));
      setData(hud, "x", own.x.toFixed(2));
      setData(hud, "z", own.z.toFixed(2));
      // Round trip and correction waver from frame to frame; nobody reads them that fast.
      if (now - hudLineAt >= HUD_LINE_MS) {
        hudLineAt = now;
        setText(
          hud,
          `Ship ${String(session.shipId)} · hull ${String(Math.round((current.hull / 255) * 100))}% · ` +
            `round trip ${session.roundTripMs === null ? "–" : `${session.roundTripMs.toFixed(0)} ms`} · ` +
            `correction ${(predictor.lastCorrection * 100).toFixed(0)} cm · ` +
            `W A S D fly · Shift cruise · Tab lock · Space fire · X extract · V vent · C scan`,
        );
      }
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
        setData(run, "phase", status.phase);
        setData(run, "cargo", String(status.cargo));
        setData(run, "extractor", status.extractor);
        setText(run, runLine(status) + hint);
        const pocketName = (index: number) => {
          const pocket = current.joined.site.pockets[index];
          return pocket ? POCKET_NAME[pocket.kind] : "?";
        };
        setData(scan, "revealed", String(reveal !== null));
        setText(scan, scanLine(status, reveal, pocketName));
      }
      drawNav(own);
    } else {
      setData(run, "phase", null);
      setData(hud, "x", null);
      hudLineAt = Number.NEGATIVE_INFINITY;
      setText(hud, "");
      setText(run, "");
      setText(scan, "");
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
