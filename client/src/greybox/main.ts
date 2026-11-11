import {
  BoxGeometry,
  BufferAttribute,
  BufferGeometry,
  CylinderGeometry,
  DoubleSide,
  GridHelper,
  Group,
  IcosahedronGeometry,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  PerspectiveCamera,
  Points,
  PointsMaterial,
  RingGeometry,
  Scene,
  Vector3,
  WebGLRenderer,
} from "three";
import type { Object3D } from "three";
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
import { LOOK_MODES, dragLook, isHome, settleLook, turnLook, zoomWidth } from "../view/look";
import type { LookState } from "../view/look";
import { centreOf, firstHit, occludersHiding } from "./occlusion";
import type { Occluder } from "./occlusion";
import { pickNearest, screenRay, screenToPlane, worldToScreen } from "../view/picking";
import type { PickCandidate, ScreenPoint } from "../view/picking";
import { createRng } from "../view/rng";
import { createStarField } from "../view/sky";
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
  PROJECTIONS,
  aimRig,
  createRig,
  hiddenDepthBehind,
  pixelsPerMetre,
  planeDepthScale,
  towardCamera,
} from "../view/view";
import { SHIP_BEAM, SHIP_LENGTH } from "../view/hulls";
import { loadFrigate } from "../outpost/models";
import { setGear } from "../view/frigate";
import { stationLights } from "../view/lights";
import {
  BOLT,
  SIGNAL,
  STRUCTURE_STENCIL,
  linear,
  shipMeshes,
  useShipModel,
  usesShipModel,
} from "../view/readability";

const STEP = 1 / 60;
const PICK_RADIUS_PX = 20;
const DESIGNATE_MS = 700;
const TRIAL_TIMEOUT_MS = 6000;
const GAP_MS = 700;

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
if (config.backdrop !== "stars") {
  scene.background =
    config.backdrop === "bright" ? linear(0.42, 0.5, 0.6) : linear(0.035, 0.06, 0.11);
}
// The game's lights and its hull model, so the readability gate measures what players see.
scene.add(...stationLights());
try {
  useShipModel(await loadFrigate());
} catch (error) {
  console.warn("The frigate did not load; player ships are the stand-in wedge.", error);
}

const world = createWorld(config.scene, config.seed, config.heights);
const cameraTarget = new Vector3();
let viewport = { width: 1, height: 1 };
// The configured width is where the view starts; the wheel zooms from there.
let viewWidth: number = config.width;
let rig = createRig(1, viewWidth, config.projection);
// The tactical view is where the camera rests; `look` is where the tester has turned it.
const homeLook: LookState = { yaw: 0, pitch: config.elevation };
let look: LookState = { ...homeLook };
aimRig(rig, cameraTarget, look.pitch, look.yaw);

// The star backdrop is at infinity: it is drawn first, through its own perspective camera that
// only copies the main camera's orientation. That keeps it behaving like a sky in both the
// orthographic and the perspective view, and unaffected by zoom.
const SKY_FOV = 50;
const sky = config.backdrop === "stars" ? new Scene() : null;
const skyCamera = new PerspectiveCamera(SKY_FOV, 1, 0.1, 10);
if (sky) {
  const layers = [
    { count: 3200, size: 1.3, dim: 0.75 },
    { count: 700, size: 2.2, dim: 1 },
    { count: 130, size: 3.4, dim: 1.5 },
  ];
  layers.forEach(({ count, size, dim }, layer) => {
    const field = createStarField(config.seed * 10 + layer + 3000, count);
    sky.add(
      new Points(
        new BufferGeometry()
          .setAttribute("position", new BufferAttribute(field.directions, 3))
          .setAttribute(
            "color",
            new BufferAttribute(
              field.colours.map((c) => c * dim),
              3,
            ),
          ),
        new PointsMaterial({ size, sizeAttenuation: false, vertexColors: true, depthTest: false }),
      ),
    );
  });
  renderer.autoClear = false;
  renderer.setClearColor(linear(0.006, 0.009, 0.02));
}

