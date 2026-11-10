import type { Occluder } from "./occlusion";

/**
 * Coarse proxy of the home station, from the primitives in
 * `Design/FrontierOutpost/create_scene.py` (hub, crown, masts, arms, pods, landing apron, spine).
 *
 * Dimensions below are in the generator's station-local Blender space (Z up, metres). The dock's
 * inset deck (z = 0.83) is taken as the gameplay plane, which is an assumption: neither design
 * document defines the plane datum yet.
 */
const DECK_HEIGHT = 0.83;

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

export function stationOccluders(): Occluder[] {
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

/** Radius of the largest asteroid in the reference scene. */
export const LARGE_ASTEROID_RADIUS = 4.6;
