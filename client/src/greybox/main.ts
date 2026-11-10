import {
  AlwaysStencilFunc,
  BoxGeometry,
  Color,
  CylinderGeometry,
  DirectionalLight,
  DoubleSide,
  EqualStencilFunc,
  ExtrudeGeometry,
  GreaterDepth,
  GridHelper,
  Group,
  HemisphereLight,
  IcosahedronGeometry,
  LinearSRGBColorSpace,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  ReplaceStencilOp,
  RingGeometry,
  Scene,
  Shape,
  Vector3,
  WebGLRenderer,
} from "three";
import type { BufferGeometry } from "three";
import {
  BACKDROPS,
  HEIGHT_MODES,
  OCCLUSION_MODES,
  VIEW_WIDTHS,
  VISION_MODES,
  configToSearch,
  parseConfig,
} from "./config";
import type { Config } from "./config";
import type { ShipInput } from "./kinematics";
import { occludersHiding } from "./occlusion";
import type { Occluder } from "./occlusion";
import { pickNearest, screenToPlane, worldToScreen } from "./picking";
import type { PickCandidate, ScreenPoint } from "./picking";
import { createRng } from "./rng";
import { MAX_BOLTS, SCENE_IDS, SCENE_LABELS, createWorld, stepWorld } from "./scenarios";
import type { Allegiance, Ship } from "./scenarios";
import {
  HEADING_TOLERANCE_DEG,
  PROVISIONAL_THRESHOLDS,
  chooseRangePair,
  headingErrorDeg,
  planRun,
  summarise,
} from "./trials";
import type { TrialKind, TrialRecord } from "./trials";
import {
  ELEVATIONS,
  createGreyboxCamera,
  hiddenDepthBehind,
  pixelsPerMetre,
  planeDepthScale,
  viewDirectionAtElevation,
} from "./view";

// Starter frigate bounds from `scene_manifest.json` (docking_validation), in metres.
const SHIP_LENGTH = 2.67;
const SHIP_BEAM = 1.29;
const SHIP_HEIGHT = 1.14;

const STEP = 1 / 60;
const PICK_RADIUS_PX = 20;
const DESIGNATE_MS = 700;
const TRIAL_TIMEOUT_MS = 6000;
const GAP_MS = 700;

/** Stencil value written by structures. */
const STRUCTURE = 1;

const linear = (r: number, g: number, b: number) =>
  new Color().setRGB(r, g, b, LinearSRGBColorSpace);

// Greybox placeholders, not a visual language: hull tones come from the style guide's material
// baselines, and allegiance always pairs a colour with a shape (design §5).
const HULL: Record<Allegiance, Color> = {
  self: linear(0.53, 0.57, 0.61),
  ally: linear(0.29, 0.35, 0.43),
  hostile: linear(0.3, 0.11, 0.07),
};
const SIGNAL: Record<Allegiance, Color> = {
  self: linear(1, 1, 1),
  ally: linear(0.04, 0.65, 1),
  hostile: linear(1, 0.4, 0.055),
};

function element<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  className?: string,
  text?: string,
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  if (className !== undefined) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function required(id: string): HTMLElement {
  const node = document.getElementById(id);
  if (!node) throw new Error(`greybox.html is missing #${id}`);
  return node;
}

const config = parseConfig(window.location.search);
const stage = required("stage");
const overlay = required("overlay");
const panel = required("panel");
const prompt = required("prompt");

// --- Scene -------------------------------------------------------------------------------------

const renderer = new WebGLRenderer({ antialias: true, stencil: true });
renderer.setPixelRatio(window.devicePixelRatio);
stage.prepend(renderer.domElement);
renderer.domElement.style.filter = {
  normal: "",
  grey: "grayscale(1)",
  protan: "url(#protan)",
  deutan: "url(#deutan)",
}[config.vision];

const scene = new Scene();
scene.background =
  config.backdrop === "bright" ? linear(0.42, 0.5, 0.6) : linear(0.035, 0.06, 0.11);
scene.add(new HemisphereLight(linear(0.3, 0.48, 1), linear(0.025, 0.038, 0.055), 0.6));
const starlight = new DirectionalLight(linear(0.67, 0.77, 1), 2.4);
starlight.position.set(-4, 10, 6);
scene.add(starlight);

