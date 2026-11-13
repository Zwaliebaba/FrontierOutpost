/**
 * The game client: takes the contract on offer, flies the player's ship through the run with the
 * keyboard, and draws every ship from the server's snapshots with the tactical camera (design §5).
 */
import "./command.css";
import "./chart.css";
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
import { crewQuote, launchRole, renderCrew, resetSplitPending } from "./outpost/crew";
import { loadOutpostModels } from "./outpost/models";
import { affordableFacilities, fittingName, launchLines, supplySales } from "./outpost/options";
import { BERTHS, placeStation } from "./outpost/placement";
import { OutpostScene } from "./outpost/scene";
import type { HomeTab } from "./outpost/ui";
import { renderHome, tickProcessor } from "./outpost/ui";
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
  runCells,
  scanSummary,
  summaryLines,
} from "./game/contract";
import type { Contract, Joined, RunCell, Scanned } from "./game/contract";
import { chartPlaces, jumpReadiness, sideOf, waypointFor } from "./game/chart";
import type { ChartPlace } from "./game/chart";
import { ChartView, glyph, sitePreview } from "./game/chartView";
import { Pulse, gauges } from "./game/gauges";
import { CANCEL_TEXT, JumpDrive, REFUSAL_TEXT } from "./game/jump";
import { JumpEffects } from "./game/jumpEffects";
import type { GaugeKey } from "./game/gauges";
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
import { clearOf, distanceText, placeMarkers } from "./game/navigation";
import type { Box } from "./game/navigation";
import { Receipts } from "./game/receipts";
import { Notices } from "./game/notices";
import type { NoticeLevel } from "./game/notices";
import { prompts } from "./game/prompts";
import type { Prompt } from "./game/prompts";
import { SCAN_REFUSAL_TEXT } from "./game/scan";
import { ExpeditionView, RESTING_ELEVATION } from "./game/render";
import { targetReadout } from "./game/target";
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
/** Room the waypoint keeps from the screen's edges: the bar at the top, the footer below. */
const NAV_INSETS = { top: 62, right: 16, bottom: 48, left: 16 };
/** The top bar's and the footer's heights (command.css): the waypoint stays between them. */
const BAR_HEIGHT = 64;
const FOOTER_HEIGHT = 30;
/** The flight keys, shown in the footer for as long as a run lasts. */
const FLIGHT_KEYS =
  "W A S D fly · Shift cruise · M map · J jump · Tab lock · Space fire · X extract · V vent · C scan";
/** How often the map and the minimap are redrawn, at most: ships cross them slowly. */
const CHART_MS = 100;
/** How soon a dropped connection is retried, and how often an unreachable server. */
const RECONNECT_MS = 1_500;
/** How often the HUD's top line is rewritten, at most. */
const HUD_LINE_MS = 200;
const RETRY_MS = 3_000;
/** The reason the server closes a session with when the pilot signs in on another page. */
const SIGNED_IN_ELSEWHERE = "signed in elsewhere";
/** Within this distance of a deposit a prompt points to it, metres. */
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
// The run status: one cell an instrument, in the top bar for as long as a run lasts.
const run = element("div", "run");
// The locked target, under the top bar at the middle of the screen.
const lock = element("div", "lock");
// Bottom centre: the notices, and under them the prompts for the keys that would act now.
const messages = element("div", "messages");
const notice = element("div", "notice");
notice.setAttribute("role", "status");
notice.setAttribute("aria-live", "polite");
const promptRow = element("div", "prompts");
messages.append(notice, promptRow);
// A wash of the drive's light over the whole view as the pilot's own ship jumps: the camera
// goes with the ship, and the wash covers the cut.
const jumpWash = element("div", "jump-wash");
jumpWash.setAttribute("aria-hidden", "true");
const accountLine = line("account");
const command = element("header", "command");
const brand = element("div", undefined, "brand");
brand.innerHTML =
  '<span class="insignia" aria-hidden="true">⌃</span><strong>FRONTIER / OUTPOST</strong>';
// One bar: the brand, what the pilot holds at home or the run's status in flight, and the
// connection.
command.append(brand, accountLine, run, status);
const utility = element("footer", "utility");
utility.textContent = "HOME OUTPOST   /   BERTHS 07–10";
const cameraHints = element("span");
cameraHints.textContent = "SCROLL  Zoom   /   RMB  Orbit   /   R  Reset";
// In flight the footer carries the keys. The link's diagnostics stay in the page for the
// browser tests, out of sight: nobody flies by a round trip in milliseconds.
const hudKeys = element("span", undefined, "hud-keys");
const hudDiagnostics = element("span", undefined, "diagnostic");
hud.append(hudKeys, hudDiagnostics);
utility.append(hud, cameraHints, sim);
document.body.append(command, utility, lock, messages, jumpWash);
const stationLabels = element("div", "station-labels");
document.body.append(stationLabels);
const nav = element("div", "nav");
// The ship gauges: one row each for hull, hold and heat, built once and updated in place.
const shipPanel = element("div", "ship");
const gaugeRows = new Map<
  GaugeKey,
  {
    row: HTMLDivElement;
    label: HTMLSpanElement;
    fill: HTMLDivElement;
    value: HTMLSpanElement;
    note: HTMLSpanElement;
  }
