import type { Occluder } from "./occlusion";
import { tacticalViewDirection } from "../camera";

/**
 * Station scales against the Stage 1 station, whose span is 14.6 m pod to pod. 1 is that
 * station, as the game draws it today. 2.2 is the M9 ring as authored in
 * `Design/Art/station/exterior` (32.4 m across), and 3 is the same ring scaled by 3/2.2. Plan
 * M9 asks for two to three times larger; the greybox block at each scale chooses (plan 9.1).
 */
export const STATION_SCALES = [1, 2.2, 3] as const;
export type StationScale = (typeof STATION_SCALES)[number];

/**
 * Which way the ring's hangar mouth faces. In play the mouth faces the site, the way ships
 * launch, so its bearing from the camera depends on where the site lies.
 */
export const MOUTH_FACINGS = ["site", "camera", "side", "away"] as const;
export type MouthFacing = (typeof MOUTH_FACINGS)[number];

export interface StationSetup {
  scale: StationScale;
  /** Ignored at scale 1: the Stage 1 station has berths, not a hangar. */
  facing: MouthFacing;
}

export const DEFAULT_STATION: StationSetup = { scale: 2.2, facing: "site" };

/** Radius of the largest asteroid in the reference scene. */
export const LARGE_ASTEROID_RADIUS = 4.6;

// --- The Stage 1 station -----------------------------------------------------------------------

/**
 * The Stage 1 station's dock deck (z = 0.83) is taken as the gameplay plane, which is an
 * assumption: neither design document defines the plane datum yet.
 */
const DECK_HEIGHT = 0.83;
/** How far the Stage 1 station reaches from its centre: a habitat pod's outer edge. */
const LEGACY_REACH = 5.2 + 2.12;

const cylinder = (x: number, y: number, radius: number, zMin: number, zMax: number): Occluder => ({
  kind: "cylinder",
  x,
  z: -y,
  radius,
  yMin: zMin - DECK_HEIGHT,
  yMax: zMax - DECK_HEIGHT,
});

const box = (
  x: number,
  y: number,
  sizeX: number,
  sizeY: number,
  zMin: number,
  zMax: number,
): Occluder => ({
  kind: "box",
  x,
  z: -y,
  halfX: sizeX / 2,
  halfZ: sizeY / 2,
  yMin: zMin - DECK_HEIGHT,
  yMax: zMax - DECK_HEIGHT,
});

/**
 * Coarse proxy of the Stage 1 station, from the primitives in
 * `Design/FrontierOutpost/create_scene.py` (hub, crown, masts, arms, pods, landing apron, spine).
 * Dimensions are in the generator's station-local Blender space (Z up, metres). The baseline
 * the larger station is measured against.
 */
export function legacyStationOccluders(): Occluder[] {
  const parts: Occluder[] = [
    // Hub: lower collar to rooftop armour.
    cylinder(0, 0, 2.4, -0.275, 4.68),
    // Landing apron: neck and slab, top at the deck.
    box(0, -2.6, 1.65, 2, -0.03, 0.83),
    box(0, -4.3, 4.9, 4, 0.34, 0.83),
    // Pod antenna (right pod only).
    box(5.2, 0.5, 0.1, 0.1, 2.98, 5.05),
  ];
  for (const side of [-1, 1]) {
    parts.push(box(side * 3.45, 0.5, 3.1, 1.35, 1.325, 2.475)); // pressurised bridge
    parts.push(cylinder(side * 5.2, 0.5, 2.12, 0.35, 2.98)); // habitat pod
  }
  // Rooftop masts: (x, y, antenna length above z = 4.9).
  for (const [x, y, length] of [
    [0, 0.3, 5.3],
    [-0.55, 0.6, 3.6],
    [0.48, 0.45, 4.2],
    [0.65, -0.1, 2.6],
  ] as const) {
    parts.push(box(x, y, 0.12, 0.12, 4.68, 4.9 + length));
  }
  // Suspended spine tiers: (centre z, radius, depth). Below the plane, so they never hide ships.
  for (const [z, radius, depth] of [
    [-0.8, 1.4, 1.2],
    [-1.9, 0.95, 1.1],
    [-3, 0.65, 1.3],
    [-4.05, 0.33, 0.9],
  ] as const) {
    parts.push(cylinder(0, 0, radius, z - depth / 2, z + depth / 2));
  }
  return parts;
}

// --- The M9 ring -------------------------------------------------------------------------------