const world = createWorld(config.scene, config.seed, config.heights);
const viewDirection = viewDirectionAtElevation(config.elevation);
const cameraTarget = new Vector3();
let viewport = { width: 1, height: 1 };
let camera = createGreyboxCamera(1, config.width, config.elevation, cameraTarget);

function occluderMesh(occluder: Occluder): Mesh<BufferGeometry, MeshStandardMaterial> {
  const material = new MeshStandardMaterial({
    color: occluder.kind === "sphere" ? linear(0.1, 0.12, 0.15) : linear(0.095, 0.135, 0.19),
    roughness: occluder.kind === "sphere" ? 0.95 : 0.5,
    metalness: occluder.kind === "sphere" ? 0 : 0.6,
    flatShading: true,
    transparent: config.occlusion === "fade",
    // Structures mark the pixels they own, so silhouettes show behind them and nowhere else.
    stencilWrite: true,
    stencilRef: STRUCTURE,
    stencilFunc: AlwaysStencilFunc,
    stencilZPass: ReplaceStencilOp,
  });
  switch (occluder.kind) {
    case "sphere": {
      const mesh = new Mesh(new IcosahedronGeometry(occluder.radius, 2), material);
      mesh.position.set(occluder.x, occluder.y, occluder.z);
      return mesh;
    }
    case "cylinder": {
      const height = occluder.yMax - occluder.yMin;
      const mesh = new Mesh(
        new CylinderGeometry(occluder.radius, occluder.radius, height, 12),
        material,
      );
      mesh.position.set(occluder.x, occluder.yMin + height / 2, occluder.z);
      return mesh;
    }
    case "box": {
      const height = occluder.yMax - occluder.yMin;
      const mesh = new Mesh(
        new BoxGeometry(occluder.halfX * 2, height, occluder.halfZ * 2),
        material,
      );
      mesh.position.set(occluder.x, occluder.yMin + height / 2, occluder.z);
      return mesh;
    }
  }
}
const occluderMeshes = world.occluders.map(occluderMesh);
for (const mesh of occluderMeshes) scene.add(mesh);

/** Extrudes a footprint given as (forward, lateral) pairs into a hull centred on its height. */
function hullGeometry(footprint: readonly (readonly [number, number])[]): BufferGeometry {
  const shape = new Shape();
  footprint.forEach(([forward, lateral], index) => {
    if (index === 0) shape.moveTo(forward, lateral);
    else shape.lineTo(forward, lateral);
  });
  return new ExtrudeGeometry(shape, { depth: SHIP_HEIGHT, bevelEnabled: false })
    .rotateX(Math.PI / 2)
    .translate(0, SHIP_HEIGHT / 2, 0);
}
const L = SHIP_LENGTH / 2;
const B = SHIP_BEAM / 2;
// Player-side ships are a wedge; hostiles are forked. Both fill the frigate's bounds.
const wedge = hullGeometry([
  [L, 0],
  [0.3, B],
  [-L, B],
  [-L, -B],
  [0.3, -B],
]);
const forked = hullGeometry([
  [L, B * 0.95],
  [L, B * 0.45],
  [0.1, B * 0.25],
  [0.1, -B * 0.25],
  [L, -B * 0.45],
  [L, -B * 0.95],
  [-0.6, -B],
  [-L, -B * 0.45],
  [-L, B * 0.45],
  [-0.6, B],
]);

function planeMarker(allegiance: Allegiance): Group {
  const material = new MeshBasicMaterial({ color: SIGNAL[allegiance], side: DoubleSide });
  const group = new Group();
  const ring = (inner: number, outer: number, segments: number) => {
    group.add(new Mesh(new RingGeometry(inner, outer, segments).rotateX(-Math.PI / 2), material));
  };
  if (allegiance === "hostile") ring(1.75, 2, 4);
  else ring(1.7, 1.86, 40);
  if (allegiance === "self") ring(2.05, 2.15, 40);
  return group;
}