>();
for (const key of ["hull", "hold", "heat"] as const) {
  const row = element("div", undefined, "gauge");
  row.dataset.gauge = key;
  const label = element("span", undefined, "gauge-label");
  const track = element("div", undefined, "gauge-track");
  const fill = element("div", undefined, "gauge-fill");
  const value = element("span", undefined, "gauge-value");
  const note = element("span", undefined, "gauge-note");
  track.append(fill);
  row.append(label, track, value, note);
  shipPanel.append(row);
  gaugeRows.set(key, { row, label, fill, value, note });
}
document.body.append(shipPanel);
const signInPanel = element("div", "signin", "panel");
const briefing = element("div", "briefing", "panel");
const summaryPanel = element("div", "summary", "panel");
const homePanel = element("div", "home", "panel");
document.body.append(nav, signInPanel, homePanel, briefing, summaryPanel);

function showStatus(state: "connecting" | "connected" | "closed" | "offline", text: string) {
  status.dataset.state = state;
  status.textContent = text;
}

const notices = new Notices();
function showNotice(text: string, level: NoticeLevel = "info"): void {
  notices.push(text, level, performance.now());
}
let noticesDrawn = "";
/** Writes the notices on show to the page, when they have changed. */
function drawNotices(now: number): void {
  const shown = notices.visible(now);
  const key = shown.map((entry) => entry.id).join(" ");
  if (key === noticesDrawn) return;
  noticesDrawn = key;
  notice.replaceChildren(
    ...shown.map((entry) => {
      const node = element("p", undefined, "notice");
      node.dataset.level = entry.level;
      node.textContent = entry.text;
      return node;
    }),
  );
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
      showNotice(`The outpost models did not load: ${String(error)}`, "warn");
    });
  play(connection, view, loaded);
  await identify(connection);
}

/** What the pilot's home holds, the outpost's rules, and the outpost as drawn. */
let holdings: AccountInfo | null = null;
/** When `holdings` arrived, on the page's clock: the processor's progress counts on from it. */
let holdingsAt = 0;
const sinceHoldings = () => (performance.now() - holdingsAt) / 1000;
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
  holdingsAt = performance.now();
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
  /** Recent changes to the hull and the hold, which the gauges call out. */
  hullPulse: Pulse;
  cargoPulse: Pulse;
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
    hullPulse: new Pulse(),
    cargoPulse: new Pulse(),
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
        const now = performance.now();
        life.cargoPulse.observe(snapshot.own.cargo, now);
        const own = snapshot.entities.find((entity) => entity.id === joined.shipId);
        if (!own) return;
        life.hull = own.hull;
        life.hullPulse.observe(own.hull, now);
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

const TIER_NAME = ["", "I", "II", "III"];

