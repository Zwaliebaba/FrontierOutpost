import { between, createRng } from "./rng";

/**
 * Dust in flight. Stars sit at infinity and the camera follows the ship, so in open space nothing
 * on screen moved and a pilot could not tell they were flying. A thin field of specks fixed in
 * world space slides past at the ship's speed. Drawn as dots they read as more stars, so each is
 * a short streak trailing against the ship's motion, as long as the ship is fast and gone when it
 * stops: it moves only as the scenery does, never on its own, so it needs no reduced-motion
 * variant. Each speck has a home in one tile; drawing wraps it into the tile around the camera,
 * so a few hundred cover any distance flown. Kept free of three.js so it can be tested.
 */
export interface DustField {
  /** x, y, z of each speck's home, three numbers per speck. y is height: presentation only. */
  homes: Float32Array;
  /** Linear RGB of each speck. */
  colours: Float32Array;
  /** The side of the square tile the field repeats in, metres. */
  tile: number;
}

/**
 * Wide enough to cover the view zoomed fully out (140 m across, about as deep on the plane at
 * the resting 33°), turned to any heading.
 */
export const DUST_TILE = 200;
/** About 25 specks across the view at the starting zoom: enough to read motion, no more. */
export const DUST_COUNT = 600;
/** How far back a streak reaches: where the speck was this long ago, seen from the ship. */
export const STREAK_SECONDS = 0.2;
/** The longest streak, metres, so the cruise drive and a jump's cut do not draw lines. */
export const STREAK_MOST = 2.4;
/** Below the gameplay plane, so ships and deposits draw over the dust. */
const LOWEST = -6;
const HIGHEST = -0.5;

export function createDust(seed: number, count = DUST_COUNT, tile = DUST_TILE): DustField {
  const rng = createRng(seed);
  const homes = new Float32Array(count * 3);
  const colours = new Float32Array(count * 3);
  for (let i = 0; i < count; i += 1) {
    homes.set([rng() * tile, between(rng, LOWEST, HIGHEST), rng() * tile], i * 3);
    // Faint and cool, a little warmer now and then: dust lit by the same starlight as the hulls.
    const glow = between(rng, 0.18, 0.45);
    const warm = rng() > 0.85;
    colours.set([glow * (warm ? 1 : 0.8), glow * 0.9, glow * (warm ? 0.75 : 1.05)], i * 3);
  }
  return { homes, colours, tile };
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

/**
 * Writes into `out` one streak per speck, head then tail, six numbers each, with the camera on
 * (`x`, `z`) and moving at (`vx`, `vz`) metres per second. The tail lies behind the head against
 * the motion, as the speck was a moment ago relative to the ship.
 */
export function streakDust(
  field: DustField,
  x: number,
  z: number,
  vx: number,
  vz: number,
  out: Float32Array,
): void {
  const speed = Math.hypot(vx, vz);
  const length = Math.min(speed * STREAK_SECONDS, STREAK_MOST);
  const dx = speed > 0 ? (vx / speed) * length : 0;
  const dz = speed > 0 ? (vz / speed) * length : 0;
  const { homes, tile } = field;
  for (let i = 0, o = 0; i < homes.length; i += 3, o += 6) {
    const hx = wrap(homes[i] ?? 0, x, tile);
    const hy = homes[i + 1] ?? 0;
    const hz = wrap(homes[i + 2] ?? 0, z, tile);
    out.set([hx, hy, hz, hx + dx, hy, hz + dz], o);
  }
}