interface ShipView {
  ship: Ship;
  hull: Mesh;
  silhouette: Mesh | null;
  marker: Group | null;
  drop: Mesh | null;
  hiddenFor: number;
  longestHidden: number;
}

const shipViews = world.ships.map((ship): ShipView => {
  const geometry = ship.allegiance === "hostile" ? forked : wedge;
  const hull = new Mesh(
    geometry,
    new MeshStandardMaterial({
      color: HULL[ship.allegiance],
      roughness: 0.45,
      metalness: 0.5,
      // A visible hull clears the structure mark, so a ship in front of one is drawn normally.
      stencilWrite: true,
      stencilRef: 0,
      stencilFunc: AlwaysStencilFunc,
      stencilZPass: ReplaceStencilOp,
    }),
  );
  scene.add(hull);

  let silhouette: Mesh | null = null;
  if (config.occlusion === "silhouette") {
    // Drawn only where a structure is the nearest surface and this hull lies behind it.
    silhouette = new Mesh(
      geometry,
      new MeshBasicMaterial({
        color: SIGNAL[ship.allegiance],
        depthFunc: GreaterDepth,
        depthWrite: false,
        transparent: true,
        opacity: 0.9,
        stencilWrite: true,
        stencilRef: STRUCTURE,
        stencilFunc: EqualStencilFunc,
      }),
    );
    silhouette.renderOrder = 10;
    scene.add(silhouette);
  }

  let marker: Group | null = null;
  let drop: Mesh | null = null;
  if (config.cues) {
    marker = planeMarker(ship.allegiance);
    scene.add(marker);
    if (Math.abs(ship.height) > 0.05) {
      drop = new Mesh(
        new BoxGeometry(0.07, Math.abs(ship.height), 0.07),
        new MeshBasicMaterial({ color: SIGNAL[ship.allegiance] }),
      );
      scene.add(drop);
    }
  }
  return { ship, hull, silhouette, marker, drop, hiddenFor: 0, longestHidden: 0 };
});

const playerView = shipViews.find((view) => view.ship.allegiance === "self");
if (!playerView) throw new Error("scenario has no player ship");
const player: ShipView = playerView;
const others = shipViews.filter((view) => view !== player);

const rangeRings = new Group();
if (config.cues) {
  const grid = new GridHelper(200, 40, linear(0.1, 0.16, 0.26), linear(0.06, 0.1, 0.17));
  grid.position.y = -0.02;
  scene.add(grid);
  const ringMaterial = new MeshBasicMaterial({ color: linear(0.2, 0.32, 0.5), side: DoubleSide });
  for (const radius of [10, 20]) {
    rangeRings.add(
      new Mesh(
        new RingGeometry(radius - 0.05, radius + 0.05, 96).rotateX(-Math.PI / 2),
        ringMaterial,
      ),
    );
  }
  scene.add(rangeRings);
}

// Bolts follow the style guide's rule for effects: a bright core inside a readable coloured edge.
const boltViews = Array.from({ length: MAX_BOLTS }, () => {
  const group = new Group();
  const edge = new Mesh(
    new BoxGeometry(1.1, 0.26, 0.26),
    new MeshBasicMaterial({ transparent: true, opacity: 0.6 }),
  );
  group.add(edge, new Mesh(new BoxGeometry(0.9, 0.1, 0.1), new MeshBasicMaterial()));
  group.visible = false;
  scene.add(group);
  return { group, edge };
});

// --- Input -------------------------------------------------------------------------------------

const held = new Set<string>();
const axis = (positive: string[], negative: string[]) =>
  Number(positive.some((key) => held.has(key))) - Number(negative.some((key) => held.has(key)));
const readInput = (): ShipInput => ({
  thrust: axis(["KeyW", "ArrowUp"], ["KeyS", "ArrowDown"]),
  turn: axis(["KeyD", "ArrowRight"], ["KeyA", "ArrowLeft"]),
});

// --- Trials ------------------------------------------------------------------------------------

type ActiveTrial =
  | { kind: "acquire" | "heading"; targetId: string; startedAt: number; occluded: boolean }
  | { kind: "range"; nearId: string; farId: string; ratio: number; startedAt: number };

