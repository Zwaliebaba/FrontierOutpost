import { createRng } from "./rng";

export interface StarField {
  /** Unit direction of each star, three numbers per star. */
  directions: Float32Array;
  /** Linear RGB of each star, three numbers per star. */
  colours: Float32Array;
}

/** Share of stars gathered into a band across the sky. */
const BAND_SHARE = 0.4;
/** How far the band tilts from the gameplay plane, in radians. */
const BAND_TILT = 0.9;

/**
 * A seeded star field at infinity. Most stars are dim and a few are bright; a tilted band gives
 * the sky a shape, so that turning the camera has something to turn against.
 */
export function createStarField(seed: number, count: number): StarField {
  const rng = createRng(seed);
  const directions = new Float32Array(count * 3);
  const colours = new Float32Array(count * 3);
  const cosTilt = Math.cos(BAND_TILT);
  const sinTilt = Math.sin(BAND_TILT);

  for (let i = 0; i < count; i += 1) {
    const around = rng() * 2 * Math.PI;
    // Uniform over the sphere, or squeezed toward the band's equator.
    const inBand = rng() < BAND_SHARE;
    const up = inBand ? (rng() + rng() + rng() - 1.5) * 0.18 : rng() * 2 - 1;
    const flat = Math.sqrt(1 - up * up);
    const x = Math.cos(around) * flat;
    const z = Math.sin(around) * flat;
    // Tilt the band about the x axis.
    directions.set([x, up * cosTilt - z * sinTilt, up * sinTilt + z * cosTilt], i * 3);

    const brightness = 0.12 + 0.88 * rng() ** 4;
    const warmth = rng();
    colours.set(
      [
        brightness * (warmth > 0.8 ? 1 : 0.78),
        brightness * 0.86,
        brightness * (warmth > 0.8 ? 0.7 : 1),
      ],
      i * 3,
    );
  }
  return { directions, colours };
}
