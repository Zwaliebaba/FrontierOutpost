import { ExtrudeGeometry, Shape } from "three";
import type { BufferGeometry } from "three";

/** Starter frigate bounds from `scene_manifest.json` (docking_validation), in metres. */
export const SHIP_LENGTH = 2.67;
export const SHIP_BEAM = 1.29;
export const SHIP_HEIGHT = 1.14;

/**
 * Extrudes a footprint given as (forward, lateral) pairs into a hull centred on its height.
 * Forward is +x, so a hull rotated by -heading about y faces its heading on the plane.
 */
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

/** Player-side ships are a wedge. Greybox stand-in until hull models load (plan M5). */
export function wedgeHull(): BufferGeometry {
  return hullGeometry([
    [L, 0],
    [0.3, B],
    [-L, B],
    [-L, -B],
    [0.3, -B],
  ]);
}

/** Hostile ships are forked, so allegiance reads by shape as well as colour (design §5). */
export function forkedHull(): BufferGeometry {
  return hullGeometry([
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
}