let phase: "idle" | "trial" | "gap" | "done" = "idle";
let plan: TrialKind[] = [];
let planIndex = 0;
let retries = 0;
let active: ActiveTrial | null = null;
let gapUntil = 0;
let paused = false;
let records: TrialRecord[] = [];
let trialRng = createRng(config.seed + 1000);
let sampledSeconds = 0;
let hiddenSeconds = 0;
const brackets = new Map<string, HTMLElement>();

const shipPosition = (ship: Ship) => new Vector3(ship.state.x, ship.height, ship.state.z);
const screenOf = (ship: Ship): ScreenPoint => worldToScreen(shipPosition(ship), camera, viewport);
const isHidden = (ship: Ship) =>
  occludersHiding(shipPosition(ship), viewDirection, world.occluders).length > 0;

function onScreen(allegiance?: Allegiance): Ship[] {
  const margin = 30;
  return others
    .map((view) => view.ship)
    .filter((ship) => allegiance === undefined || ship.allegiance === allegiance)
    .filter((ship) => {
      const { x, y } = screenOf(ship);
      return (
        x > margin && y > margin && x < viewport.width - margin && y < viewport.height - margin
      );
    });
}

function setBracket(id: string, label: string): void {
  const bracket = element("div", "bracket", label);
  overlay.append(bracket);
  brackets.set(id, bracket);
}

function clearBrackets(): void {
  for (const bracket of brackets.values()) bracket.remove();
  brackets.clear();
}

function beginTrial(now: number): void {
  const kind = plan[planIndex];
  if (kind === undefined) {
    phase = "done";
    prompt.textContent = "Run complete. Results are in the panel.";
    renderResults();
    return;
  }
  const hostiles = onScreen("hostile");
  const pick = <T>(items: readonly T[]) => items[Math.floor(trialRng() * items.length)];

  if (kind === "range") {
    const pair = chooseRangePair(
      player.ship.state,
      hostiles.map((ship) => ({ id: ship.id, x: ship.state.x, z: ship.state.z })),
    );
    if (pair) {
      const nearIsOne = trialRng() < 0.5;
      setBracket(pair.near.id, nearIsOne ? "1" : "2");
      setBracket(pair.far.id, nearIsOne ? "2" : "1");
      active = {
        kind,
        nearId: pair.near.id,
        farId: pair.far.id,
        ratio: pair.ratio,
        startedAt: now,
      };
      paused = true;
      prompt.textContent = "Which hostile is nearer to your ship? Click it, or press 1 or 2.";
    }
  } else {
    const target = pick(kind === "acquire" ? hostiles : onScreen());
    if (target) {
      setBracket(target.id, "");
      active = { kind, targetId: target.id, startedAt: now, occluded: isHidden(target) };
      paused = kind === "heading";
      prompt.textContent =
        kind === "acquire"
          ? "Select the hostile that was just marked."
          : "Click where the marked ship is heading.";
    }
  }

  if (active) {
    phase = "trial";
    retries = 0;
    return;
  }
  // Nothing suitable is on screen: wait and try again, and give up on this trial eventually.
  retries += 1;
  if (retries > 10) {
    planIndex += 1;
    retries = 0;
  }
  phase = "gap";
  gapUntil = now + 500;
  prompt.textContent = "Waiting for ships to come into view…";
}

function finishTrial(now: number, outcome: TrialRecord["outcome"], value: number | null): void {
  if (!active) return;
  records.push({
    kind: active.kind,
    ms: Math.round(now - active.startedAt),
    outcome,
    value,
    occluded: active.kind === "range" ? false : active.occluded,
  });
  active = null;
  paused = false;
  clearBrackets();
  planIndex += 1;
  phase = "gap";
  gapUntil = now + GAP_MS;
  prompt.textContent = `Trial ${String(planIndex)} of ${String(plan.length)} recorded.`;
}

function answerRange(now: number, chosenId: string): void {
  if (active?.kind !== "range") return;
  finishTrial(now, chosenId === active.nearId ? "correct" : "wrong", active.ratio);
}

