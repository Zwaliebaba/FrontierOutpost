import { between, createRng } from "./rng";

/**
 * Dust in flight. Stars sit at infinity and the camera follows the ship, so in open space nothing
 * on screen moved and a pilot could not tell they were flying. A thin field of specks fixed in
 * world space slides past at the ship's speed. Each speck is a soft round dot, never stretched
 * into a streak: streaks read as lines across the view on the cruise drive. Dots are told from
 * stars three ways: they are soft where stars are crisp, they drift against the sky as the ship
 * moves, and they fade nearly out when it stops. They move only as the scenery does, never on
 * their own, so they need no reduced-motion variant. Each speck has a home in one tile; drawing
 * wraps it into the tile around the camera, so a few hundred cover any distance flown. Kept free
 * of three.js so it can be tested.
 */
export interface DustField {
  /** x, y, z of each speck's home, three numbers per speck. y is height: presentation only. */
  homes: Float32Array;
  /** Linear RGB of each speck. */
  colours: Float32Array;
  /** How wide each speck is drawn, CSS pixels. */
  sizes: Float32Array;
  /** The side of the square tile the field repeats in, metres. */
  tile: number;
}

/**
 * Wide enough to cover the view zoomed fully out (140 m across, about as deep on the plane at
 * the resting 33°), turned to any heading.
 */
export const DUST_TILE = 200;
/** About 16 specks across the view at the starting zoom: enough to read motion, no more. */
export const DUST_COUNT = 420;
/** How much of the dust shows while the ship is still: enough to find again, not to notice. */
export const DUST_AT_REST = 0.3;
/** At this speed and above the dust shows in full, metres per second: most of normal flight. */
export const DUST_FULL_SPEED = 8;
/** Below the gameplay plane, so ships and deposits draw over the dust. */
const LOWEST = -6;
const HIGHEST = -0.5;
/** The smallest and largest speck, CSS pixels: the deepest are the smallest and dimmest. */
const SMALLEST = 2;
const LARGEST = 4;

export function createDust(seed: number, count = DUST_COUNT, tile = DUST_TILE): DustField {
  const rng = createRng(seed);
  const homes = new Float32Array(count * 3);
  const colours = new Float32Array(count * 3);
  const sizes = new Float32Array(count);
  for (let i = 0; i < count; i += 1) {
    const height = between(rng, LOWEST, HIGHEST);
    homes.set([rng() * tile, height, rng() * tile], i * 3);
    // Nearer the plane is larger and brighter: a hint of depth without perspective.
    const near = (height - LOWEST) / (HIGHEST - LOWEST);
    sizes[i] = SMALLEST + (LARGEST - SMALLEST) * near;
    // Faint and cool, a little warmer now and then: dust lit by the same starlight as the hulls.
    const glow = between(rng, 0.1, 0.22) * (0.6 + 0.4 * near);
    const warm = rng() > 0.85;
    colours.set([glow * (warm ? 1 : 0.8), glow * 0.9, glow * (warm ? 0.75 : 1.05)], i * 3);
  }
  return { homes, colours, sizes, tile };
}

/** `value` wrapped into [centre − tile/2, centre + tile/2). */
function wrap(value: number, centre: number, tile: number): number {
  const from = centre - tile / 2;
  return from + ((((value - from) % tile) + tile) % tile);
}

/** Writes into `out` where each speck of `field` is drawn with the camera on (`x`, `z`). */
export function placeDust(field: DustField, x: number, z: number, out: Float32Array): void {
  const { homes, tile } = field;
  for (let i = 0; i < homes.length; i += 3) {
    out[i] = wrap(homes[i] ?? 0, x, tile);
    out[i + 1] = homes[i + 1] ?? 0;
    out[i + 2] = wrap(homes[i + 2] ?? 0, z, tile);
  }
}

/** How much of the dust shows, 0 to 1, with the ship moving at `speed` metres per second. */
export function dustOpacity(speed: number): number {
  const share = Math.min(1, Math.max(0, speed / DUST_FULL_SPEED));
  return DUST_AT_REST + (1 - DUST_AT_REST) * share;
}
