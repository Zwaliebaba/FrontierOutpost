import type { Object3D } from "three";
import { STATION_DROP } from "../outpost/placement";
import { modelOccluder } from "./occlusion";
import type { Occluder, Primitive } from "./occlusion";

/**
 * The home station for the greybox's station scene. Gate 2 is measured around the real station
 * silhouette (design §9), so the scene uses `station.glb` ({@link realStation}); this coarse
 * proxy, from the primitives in `Design/FrontierOutpost/create_scene.py` (hub, crown, masts,
 * arms, pods, landing apron, spine), stands in only if the model cannot load.
 *
 * Dimensions below are in the generator's station-local Blender space (Z up, metres). Both stand
 * where the game stands the station: its origin {@link STATION_DROP} below the gameplay plane, so
 * the deck lies half a hull below it, where a ship's underside rests.
 */
const DECK_HEIGHT = STATION_DROP;

/** The real station, at the game's height and unturned, as an occluder. */
export function realStation(station: Object3D): Occluder {
  station.position.set(0, -STATION_DROP, 0);
  station.rotation.set(0, 0, 0);
  return modelOccluder(station);
}

const cylinder = (x: number, y: number, radius: number, zMin: number, zMax: number): Primitive => ({
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
): Primitive => ({
  kind: "box",
  x,
  z: -y,
  halfX: sizeX / 2,
  halfZ: sizeY / 2,
  yMin: zMin - DECK_HEIGHT,
  yMax: zMax - DECK_HEIGHT,
});

export function stationOccluders(): Primitive[] {
  const parts: Primitive[] = [
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