function handleClick(point: ScreenPoint, now: number): void {
  if (phase !== "trial" || !active) return;
  const candidates = (ships: readonly Ship[]): PickCandidate[] =>
    ships.map((ship) => ({ id: ship.id, screen: screenOf(ship) }));

  if (active.kind === "range") {
    const { nearId, farId } = active;
    const pair = others.map((view) => view.ship).filter((s) => s.id === nearId || s.id === farId);
    const picked = pickNearest(point, candidates(pair), PICK_RADIUS_PX * 1.5);
    if (picked !== null) answerRange(now, picked);
  } else if (active.kind === "acquire") {
    const picked = pickNearest(point, candidates(others.map((view) => view.ship)), PICK_RADIUS_PX);
    const outcome = picked === null ? "miss" : picked === active.targetId ? "correct" : "wrong";
    finishTrial(now, outcome, null);
  } else {
    const targetId = active.targetId;
    const target = others.find((view) => view.ship.id === targetId)?.ship;
    if (!target) return;
    const centre = screenOf(target);
    if (Math.hypot(point.x - centre.x, point.y - centre.y) < 15) return;
    const pointed = screenToPlane(point, camera, viewport, target.height);
    if (!pointed) return;
    const error = headingErrorDeg(
      target.state.heading,
      target.state.x,
      target.state.z,
      pointed.x,
      pointed.z,
    );
    finishTrial(now, error <= HEADING_TOLERANCE_DEG ? "correct" : "wrong", error);
  }
}

function startRun(): void {
  plan = planRun(config.trials, createRng(config.seed + 2000));
  trialRng = createRng(config.seed + 1000);
  planIndex = 0;
  retries = 0;
  records = [];
  active = null;
  paused = false;
  sampledSeconds = 0;
  hiddenSeconds = 0;
  for (const view of others) {
    view.hiddenFor = 0;
    view.longestHidden = 0;
  }
  clearBrackets();
  results.replaceChildren();
  phase = "gap";
  gapUntil = performance.now() + 1500;
  prompt.textContent = "Run starting. Fly with W A S D or the arrow keys.";
}

function updateTrial(now: number): void {
  if (phase === "gap" && now >= gapUntil) beginTrial(now);
  if (phase !== "trial" || !active) return;
  if (now - active.startedAt > TRIAL_TIMEOUT_MS) {
    finishTrial(now, "timeout", active.kind === "range" ? active.ratio : null);
  } else if (active.kind === "acquire" && now - active.startedAt > DESIGNATE_MS) {
    clearBrackets();
  }
}

// --- Panel -------------------------------------------------------------------------------------

type Choice = readonly [value: string, label: string];
const same = (values: readonly (string | number)[], suffix = ""): Choice[] =>
  values.map((value): Choice => [String(value), `${String(value)}${suffix}`]);

const controls: { key: keyof Config; label: string; choices: Choice[] }[] = [
  {
    key: "scene",
    label: "Scene",
    choices: SCENE_IDS.map((id): Choice => [id, SCENE_LABELS[id]]),
  },
  { key: "elevation", label: "Camera elevation", choices: same(ELEVATIONS, "°") },
  { key: "width", label: "View width", choices: same(VIEW_WIDTHS, " m") },
  {
    key: "cues",
    label: "Plane cues",
    choices: [
      ["1", "On"],
      ["0", "Off"],
    ],
  },
  { key: "occlusion", label: "Behind structures", choices: same(OCCLUSION_MODES) },
  { key: "heights", label: "Ship heights", choices: same(HEIGHT_MODES) },
  { key: "vision", label: "Colour vision", choices: same(VISION_MODES) },
  { key: "backdrop", label: "Backdrop", choices: same(BACKDROPS) },
  { key: "seed", label: "Seed", choices: same([1, 2, 3, 4, 5]) },
];

const currentParams = new URLSearchParams(configToSearch(config));
for (const control of controls) {
  const label = element("label", undefined, control.label);
  const select = element("select");
  for (const [value, text] of control.choices) {
    const option = element("option", undefined, text);
    option.value = value;
    select.append(option);
  }
  select.value = currentParams.get(control.key) ?? "";
  select.addEventListener("change", () => {
    const next = new URLSearchParams(currentParams);
    next.set(control.key, select.value);
    window.location.search = next.toString();
  });
  label.append(select);
  panel.append(label);
}