function occluderMesh(occluder: Occluder): Mesh<BufferGeometry, MeshStandardMaterial> {
  const material = new MeshStandardMaterial({
    color: occluder.kind === "sphere" ? linear(0.1, 0.12, 0.15) : linear(0.095, 0.135, 0.19),
    roughness: occluder.kind === "sphere" ? 0.95 : 0.5,
    metalness: occluder.kind === "sphere" ? 0 : 0.6,
    flatShading: true,
    transparent: config.occlusion === "fade",
    ...STRUCTURE_STENCIL,
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
  hull: Object3D;
  silhouette: Object3D | null;
  marker: Group | null;
  drop: Mesh | null;
  hiddenFor: number;
  longestHidden: number;
}

// Ships draw through the same readability rules as the expedition view (view/readability).
const shipViews = world.ships.map((ship): ShipView => {
  const meshes = shipMeshes(ship.allegiance);
  // Every greybox ship is in flight.
  setGear(meshes.gear, 0);
  const hull = meshes.hull;
  scene.add(hull);
  const silhouette = config.occlusion === "silhouette" ? meshes.silhouette : null;
  if (silhouette) scene.add(silhouette);

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
const screenOf = (ship: Ship): ScreenPoint =>
  worldToScreen(shipPosition(ship), rig.camera, viewport);
const hiding = (ship: Ship) => {
  const position = shipPosition(ship);
  return occludersHiding(position, towardCamera(rig.camera, position), world.occluders);
};
const isHidden = (ship: Ship) => hiding(ship).length > 0;

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
    cameraYawDeg: Math.round((look.yaw * 180) / Math.PI),
    cameraPitchDeg: Math.round(look.pitch),
    viewWidth: Math.round(viewWidth * 10) / 10,
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

const candidates = (ships: readonly Ship[]): PickCandidate[] =>
  ships.map((ship) => ({ id: ship.id, screen: screenOf(ship) }));

// Outside a measured run, clicking a ship or a structure makes the camera centre on it and
// orbit it. The tester's own ship keeps flying, so any steering input takes the camera back:
// otherwise the ship could be steered while it is off screen.
interface Focus {
  label: string;
  position: () => Vector3;
}
let focus: Focus | null = null;
let focusBlend = 0;
const focusPoint = new Vector3();

function focusOn(point: ScreenPoint): void {
  const picked = pickNearest(point, candidates(others.map((view) => view.ship)), PICK_RADIUS_PX);
  const ship = others.find((view) => view.ship.id === picked)?.ship;
  if (ship) {
    focus = { label: ship.id, position: () => shipPosition(ship) };
    return;
  }
  const ray = screenRay(point, rig.camera, viewport);
  const structure = world.occluders[firstHit(ray.origin, ray.direction, world.occluders)];
  if (structure) {
    const centre = centreOf(structure);
    const position = new Vector3(centre.x, Math.max(0, centre.y), centre.z);
    focus = { label: "a structure", position: () => position };
  } else {
    focus = null;
  }
}

function updateFocus(dt: number): void {
  const input = readInput();
  const steering = input.thrust !== 0 || input.turn !== 0;
  if (steering || held.has("KeyR") || phase === "trial" || phase === "gap") focus = null;
  if (focus) focusPoint.copy(focus.position());
  const goal = focus ? 1 : 0;
  focusBlend += (goal - focusBlend) * (1 - Math.exp(-7 * dt));
  if (Math.abs(goal - focusBlend) < 0.001) focusBlend = goal;
}

function handleClick(point: ScreenPoint, now: number): void {
  if (phase === "idle" || phase === "done") {
    if (config.look !== "fixed") focusOn(point);
    return;
  }
  if (phase !== "trial" || !active) return;

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
    const pointed = screenToPlane(point, rig.camera, viewport, target.height);
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
  {
    key: "projection",
    label: "Projection",
    choices: PROJECTIONS.map((id): Choice => [
      id,
      { ortho: "Orthographic", narrow: "Perspective, narrow", wide: "Perspective, wide" }[id],
    ]),
  },
  {
    key: "look",
    label: "Look around",
    choices: LOOK_MODES.map((id): Choice => [
      id,
      { fixed: "Off (fixed view)", hold: "Hold, springs back", free: "Free, stays put" }[id],
    ]),
  },
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
const cameraReadout = element("dl");
const results = element("div", "results");
panel.append(readout, cameraReadout, results);

function row(list: HTMLElement, term: string, value: string, className?: string): void {
  list.append(element("dt", undefined, term), element("dd", className, value));
}

function renderReadout(): void {
  const scale = pixelsPerMetre(viewWidth, viewport.width);
  const tallest = Math.max(
    0,
    ...world.occluders.map((o) => (o.kind === "sphere" ? o.radius : o.yMax)),
  );
  readout.replaceChildren();
  row(readout, "Visible width", `${viewWidth.toFixed(0)} m`);
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
      // Which hull player-side ships were: results from before the frigate measured the wedge.
      hull: usesShipModel() ? "frigate" : "wedge",
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
  skyCamera.aspect = viewport.width / viewport.height;
  skyCamera.updateProjectionMatrix();
  rig = createRig(viewport.width / viewport.height, viewWidth, config.projection);
  aimRig(rig, cameraTarget, look.pitch, look.yaw);
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
    edge.material.color.copy(bolt.allegiance === "hostile" ? BOLT.hostile : BOLT.pilot);
  });

  cameraTarget.set(player.ship.state.x, 0, player.ship.state.z).lerp(focusPoint, focusBlend);
  aimRig(rig, cameraTarget, look.pitch, look.yaw);

  if (config.occlusion === "fade") {
    const inFront = new Set(hiding(player.ship));
    occluderMeshes.forEach((mesh, index) => {
      mesh.material.opacity = inFront.has(index) ? 0.22 : 1;
    });
  }

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

// Left button answers trials. Right button, or Q and E, turns the camera when looking is allowed.
const canvas = renderer.domElement;
let dragFrom: ScreenPoint | null = null;
canvas.addEventListener("contextmenu", (event) => {
  event.preventDefault();
});
canvas.addEventListener("pointerdown", (event) => {
  if (event.button === 0) {
    const bounds = canvas.getBoundingClientRect();
    handleClick(
      { x: event.clientX - bounds.left, y: event.clientY - bounds.top },
      performance.now(),
    );
  } else if (event.button === 2 && config.look !== "fixed") {
    dragFrom = { x: event.clientX, y: event.clientY };
    canvas.setPointerCapture(event.pointerId);
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

// The wheel zooms in every look mode: the design already allows zoom within the fixed view (§5).
canvas.addEventListener(
  "wheel",
  (event) => {
    event.preventDefault();
    viewWidth = zoomWidth(viewWidth, event.deltaY, event.deltaMode);
    resize();
  },
  { passive: false },
);

const KEY_TURN_RATE = 1.6;
function updateLook(dt: number): void {
  if (held.has("KeyR") && viewWidth !== config.width) {
    viewWidth = config.width;
    resize();
  }
  if (config.look !== "fixed") {
    if (held.has("KeyR")) look = { ...homeLook };
    const keyTurn = axis(["KeyE"], ["KeyQ"]);
    if (keyTurn !== 0) look = turnLook(look, keyTurn * KEY_TURN_RATE * dt);
    else if (config.look === "hold" && !dragFrom) look = settleLook(look, homeLook, dt);
  }

  const angle = isHome(look, homeLook)
    ? "Tactical view"
    : `Turned ${((look.yaw * 180) / Math.PI).toFixed(0)}°, elevation ${look.pitch.toFixed(0)}°`;
  const text = focus ? `${angle}, centred on ${focus.label}` : angle;
  if (cameraReadout.dataset.text !== text) {
    cameraReadout.dataset.text = text;
    cameraReadout.replaceChildren();
    row(cameraReadout, "Camera", text);
  }
}

const IDLE_PROMPT = [
  "W A S D or arrows to fly",
  ...(config.look === "fixed"
    ? []
    : [
        config.look === "hold"
          ? "hold right mouse or Q / E to look"
          : "right-drag or Q / E to turn",
        "click a ship or structure to centre on it",
      ]),
  "scroll to zoom",
  "R to reset the view",
  "Space to start a run",
].join(" · ");

resize();
prompt.textContent = IDLE_PROMPT;

let last = performance.now();
let accumulator = 0;
renderer.setAnimationLoop((now: number) => {
  const frame = Math.min(0.1, (now - last) / 1000);
  accumulator += frame;
  last = now;
  updateLook(frame);
  updateFocus(frame);
  while (accumulator >= STEP) {
    accumulator -= STEP;
    if (paused) continue;
    stepWorld(world, readInput(), STEP);
    if (phase === "trial" || phase === "gap") sampleOcclusion();
  }
  syncViews();
  updateTrial(now);
  if (sky) {
    renderer.clear();
    skyCamera.quaternion.copy(rig.camera.quaternion);
    renderer.render(sky, skyCamera);
    renderer.clearDepth();
  }
  renderer.render(scene, rig.camera);
});
