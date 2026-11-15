/**
 * An NPC ship's model from the art pipeline (Design/Art/npcs), set up as the game draws hulls
 * (view/hulls): nose along +x and centred on its height. Its root stays where the plane origin
 * is, because the pipeline fits the hull inside the collision circle about that point.
 */
import { Box3, Group, Vector3 } from "three";
import type { Object3D } from "three";

export class NpcModelError extends Error {
  override name = "NpcModelError";
}

/** The model under `root` in a GLB's scene, wrapped so its outer group takes the ship's place. */
export function npcModel(scene: Object3D, root: string): Object3D {
  const node = scene.getObjectByName(root);
  if (!node) throw new NpcModelError(`the NPC model has no ${root}`);
  node.removeFromParent();
  node.position.set(0, 0, 0);
  // Ships face +Y in Blender, -Z in glTF; this turns the nose to +x.
  node.rotation.set(0, -Math.PI / 2, 0);
  const ship = new Group();
  ship.name = root;
  ship.add(node);
  ship.updateMatrixWorld(true);
  const bounds = new Box3().setFromObject(node);
  node.position.y = -bounds.getCenter(new Vector3()).y;
  ship.updateMatrixWorld(true);
  return ship;
}