const startButton = element("button", undefined, "Start run (Space)");
startButton.addEventListener("click", startRun);
panel.append(startButton);

const readout = element("dl");
const results = element("div", "results");
panel.append(readout, results);

function row(list: HTMLElement, term: string, value: string, className?: string): void {
  list.append(element("dt", undefined, term), element("dd", className, value));
}

function renderReadout(): void {
  const scale = pixelsPerMetre(config.width, viewport.width);
  const tallest = Math.max(
    0,
    ...world.occluders.map((o) => (o.kind === "sphere" ? o.radius : o.yMax)),
  );
  readout.replaceChildren();
  row(readout, "Scale", `${scale.toFixed(1)} px/m`);
  row(readout, "Ship beam on screen", `${(SHIP_BEAM * scale).toFixed(0)} px`);
  row(readout, "Ship length on screen", `${(SHIP_LENGTH * scale).toFixed(0)} px`);
  row(readout, "Depth drawn at", `${planeDepthScale(config.elevation).toFixed(2)} × width`);
  row(
    readout,
    "Plane hidden behind tallest structure",
    `${hiddenDepthBehind(tallest, config.elevation).toFixed(1)} m`,
  );
}

function renderResults(): void {
  const summary = summarise(records);
  const longestHidden = Math.max(0, ...others.map((view) => view.longestHidden));
  const hiddenShare = sampledSeconds === 0 ? 0 : hiddenSeconds / sampledSeconds;
  const t = PROVISIONAL_THRESHOLDS;
  const verdict = (ok: boolean | null) => (ok === null ? undefined : ok ? "ok" : "over");
  const percent = (value: number | null) =>
    value === null ? "no data" : `${(value * 100).toFixed(0)}%`;
  const list = element("dl");
  row(
    list,
    `Median time to select (≤ ${String(t.acquireMedianMs)} ms)`,
    summary.acquireMedianMs === null ? "no data" : `${summary.acquireMedianMs.toFixed(0)} ms`,
    verdict(summary.acquireMedianMs === null ? null : summary.acquireMedianMs <= t.acquireMedianMs),
  );
  row(
    list,
    `Selection errors (≤ ${percent(t.acquireErrorRate)})`,
    percent(summary.acquireErrorRate),
    verdict(
      summary.acquireErrorRate === null ? null : summary.acquireErrorRate <= t.acquireErrorRate,
    ),
  );
  row(
    list,
    `Heading within ${String(HEADING_TOLERANCE_DEG)}° (≥ ${percent(t.headingCorrectRate)})`,
    percent(summary.headingCorrectRate),
    verdict(
      summary.headingCorrectRate === null
        ? null
        : summary.headingCorrectRate >= t.headingCorrectRate,
    ),
  );
  row(
    list,
    `Nearer target correct (≥ ${percent(t.rangeCorrectRate)})`,
    percent(summary.rangeCorrectRate),
    verdict(
      summary.rangeCorrectRate === null ? null : summary.rangeCorrectRate >= t.rangeCorrectRate,
    ),
  );
  row(
    list,
    "Mean range ratio shown",
    summary.rangeMeanRatio === null ? "no data" : summary.rangeMeanRatio.toFixed(2),
  );
  row(
    list,
    `Longest time a ship centre was hidden (≤ ${String(t.longestHiddenSeconds)} s)`,
    `${longestHidden.toFixed(1)} s`,
    verdict(longestHidden <= t.longestHiddenSeconds),
  );
  row(list, "Share of ship-time hidden", percent(hiddenShare));

  const download = element("button", undefined, "Download results (JSON)");
  download.addEventListener("click", () => {
    const payload = {
      generatedAt: new Date().toISOString(),
      config,
      viewport,
      summary,
      occlusion: { longestHiddenSeconds: longestHidden, hiddenShare },
      records,
    };
    const link = element("a");
    link.href = URL.createObjectURL(new Blob([JSON.stringify(payload, null, 2)]));
    link.download = `greybox-${config.scene}-${String(config.elevation)}deg.json`;
    link.click();
    URL.revokeObjectURL(link.href);
  });
  results.replaceChildren(
    element("h2", undefined, "Results"),
    list,
    element("p", "note", "Limits shown are provisional; the design leaves gate thresholds open."),
    download,
  );
}