function play(connection: Connection, view: ExpeditionView, loaded: Sim): void {
  let life: Life | null = null;
  let selectedTier = 1;
  let contract: Contract | null = null;
  let reveal: Scanned | null = null;
  /** The destination chosen for the jump drive, and any spool in progress. */
  const jumpDrive = new JumpDrive();
  /** The spool on the pilot's ship and the flashes of every pilot's jump, as drawn. */
  const jumpEffects = new JumpEffects();
  /** How many jumps this page has drawn this run, the pilot's own and the crew's, for tests. */
  let jumpsSeen = 0;
  const calm = window.matchMedia("(prefers-reduced-motion: reduce)");
  /** When the map was last redrawn; set back to redraw it on the next frame. */
  let chartAt = Number.NEGATIVE_INFINITY;
  /** J, or the map's button: cancels a spool, jumps to the destination chosen, or asks for one. */
  const engageJump = () => {
    if (!life) return;
    const action = jumpDrive.press(life.joined.site.pockets);
    if (action) {
      send({ type: "jump", target: action.jump });
    } else {
      // Nothing chosen: the map is where a destination is chosen, so J leads there.
      chart.open();
      showNotice("Jump drive: choose a destination on the map, then press J");
    }
    chartAt = Number.NEGATIVE_INFINITY;
  };
  const chart = new ChartView({
    choose: (id) => {
      jumpDrive.toggle(id);
      chartAt = Number.NEGATIVE_INFINITY;
    },
    engage: engageJump,
    jumpTo: (id) => {
      jumpDrive.selected = id;
      engageJump();
    },
  });
  document.body.append(chart.navigator, chart.overlay);
  /**
   * The camera moving between the station and the ship, when a run begins or ends. It starts
   * `from` this far from the station's centre, so it holds if the next dock moves the station.
   */
  let glide: { from: Vector3; toStation: boolean; t: number } | null = null;
  const glided = new Vector3();
  const shots = new Shots();
  resetSplitPending();
  const receipts = new Receipts();
  const receiptKey = `frontier.receipts.${callsign}`;
  try {
    receipts.restore(localStorage.getItem(receiptKey));
  } catch {
    /* Storage may be disabled. */
  }
  const saveReceipts = () => {
    try {
      localStorage.setItem(receiptKey, receipts.serialize());
    } catch {
      /* History is optional; account state remains authoritative. */
    }
  };

  const showBriefing = (offer: Contract) => {
    const scrollTop = briefing.querySelector(".briefing-content")?.scrollTop ?? 0;
    const focused =
      document.activeElement instanceof HTMLElement && briefing.contains(document.activeElement)
        ? document.activeElement
        : null;
    const focusAction = focused?.dataset.action;
    const focusSplit = focused?.dataset.split;
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
    // The site as the system map will draw it, and its pockets listed with the map's glyphs.
    const { site } = offer;
    const figure = element("figure", undefined, "briefing-site");
    figure.append(
      sitePreview(site, chartPlaces(site, { x: site.dockX, z: site.dockZ }, null, false), 244, 108),
    );
    const pockets = element("ul", undefined, "briefing-places");
    for (const pocket of site.pockets) {
      const item = element("li");
      item.dataset.locked = String(pocket.locked);
      const needs = POCKET_NEEDS[pocket.kind];
      item.append(
        glyph(pocket.kind, 18),
        pocket.locked
          ? `${POCKET_NAME[pocket.kind]}: locked, needs ${needs ?? "a facility"}`
          : `${POCKET_NAME[pocket.kind]}: open`,
      );
      pockets.append(item);
    }
    const note = element("p");
    note.textContent =
      "Fly out, extract, and dock with at least 10 units to collect the contract. " +
      "Scanning in the site reveals the rest.";
    const stake = element("div", undefined, "stake");
    const remedies = element("div", undefined, "launch-remedies");
    if (catalog && holdings) {
      const known = catalog;
      for (const text of launchLines(known, holdings)) {
        const p = element("p");
        p.textContent = text;
        stake.append(p);
      }
      // Short of supplies: the sales that cover it, one click each, before the launch refuses.
      for (const sale of supplySales(known, holdings)) {
        const sell = element("button");
        sell.dataset.action = `supply-sale:${sale.good}`;
        sell.textContent = `Sell ${String(sale.amount)} ${sale.good} · +${String(sale.credits)} cr`;
        sell.addEventListener("click", () => {
          send({ type: "sell", good: sale.good, amount: sale.amount });
        });
        remedies.append(sell);
      }
      if (holdings.launch.due.type === "short" && supplySales(known, holdings).length === 0) {
        const p = element("p");
        p.textContent = "Your goods are at the processor: take a batch back to sell its inputs.";
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
        remedies.append(buy);
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
      option.textContent = `Tier ${TIER_NAME[tier.tier] ?? String(tier.tier)} / pays ${String(catalog ? crewQuote(catalog, tier.payout, crew).total : tier.payout)} cr${tier.open ? "" : " / locked"}`;
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
    // The server would refuse a pilot who must sell first; say so here, where the button is.
    const due = holdings?.launch.due;
    const short = due?.type === "short" ? due.missing : 0;
    if (short > 0) {
      const p = element("p", undefined, "crew-note");
      p.dataset.short = String(short);
      p.textContent = `Supplies: sell ${String(short)} cr of goods to launch`;
      tiers.append(p);
    }
    for (const tier of offer.tiers) {
      const button = element("button");
      button.dataset.tier = String(tier.tier);
      button.hidden = tier.tier !== selectedTier;
      const following = role.follow === tier.tier;
      button.textContent = following
        ? `Follow the host · tier ${TIER_NAME[tier.tier] ?? String(tier.tier)}`
        : `Launch · tier ${TIER_NAME[tier.tier] ?? String(tier.tier)}`;
      if (following) button.dataset.action = "follow";
      const ready = role.launches ? tier.open : following;
      button.disabled = !ready || short > 0;
      if (!tier.open) button.title = "Opens with a tier 2 facility";
      else if (!ready && role.note) button.title = role.note;
      else if (short > 0) button.title = `Sell ${String(short)} cr of goods first`;
      button.addEventListener("click", () => {
        for (const other of tiers.querySelectorAll("button")) other.disabled = true;
        // The run begins when `joined` arrives (handled below, as for a rejoined run).
        launch(connection, tier.tier).catch((error: unknown) => {
          note.textContent = `Could not launch: ${error instanceof Error ? error.message : String(error)}`;
          for (const other of offer.tiers) {
            const b = tiers.querySelector<HTMLButtonElement>(`[data-tier="${String(other.tier)}"]`);
            if (b) {
              b.disabled = (role.launches ? !other.open : role.follow !== other.tier) || short > 0;
            }
          }
        });
      });
      tiers.append(button);
    }
    const content = element("div", undefined, "briefing-content");
    const heading = element("div", undefined, "briefing-heading");
    const crewControl = element("button");
    crewControl.dataset.action = "crew-controls";
    crewControl.textContent = `CREW ${String(crew?.members.length ?? 1)}/4`;
    crewControl.setAttribute("aria-label", "Open crew formation and payout controls");
    crewControl.addEventListener("click", () => {
      crewSection.scrollIntoView({ block: "start" });
      crewSection.querySelector<HTMLElement>("input,button")?.focus({ preventScroll: true });
    });
    heading.append(title, crewControl);
    content.append(eyebrow, heading, note, figure, pockets, facts, remedies, crewSection);
    tiers.prepend(stake);
    const quoted = offer.tiers.find((tier) => tier.tier === selectedTier);
    if (catalog && quoted && (crew?.members.length ?? 1) > 1) {
      const quote = crewQuote(catalog, quoted.payout, crew);
      const payout = element("p", undefined, "crew-quote");
      payout.textContent = `Crew contract pot ${String(quote.total)} cr · your ${String(quote.percent)}% ≈ ${String(quote.share)} cr`;
      const terms = element("small");
      terms.textContent = `Estimate with ${String(quote.pilots)} pilot(s) following. Cargo is personal; the pot pays when the expedition ends. Bonuses and missing followers change the final share.`;
      payout.append(terms);
      tiers.insertBefore(payout, stake.nextSibling);
      tierSelect.addEventListener("change", () => {
        showBriefing(offer);
        document.getElementById("threat-tier")?.focus();
      });
    }
    briefing.append(content, tiers);
    content.scrollTop = scrollTop;
    if (focusAction || focusSplit) {
      const replacement = [
        ...briefing.querySelectorAll<HTMLElement>("[data-action], [data-split]"),
      ].find((node) =>
        focusSplit !== undefined
          ? node.dataset.split === focusSplit
          : node.dataset.action === focusAction,
      );
      replacement?.focus({ preventScroll: true });
    }
  };

  const showSummary = (summary: RunSummary) => {
    summaryPanel.replaceChildren();
    receipts.add(summary);
    saveReceipts();
    summaryPanel.dataset.ending = summary.ending;
    summaryPanel.dataset.run = summary.run;
    summaryPanel.dataset.settled = String(summary.settled);
    const lines = summaryLines(summary);
    const title = element("h2");
    title.textContent = lines[0] ?? "";
    summaryPanel.append(title);
    const history = element("select");
    history.setAttribute("aria-label", "Expedition receipt");
    for (const [index, receipt] of receipts.all.entries()) {
      const option = element("option");
      option.value = receipt.run;
      option.textContent = `Expedition ${String(receipts.all.length - index)} · ${receipt.ending} · ${receipt.settled ? `${String(receipt.payout)} cr` : "payment pending"}`;
      option.selected = receipt.run === summary.run;
      history.append(option);
    }
    history.addEventListener("change", () => {
      const receipt = receipts.all.find((r) => r.run === history.value);
      if (receipt) showSummary(receipt);
    });
    summaryPanel.append(history);
    for (const text of lines.slice(1)) {
      const p = element("p");
      p.textContent = text;
      summaryPanel.append(p);
    }
    if (summary.settled) {
      const pot = element("p");
      pot.textContent = `Crew pot: contract ${String(summary.pot.contract)} · bounty ${String(summary.pot.bounty)} · relays ${String(summary.pot.relay)} cr. Your banked cargo belongs to you.`;
      summaryPanel.append(pot);
    }
    // The account arrives before the summary, so it already says whether supplies carried.
    if (
      summary.ending === "docked" &&
      summary.settled &&
      summary.pot.contract === 0 &&
      summary.run === receipts.all[0]?.run &&
      holdings?.launch.due.type === "carried"
    ) {
      const p = element("p", undefined, "crew-note");
      p.dataset.supplies = "carried";
      p.textContent = "Contract not met: your supplies carry over, so the next launch is free.";
      summaryPanel.append(p);
    }
    const next = element("button");
    next.textContent = "Next contract";
    next.addEventListener("click", () => {
      delete summaryPanel.dataset.ending;
      if (contract) showBriefing(contract);
    });
    summaryPanel.append(next);
    homeChanged();
    const ready = catalog && holdings ? affordableFacilities(catalog, holdings) : [];
    const development = ready[0];
    if (development) {
      const develop = element("button");
      develop.dataset.action = "develop-outpost";
      develop.textContent = `Compare ${String(ready.length)} affordable facilities`;
      develop.addEventListener("click", () => {
        delete summaryPanel.dataset.ending;
        selectedSocket = development.socket;
        homeTab = "facilities";
        homeChanged();
        if (contract) showBriefing(contract);
      });
      summaryPanel.append(develop);
    }
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
    jumpDrive.reset();
    jumpEffects.reset();
    jumpsSeen = 0;
    chart.close();
    chart.setSite(
      joined.site,
      joined.obstacles,
      joined.deposits,
      chartPlaces(joined.site, { x: joined.site.dockX, z: joined.site.dockZ }, null, false),
    );
    chartAt = Number.NEGATIVE_INFINITY;
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
        ...(receipts.all.length
          ? {
              receipt: () => {
                const last = receipts.all[0];
                if (last) showSummary(last);
              },
            }
          : {}),
        tab: (tab) => {
          homeTab = tab;
          homeChanged();
        },
        select: (socket, facility) => {
          selectedSocket = socket;
          homeTab = "facilities";
          homeChanged();
          if (facility) {
            const card = homePanel.querySelector<HTMLElement>(
              `[data-facility="${CSS.escape(facility)}"]`,
            );
            card?.scrollIntoView({ block: "nearest" });
            card?.focus({ preventScroll: true });
          }
        },
      },
      homeTab,
      sinceHoldings(),
    );
    if (!life) homePanel.dataset.state = "open";
    if (briefing.dataset.state === "open" && contract) showBriefing(contract);
  };
  // The processor's bar moves on between account messages; the server finishes the batch.
  window.setInterval(() => {
    if (holdings && homePanel.dataset.state === "open") {
      tickProcessor(homePanel, holdings, sinceHoldings());
    }
  }, 1_000);

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
        jumpDrive.reset();
        jumpEffects.reset();
        chart.clear();
        selection.release();
        showSummary(message.summary);
        homeChanged();
        break;
      case "scanned": {
        reveal = message;
        // What it found about the pockets is on the map from now on; the notice says so once.
        const pockets = life?.joined.site.pockets ?? [];
        showNotice(
          scanSummary(message, (index) => {
            const pocket = pockets[index];
            return pocket ? POCKET_NAME[pocket.kind] : "?";
          }),
          "good",
        );
        break;
      }
      case "account":
        showAccount(callsign, message.account);
        break;
      case "catalog":
        catalog = message.catalog;
        homeChanged();
        break;
      case "crewRefused":
        resetSplitPending();
        homeChanged();
        showNotice(message.reason, "warn");
        break;
      case "homeRefused":
        showNotice(message.reason, "warn");
        break;
      case "crew":
        crew = message.crew ?? null;
        homeChanged();
        break;
      case "potPaid": {
        const { pot, share } = message;
        const receipt = receipts.settle(message.run, pot, share);
        saveReceipts();
        if (receipt && summaryPanel.dataset.run === message.run && summaryPanel.dataset.ending)
          showSummary(receipt);
        homeChanged();
        showNotice(
          `The crew's pot is paid: ${String(pot.contract + pot.bounty + pot.relay)} cr ` +
            `(contract ${String(pot.contract)}, bounty ${String(pot.bounty)}, relays ` +
            `${String(pot.relay)}). Your share: ${String(share)} cr`,
          "good",
        );
        break;
      }
      case "extractRefused":
        showNotice(refusalText(message.reason), "warn");
        break;
      case "scanRefused":
        showNotice(SCAN_REFUSAL_TEXT[message.reason], "warn");
        break;
      case "jumpSpooling":
        if (message.ship !== life?.session.shipId) {
          jumpEffects.remoteCharge(message.ship, message.seconds);
          break;
        }
        jumpDrive.spooling(message.target, message.seconds, performance.now());
        // The drive is spooling: back to the scene, where a hostile lock would show.
        chart.close();
        chartAt = Number.NEGATIVE_INFINITY;
        break;
      case "jumpRefused":
        showNotice(REFUSAL_TEXT[message.reason], "warn");
        break;
      case "jumpCancelled":
        jumpEffects.remoteCharge(message.ship, 0);
        if (message.ship !== life?.session.shipId) break;
        jumpDrive.ended();
        chartAt = Number.NEGATIVE_INFINITY;
        showNotice(CANCEL_TEXT[message.reason], message.reason === "pilot" ? "info" : "warn");
        break;
      case "jumped":
        jumpEffects.remoteCharge(message.ship, 0);
        if (message.ship !== life?.session.shipId) break;
        selection.steer();
        jumpDrive.ended();
        chartAt = Number.NEGATIVE_INFINITY;
        break;
      case "limitWarning":
        showNotice(
          `The instance ends in ${String(Math.round(message.secondsLeft / 60))} minutes`,
          "warn",
        );
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
      showNotice(`No hostile within lock range (${range.toFixed(0)} m)`, "warn");
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
      // Esc closes the map first; with the map closed it lets go of the lock.
      if (chart.isOpen) chart.close();
      else selection.release();
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
    } else if (event.code === "KeyM") {
      chart.toggle();
      chartAt = Number.NEGATIVE_INFINITY;
    } else if (event.code === "KeyJ") {
      engageJump();
    } else if (chart.isOpen && /^Digit[1-9]$/.test(event.code)) {
      // With the map open, a digit chooses the destination it is listed beside.
      const id = chart.keyed(event.code.slice("Digit".length));
      if (id !== null) {
        jumpDrive.toggle(id);
        chartAt = Number.NEGATIVE_INFINITY;
      }
    }
  });
  const target = new Vector3();

  const resize = () => {
    const width = window.innerWidth;
    // The home and briefing panels with their margins (command.css).
    const left = homePanel.getBoundingClientRect();
    const right = briefing.getBoundingClientRect();
    const available = life ? width : Math.max(width * 0.35, right.left - left.right - 32);
    // A fixed envelope holds every facility tier, four ships and outward captions.
    // Fit the same envelope after purchases, so an upgrade never changes its apparent scale.
    const homeWidth = Math.max(
      (25 * width) / available,
      (23 * width) / Math.max(240, window.innerHeight - 150),
    );
    view.resize(
      width,
      window.innerHeight,
      life ? viewWidth : (homeWidth * viewWidth) / START_WIDTH,
    );
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
      showNotice(`Beyond lock range (${range.toFixed(0)} m): centred on it, not locked`, "warn");
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

  // The run status in the top bar: a cell for each reading, rebuilt only when the set of cells
  // changes, as when a scan starts or the relays are first held.
  const runCellNodes = new Map<string, { cell: HTMLSpanElement; value: HTMLElement }>();
  const drawRun = (cells: readonly RunCell[]) => {
    if (written.changed("run.cells", cells.map((cell) => cell.key).join(" "))) {
      written.forget("run.cell.");
      runCellNodes.clear();
      run.replaceChildren(
        ...cells.map((cell) => {
          const node = element("span", undefined, "cell");
          node.dataset.cell = cell.key;
          const label = element("span");
          label.textContent = cell.label;
          const value = element("strong");
          node.append(label, value);
          runCellNodes.set(cell.key, { cell: node, value });
          return node;
        }),
      );
    }
    for (const cell of cells) {
      const drawn = runCellNodes.get(cell.key);
      if (!drawn) continue;
      const place = `run.cell.${cell.key}`;
      if (written.changed(`${place}.value`, cell.value)) drawn.value.textContent = cell.value;
      if (written.changed(`${place}.level`, cell.level)) drawn.cell.dataset.level = cell.level;
    }
  };

  // The prompts: the keys that would act now, under the notices.
  const drawPrompts = (list: readonly Prompt[]) => {
    const key = list.map((prompt) => `${prompt.id}:${prompt.text}`).join("|");
    if (!written.changed("prompts", key)) return;
    promptRow.replaceChildren(
      ...list.map((prompt) => {
        const node = element("span", undefined, "prompt");
        node.dataset.prompt = prompt.id;
        if (prompt.key !== "") {
          const cap = element("kbd");
          cap.textContent = prompt.key;
          node.append(cap);
        }
        node.append(prompt.text);
        return node;
      }),
    );
  };

  // The target card: built once, shown while a hostile is locked.
  const lockCard = (() => {
    const name = element("strong", "lock-name");
    const distance = element("span", "lock-distance");
    const range = element("span", "lock-range");
    const track = element("div", undefined, "lock-track");
    const fill = element("div", undefined, "lock-fill");
    track.append(fill);
    const hull = element("span", "lock-hull");
    return { name, distance, range, hull, fill, nodes: [name, distance, range, track, hull] };
  })();

  // The waypoint: the destination chosen on the map, marked where it is on screen or at the
  // screen's edge toward it. It is the scene's one label, and it takes no clicks.
  const waypoint = element("div", "waypoint");
  const waypointArrow = element("span", undefined, "waypoint-arrow");
  const waypointText = element("span", undefined, "waypoint-text");
  waypoint.append(waypointText, waypointArrow);
  waypoint.hidden = true;
  nav.append(waypoint);
  let waypointSize = { width: 0, height: 0 };
  /** Where the HUD's panels are, so the waypoint is never drawn under one. */
  let hudPanels: Box[] = [];
  const measurePanels = () => {
    hudPanels = [shipPanel, lock, messages, chart.navigator]
      .map((node) => node.getBoundingClientRect())
      .filter((box) => box.width > 0 && box.height > 0)
      .map(({ x, y, width, height }) => ({ x, y, width, height }));
  };
  const drawWaypoint = (place: ChartPlace | undefined) => {
    if (written.changed("waypoint.hidden", String(!place))) waypoint.hidden = !place;
    if (!place) return;
    const project = (x: number, z: number) =>
      worldToScreen(new Vector3(x, 0, z), view.camera, view.viewport);
    const [marker] = placeMarkers(
      [{ id: place.id, label: place.name, x: place.x, z: place.z }],
      { x: 0, z: 0 },
      project,
      view.viewport,
      NAV_INSETS,
    );
    if (!marker) return;
    setData(waypoint, "target", place.id);
    setData(waypoint, "onScreen", String(marker.onScreen));
    // Off screen the arrow points the way; on screen it points down at the place itself.
    const turned = marker.onScreen ? "rotate(90deg)" : `rotate(${marker.angle.toFixed(2)}rad)`;
    if (written.changed("waypoint.arrow", turned)) waypointArrow.style.transform = turned;
    const text = `${place.name} · ${distanceText(place.distance)}`;
    const resized =
      written.changed("waypoint.length", `${String(text.length)} ${String(marker.onScreen)}`) ||
      waypointSize.width === 0;
    if (written.changed("waypoint.text", text)) waypointText.textContent = text;
    // The font is monospace: the size changes only when the text's length does.
    if (resized) waypointSize = { width: waypoint.offsetWidth, height: waypoint.offsetHeight };
    const { width, height } = waypointSize;
    // Above an on-screen place; inward from an edge, so the label stays whole.
    const lift = marker.onScreen ? 10 : 0;
    const box = clearOf(
      {
        x: marker.screen.x - marker.anchor.x * width,
        y: marker.screen.y - lift - marker.anchor.y * height,
        width,
        height,
      },
      hudPanels,
      {
        x: 0,
        y: BAR_HEIGHT,
        width: view.viewport.width,
        height: view.viewport.height - BAR_HEIGHT - FOOTER_HEIGHT,
      },
    );
    const moved = `translate(${box.x.toFixed(0)}px, ${box.y.toFixed(0)}px)`;
    if (written.changed("waypoint.at", moved)) waypoint.style.transform = moved;
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
        at: station.root.localToWorld(new Vector3(b.x, b.deck + 0.2, b.z + 3.4)),
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
      const left = homePanel.getBoundingClientRect().right + 8;
      const right = briefing.getBoundingClientRect().left - 8;
      node.hidden =
        screen.x < left || screen.x > right || screen.y < 126 || screen.y > window.innerHeight - 70;
      if (written.changed(`home.${marker.id}.text`, marker.label)) node.textContent = marker.label;
      const transform = `translate(${screen.x.toFixed(0)}px, ${screen.y.toFixed(0)}px) translate(-50%, ${marker.id.startsWith("berth-") ? "0" : "-100%"})`;
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
    view.world.configureFeedback(!calm.matches, current?.own?.shield ?? 0);
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
    /** Seconds since the last frame as they passed, however long: a hidden tab draws none. */
    const elapsed = Math.max(0, (now - last) / 1_000);
    const dt = Math.min(0.1, elapsed);
    last = now;
    const current = life;
    view.world.configureFeedback(!calm.matches, current?.own?.shield ?? 0);

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
      // A pilot's ship that moved farther than flight allows jumped: flash both ends.
      const spool = jumpDrive.readout(now, current.joined.site.pockets);
      jumpEffects.setCharge(spool.state === "spooling" ? session.shipId : null, spool.share);
      setData(
        hud,
        "crewSpools",
        String(jumpEffects.charges.filter((charge) => charge.ship !== session.shipId).length),
      );
      const leapt = jumpEffects.observe(poses, elapsed);
      if (leapt.length > 0) {
        jumpsSeen += leapt.length;
        setData(hud, "jumps", String(jumpsSeen));
        // The wash covers the camera's cut, so it is for the pilot's own jump only.
        if (leapt.includes(session.shipId)) selection.steer();
        if (leapt.includes(session.shipId) && !calm.matches) {
          jumpWash.animate([{ opacity: 0 }, { opacity: 0.7, offset: 0.2 }, { opacity: 0 }], {
            duration: 520,
            easing: "ease-out",
          });
        }
      }
      jumpEffects.advance(dt, elapsed);
    } else {
      poses = [];
    }
    const focus = poses.find((pose) => pose.id === selection.focus);
    const centre = focus ?? own;
    setData(hud, "camera", centre ? String(centre.id) : null);
    setData(hud, "cameraMoving", String(glide !== null));
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
      view.render(poses, selection.lock, aim, look.pitch, look.yaw, shots, dt, jumpEffects);
    }

    drawHomeLabels();

    const locked = poses.find((pose) => pose.id === selection.lock);
    const lockedAt = locked && own ? Math.hypot(locked.x - own.x, locked.z - own.z) : 0;
    if (locked && own && current) {
      const readout = targetReadout(
        locked.kind,
        locked.id,
        lockedAt,
        current.joined.weaponRange,
        locked.hull,
      );
      if (written.changed("lock.shown", "true")) lock.append(...lockCard.nodes);
      setData(lock, "target", String(locked.id));
      setData(lock, "range", readout.inRange ? "in" : "out");
      setText(lockCard.name, readout.name);
      setText(lockCard.distance, readout.distance);
      setText(lockCard.range, readout.range);
      setText(lockCard.hull, readout.hull);
      const width = `${(readout.hullShare * 100).toFixed(1)}%`;
      if (written.changed("lock.fill", width)) lockCard.fill.style.width = width;
    } else {
      setData(lock, "target", null);
      setData(lock, "range", null);
      if (written.changed("lock.shown", "false")) lock.replaceChildren();
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
        const diagnostics =
          ` · Ship ${String(session.shipId)} · ` +
          `round trip ${session.roundTripMs === null ? "–" : `${session.roundTripMs.toFixed(0)} ms`} · ` +
          `correction ${(predictor.lastCorrection * 100).toFixed(0)} cm`;
        if (written.changed("hud.keys", FLIGHT_KEYS)) hudKeys.textContent = FLIGHT_KEYS;
        if (written.changed("hud.diagnostics", diagnostics)) {
          hudDiagnostics.textContent = diagnostics;
        }
      }
      const status = current.own;
      if (status) {
        const nearest = current.joined.deposits
          .map((deposit, index) => ({
            index,
            distance: Math.hypot(deposit.x - own.x, deposit.z - own.z),
            left: current.deposits[index] ?? deposit.units,
          }))
          .filter((deposit) => deposit.left > 0)
          .sort((a, b) => a.distance - b.distance)[0];
        setData(run, "phase", status.phase);
        setData(run, "cargo", String(status.cargo));
        setData(run, "extractor", status.extractor);
        setData(run, "revealed", String(reveal !== null));
        drawRun(runCells(status, reveal));
        const lockRange = current.joined.lockRange;
        drawPrompts(
          prompts({
            own: status,
            deposit:
              nearest && nearest.distance <= DEPOSIT_HINT
                ? {
                    distance: nearest.distance,
                    left: nearest.left,
                    inReach: nearest.distance <= current.joined.extractRange,
                  }
                : null,
            scanned: reveal !== null,
            locked: locked !== undefined,
            lockedInRange: locked !== undefined && lockedAt <= current.joined.weaponRange,
            hostileInLockRange: poses.some(
              (pose) =>
                pose.kind !== "playerShip" &&
                Math.hypot(pose.x - own.x, pose.z - own.z) <= lockRange,
            ),
          }),
        );
        setData(shipPanel, "active", "true");
        for (const gauge of gauges(
          status,
          current.hull,
          current.hullPulse.recent(now),
          current.cargoPulse.recent(now),
          current.joined.contractCargo,
        )) {
          const row = gaugeRows.get(gauge.key);
          if (!row) continue;
          const id = `ship.${gauge.key}`;
          if (written.changed(`${id}.level`, gauge.level)) row.row.dataset.level = gauge.level;
          const width = `${(gauge.share * 100).toFixed(1)}%`;
          if (written.changed(`${id}.width`, width)) row.fill.style.width = width;
          if (written.changed(`${id}.label`, gauge.label)) row.label.textContent = gauge.label;
          if (written.changed(`${id}.value`, gauge.value)) row.value.textContent = gauge.value;
          if (written.changed(`${id}.note`, gauge.note)) row.note.textContent = gauge.note;
        }
        // The extractor's beam, to the deposit it can be working: the nearest within reach.
        const working =
          status.extractor === "on" && nearest && nearest.distance <= current.joined.extractRange;
        view.world.setBeam(working ? own : null, working ? nearest.index : null);
      }
      // The map, the minimap beside the gauges, and the waypoint to the destination chosen.
      const { site } = current.joined;
      const places = chartPlaces(site, own, reveal, (status?.cargo ?? 0) > 0);
      const chosen = places.find((place) => place.id === jumpDrive.selected);
      if (now - chartAt >= CHART_MS) {
        chartAt = now;
        const ownId = session.shipId;
        chart.update({
          places,
          relay: status?.relay ?? 0,
          selected: jumpDrive.selected,
          readout: jumpDrive.readout(now, site.pockets),
          readiness: jumpReadiness(chosen ?? null, status?.drive ?? null),
          own,
          contacts: poses.map((pose) => ({ ...pose, side: sideOf(pose.kind, pose.id, ownId) })),
          deposits: current.deposits,
        });
        measurePanels();
      }
      drawWaypoint(waypointFor(places, jumpDrive.selected));
    } else {
      setData(run, "phase", null);
      setData(run, "revealed", null);
      drawRun([]);
      drawPrompts([]);
      setData(shipPanel, "active", null);
      setData(hud, "x", null);
      hudLineAt = Number.NEGATIVE_INFINITY;
      if (written.changed("hud.keys", "")) hudKeys.textContent = "";
      if (written.changed("hud.diagnostics", "")) hudDiagnostics.textContent = "";
      drawWaypoint(undefined);
    }
    drawNotices(now);

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