/**
 * The ring as `Design/Art/tools/model_station.py` and `refine_station.py` author it, in its own
 * Blender space (Z up, metres, the mouth on +Y). Its hangar deck and mouth sit at z = 0, so
 * that is the gameplay plane. Each primitive bounds a group of the art's meshes; trim, lamps,
 * stencils and windows are inside them. The tell plinths are left out: they stand 0.3 m above
 * the ring's top, and what stands on them depends on the outpost.
 */
const RING_AUTHORED_SCALE = 2.2;
const RING_SEGMENTS = 32;
const RING_INNER = 12.5;
const RING_OUTER = 16.3;
const RING_TOP = 1.29;
const RING_BOTTOM = -2.05;
/** The `Hangar_Mouth` anchor's distance from the centre, and its opening. */
const MOUTH_Y = 14;
export const MOUTH_WIDTH = 8;
export const MOUTH_HEIGHT = 3.5;
/** The art contract's straight approach beyond the mouth. Ships do not grow with the station. */
export const LANE_LENGTH = 30;
/** Middle of the hangar, under its roof. */
const HANGAR_DEEP_Y = 9.3;

type Part =
  | {
      kind: "box";
      x: number;
      y: number;
      sizeX: number;
      sizeY: number;
      zMin: number;
      zMax: number;
      /** Turn about Blender's Z, counter-clockwise seen from above, in radians. */
      angle: number;
    }
  | { kind: "cylinder"; x: number; y: number; radius: number; zMin: number; zMax: number };

const block = (
  x: number,
  y: number,
  sizeX: number,
  sizeY: number,
  zMin: number,
  zMax: number,
  angle = 0,
): Part => ({ kind: "box", x, y, sizeX, sizeY, zMin, zMax, angle });

const drum = (x: number, y: number, radius: number, zMin: number, zMax: number): Part => ({
  kind: "cylinder",
  x,
  y,
  radius,
  zMin,
  zMax,
});

function ringParts(): Part[] {
  const parts: Part[] = [];
  const half = Math.PI / RING_SEGMENTS;
  for (let i = 0; i < RING_SEGMENTS; i += 1) {
    const mid = (2 * i + 1) * half;
    // The art's own rule for the slot the hangar's straight lane runs through.
    if (Math.sin(mid) > 0 && Math.abs(MOUTH_Y * Math.cos(mid)) < 5.3) continue;
    // One turned box per segment, from the segment's inner corners to its outer face and as
    // wide as its outer chord, so neighbouring boxes overlap rather than leave slits.
    const inner = RING_INNER * Math.cos(half);
    const radius = (inner + RING_OUTER) / 2;
    parts.push(
      block(
        radius * Math.cos(mid),
        radius * Math.sin(mid),
        RING_OUTER - inner,
        2 * RING_OUTER * Math.sin(half),
        RING_BOTTOM,
        RING_TOP,
        mid,
      ),
    );
  }
  parts.push(
    // Hub: lower hub, pressure drum, its armoured collar, the command drum and shoulder.
    drum(0, 0, 7.6, -4.2, -0.6),
    drum(0, -1, 7.3, -2, 1.65),
    drum(0, -1, 7.5, 1.65, 2.45),
    drum(0, -1, 5, 2.45, 3.95),
    // Command bridge and the tower's foundation; the tower to its crown; aerial and beacon.
    block(0, -1.5, 4.5, 3.2, 3.95, 4.75),
    block(0, -1.3, 2.3, 1.8, 4.75, 9.05),
    drum(0, -1.3, 0.075, 9.05, 10.23),
    // The two flanking towers.
    block(-1.65, -1.1, 0.75, 1.25, 4.3, 6.55),
    block(1.65, -1.1, 0.75, 1.25, 4.3, 6),
    // Underslung core: below the plane, so it hides only ships flown below it.
    drum(0, 0, 4, -6, -4),
    drum(0, 0, 2.7, -8, -6),
    drum(0, 0, 1.65, -9.2, -8),
    drum(0, 0, 0.7, -10.8, -9.2),
    // The radial bridge toward -Y; the two toward ±X lie inside the service bridges.
    block(0, -9.5, 1.65, 6.3, -0.63, 0.46),
    // Hangar roof with its lintel, ribs and sills; back bulkhead; deck and threshold.
    block(0, 9.275, 10.6, 8.95, 3.7, 4.53),
    block(0, 5.85, 8.2, 0.2, 0, 3.6),
    block(0, 11.175, 8.2, 10.35, -0.45, -0.03),
  );
  for (const side of [-1, 1]) {
    parts.push(
      // Radial service bridge with its armour.
      block(side * 9.8, -1, 7, 3.1, -0.8, 0.83),
      // Equipment on the ring where the bastions were, and its short aerial.
      block(side * 12.8, 5, 2.1, 2.1, 1.225, 2.26),
      drum(side * 13.5, 5.6, 0.1, 1.525, 3.175),
      // Hangar cheek with its segments, and the portal frame and jamb at the mouth.
      block(side * 5.15, 9, 2.1, 9, 0, 3.2),
      block(side * 4.675, 13.5, 1.15, 0.8, -0.33, 3.85),
    );
  }
  return parts;
}

