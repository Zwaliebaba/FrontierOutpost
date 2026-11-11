/**
 * The starter frigate from Design/FrontierOutpost (frigate.glb), set up as the game draws hulls
 * (view/hulls): nose along +x, centred on its footprint, its landing feet at the hull's underside
 * so it rests on berth 07 where the stand-in wedge did. Its landing gear folds up into the keel
 * in flight (style guide §3); the model has no gear bays yet, so the legs slide up out of sight.
 */
import { Box3, Group, Vector3 } from "three";
import type { Object3D } from "three";
import { SHIP_HEIGHT } from "./hulls";

/** The generator's strut and foot nodes, as GLTFLoader names them ("Frigate_|_landing_strut"). */
const GEAR_NODE = /landing_(strut|foot)/;
/** How far the legs rise into the hull when they fold, metres. */
export const GEAR_LIFT = 0.3;
/** How much smaller folded legs are, so a foot does not show beside the keel. */
const GEAR_FOLDED_SCALE = 0.6;
/** How long the gear takes to fold or lower, seconds. */
export const GEAR_SECONDS = 0.6;

export class FrigateError extends Error {
  override name = "FrigateError";
}

/**
 * The frigate from frigate.glb's scene, wrapped so that its outer group takes the ship's place
 * and heading as the stand-in hulls do: the generator faces it +Y in Blender, which is -Z in
 * glTF, and leaves it where it stands on the berth.
 */
export function frigateModel(scene: Object3D): Object3D {
  const root = scene.getObjectByName("Frigate_ROOT");
  if (!root) throw new FrigateError("frigate.glb has no Frigate_ROOT");
  root.removeFromParent();
  root.position.set(0, 0, 0);
  // Turns glTF -Z, the nose, to +x.
  root.rotation.set(0, -Math.PI / 2, 0);
  const ship = new Group();
  ship.name = "frigate";
  ship.add(root);
  ship.updateMatrixWorld(true);
  const bounds = new Box3().setFromObject(root);
  const centre = bounds.getCenter(new Vector3());
  root.position.set(-centre.x, -SHIP_HEIGHT / 2 - bounds.min.y, -centre.z);
  for (const leg of gearOf(ship)) leg.userData.deployedY = leg.position.y;
  ship.updateMatrixWorld(true);
  return ship;
}

/** The landing gear's nodes in a frigate or a clone of one. */
export function gearOf(ship: Object3D): Object3D[] {
  const legs: Object3D[] = [];
  ship.traverse((node) => {
    if (GEAR_NODE.test(node.name)) legs.push(node);
  });
  return legs;
}

/** Sets the gear between folded (0) and lowered (1). */
export function setGear(legs: readonly Object3D[], lowered: number): void {
  const t = Math.min(1, Math.max(0, lowered));
  // Smoothstep: the legs start and settle gently.
  const eased = t * t * (3 - 2 * t);
  for (const leg of legs) {
    const deployedY = typeof leg.userData.deployedY === "number" ? leg.userData.deployedY : 0;
    leg.position.y = deployedY + (1 - eased) * GEAR_LIFT;
    leg.scale.setScalar(GEAR_FOLDED_SCALE + (1 - GEAR_FOLDED_SCALE) * eased);
  }
}

/** Moves `lowered` toward `target` (0 or 1) at the gear's pace, over `dt` seconds. */
export function stepGear(lowered: number, target: number, dt: number): number {
  const step = dt / GEAR_SECONDS;
  return target > lowered ? Math.min(target, lowered + step) : Math.max(target, lowered - step);
}