// --- Loop --------------------------------------------------------------------------------------

function resize(): void {
  viewport = { width: stage.clientWidth, height: stage.clientHeight };
  renderer.setSize(viewport.width, viewport.height);
  camera = createGreyboxCamera(
    viewport.width / viewport.height,
    config.width,
    config.elevation,
    cameraTarget,
  );
  renderReadout();
}

function sampleOcclusion(): void {
  for (const view of others) {
    sampledSeconds += STEP;
    if (isHidden(view.ship)) {
      hiddenSeconds += STEP;
      view.hiddenFor += STEP;
      view.longestHidden = Math.max(view.longestHidden, view.hiddenFor);
    } else {
      view.hiddenFor = 0;
    }
  }
}

function syncViews(): void {
  for (const view of shipViews) {
    const { state, height } = view.ship;
    for (const mesh of [view.hull, view.silhouette]) {
      mesh?.position.set(state.x, height, state.z);
      mesh?.rotation.set(0, -state.heading, 0);
    }
    view.marker?.position.set(state.x, 0.02, state.z);
    view.drop?.position.set(state.x, height / 2, state.z);
  }
  rangeRings.position.set(player.ship.state.x, 0.01, player.ship.state.z);

  boltViews.forEach(({ group, edge }, index) => {
    const bolt = world.bolts[index];
    group.visible = bolt !== undefined;
    if (!bolt) return;
    group.position.set(bolt.x, bolt.y, bolt.z);
    group.rotation.set(0, -bolt.heading, 0);
    edge.material.color.copy(bolt.allegiance === "hostile" ? linear(1, 0.25, 0.05) : SIGNAL.ally);
  });

  if (config.occlusion === "fade") {
    const hiding = new Set(
      occludersHiding(shipPosition(player.ship), viewDirection, world.occluders),
    );
    occluderMeshes.forEach((mesh, index) => {
      mesh.material.opacity = hiding.has(index) ? 0.22 : 1;
    });
  }

  cameraTarget.set(player.ship.state.x, 0, player.ship.state.z);
  camera.position.copy(cameraTarget).addScaledVector(viewDirection, 200);
  camera.lookAt(cameraTarget);
  camera.updateMatrixWorld();

  for (const [id, bracket] of brackets) {
    const ship = others.find((view) => view.ship.id === id)?.ship;
    if (!ship) continue;
    const { x, y } = screenOf(ship);
    bracket.style.transform = `translate(${x.toFixed(1)}px, ${y.toFixed(1)}px)`;
  }
}

window.addEventListener("resize", resize);
window.addEventListener("keydown", (event) => {
  if (event.target instanceof HTMLSelectElement) return;
  held.add(event.code);
  if (event.code === "Space") {
    event.preventDefault();
    startRun();
  }
  if (active?.kind === "range" && (event.code === "Digit1" || event.code === "Digit2")) {
    const label = event.code === "Digit1" ? "1" : "2";
    const chosen = [...brackets].find(([, bracket]) => bracket.textContent === label)?.[0];
    if (chosen !== undefined) answerRange(performance.now(), chosen);
  }
});
window.addEventListener("keyup", (event) => held.delete(event.code));
renderer.domElement.addEventListener("pointerdown", (event) => {
  const bounds = renderer.domElement.getBoundingClientRect();
  handleClick({ x: event.clientX - bounds.left, y: event.clientY - bounds.top }, performance.now());
});

resize();
prompt.textContent = "Fly with W A S D or the arrow keys. Press Space to start a measured run.";

let last = performance.now();
let accumulator = 0;
renderer.setAnimationLoop((now: number) => {
  accumulator += Math.min(0.1, (now - last) / 1000);
  last = now;
  while (accumulator >= STEP) {
    accumulator -= STEP;
    if (paused) continue;
    stepWorld(world, readInput(), STEP);
    if (phase === "trial" || phase === "gap") sampleOcclusion();
  }
  syncViews();
  updateTrial(now);
  renderer.render(scene, camera);
});