/** Compass bearing, atan2(z, x), from the scene toward the resting camera. */
const CAMERA_BEARING = (() => {
  const toward = tacticalViewDirection();
  return Math.atan2(toward.z, toward.x);
})();

/** Bearing on the plane, atan2(z, x), that the hangar mouth faces. */
export function mouthBearing(facing: MouthFacing): number {
  switch (facing) {
    // Both of frontier-sim's site templates lie along +X from home.
    case "site":
      return 0;
    case "camera":
      return CAMERA_BEARING;
    // To the right of the screen at the resting view.
    case "side":
      return CAMERA_BEARING - Math.PI / 2;
    case "away":
      return CAMERA_BEARING + Math.PI;
  }
}

/** A ring part in Three.js space, `size` times its authored size and turned by `turn`. */
function place(part: Part, size: number, turn: number): Occluder {
  // Blender's (x, y) is Three.js's (x, -y); the whole station then turns on the plane.
  const localX = part.x * size;
  const localZ = -part.y * size;
  const x = localX * Math.cos(turn) - localZ * Math.sin(turn);
  const z = localX * Math.sin(turn) + localZ * Math.cos(turn);
  const yMin = part.zMin * size;
  const yMax = part.zMax * size;
  if (part.kind === "cylinder") {
    return { kind: "cylinder", x, z, radius: part.radius * size, yMin, yMax };
  }
  // A Blender turn of `angle` is a bearing of -angle in Three.js, before the station turns.
  return {
    kind: "box",
    x,
    z,
    halfX: (part.sizeX * size) / 2,
    halfZ: (part.sizeY * size) / 2,
    yMin,
    yMax,
    yaw: turn - part.angle,
  };
}

/** The mouth is on Blender's +Y, which is a bearing of -90° in Three.js before any turn. */
const turnFor = (facing: MouthFacing) => mouthBearing(facing) + Math.PI / 2;
const sizeOf = (scale: StationScale) => scale / RING_AUTHORED_SCALE;

/** The station's structure proxies at the dock, centred on the origin. */
export function stationOccluders(setup: StationSetup): Occluder[] {
  if (setup.scale === 1) return legacyStationOccluders();
  const size = sizeOf(setup.scale);
  const turn = turnFor(setup.facing);
  return ringParts().map((part) => place(part, size, turn));
}

/** How far the station reaches from its centre on the plane, in metres. */
export function stationReach(setup: StationSetup): number {
  return setup.scale === 1 ? LEGACY_REACH : RING_OUTER * sizeOf(setup.scale);
}

/** The hangar along its axis, in metres from the station's centre. */
export interface Hangar {
  /** Bearing of the axis, from the centre out through the mouth. */
  bearing: number;
  /** Distance to the mouth. */
  mouth: number;
  /** Distance to the middle of the hangar, under its roof. */
  deep: number;
  /** Half the mouth's width. */
  halfWidth: number;
}

/** The ring's hangar, or null for the Stage 1 station, which docks at berths. */
export function hangarOf(setup: StationSetup): Hangar | null {
  if (setup.scale === 1) return null;
  const size = sizeOf(setup.scale);
  return {
    bearing: mouthBearing(setup.facing),
    mouth: MOUTH_Y * size,
    deep: HANGAR_DEEP_Y * size,
    halfWidth: (MOUTH_WIDTH / 2) * size,
  };
}

/** The point `along` metres out on the hangar's axis and `lateral` metres across it. */
export function onHangarAxis(hangar: Hangar, along: number, lateral: number): [number, number] {
  const cos = Math.cos(hangar.bearing);
  const sin = Math.sin(hangar.bearing);
  return [cos * along - sin * lateral, sin * along + cos * lateral];
}
