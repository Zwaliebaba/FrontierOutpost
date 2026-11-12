/**
 * The outpost's models, authored by Design/FrontierOutpost/create_scene.py (plan 5.1): the
 * station with its construction sockets, and the facility modules at each tier. Node names are
 * the generator's: `Station_ROOT`, `Socket_A`, `Facility_<facility>_T<tier>`.
 */
import { Matrix4, Mesh } from "three";
import type { BufferGeometry, Material, Object3D } from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { mergeGeometries } from "three/addons/utils/BufferGeometryUtils.js";
import facilitiesUrl from "../../../Design/FrontierOutpost/facilities.glb?url";
import frigateUrl from "../../../Design/FrontierOutpost/frigate.glb?url";
import stationUrl from "../../../Design/FrontierOutpost/station.glb?url";
import { frigateModel, gearOf } from "../view/frigate";

export interface OutpostModels {
  /** The station, its root at the origin. Sockets are among its descendants. */
  readonly station: Object3D;
  /** Each socket's anchor, by the game's socket identifier: "a", "b". */
  readonly sockets: ReadonlyMap<string, Object3D>;
  /** Each facility module, its root at the origin, by {@link facilityKey}. */
  readonly facilities: ReadonlyMap<string, Object3D>;
}

/** The outpost's models and the starter frigate (view/frigate), which waits on its berth. */
export interface LoadedModels extends OutpostModels {
  readonly frigate: Object3D;
}

export function facilityKey(facility: string, tier: number): string {
  return `${facility}:${String(tier)}`;
}

const SOCKET = /^Socket_([A-Z])$/;
const FACILITY = /^Facility_([a-z_]+)_T(\d+)$/;

export class ModelError extends Error {
  override name = "ModelError";
}

/**
 * Replaces the meshes under `root` with one mesh per material, each a child of `root`.
 *
 * The scene is authored as many small objects: the station is 379 meshes in 9 materials, and
 * every mesh is a draw call, every frame, though none of them ever moves. Merged, it is the
 * same triangles in ten calls (style guide section 9: combine parts when preparing engine
 * assets). Nodes that are not meshes, the socket anchors among them, stay where they are.
 * `preserved` keeps moving parts such as landing gear independently transformable.
 *
 * Left alone, and still drawn singly: a mesh with several materials, with children of its own,
 * or mirrored by a negative scale, whose faces baking would turn inside out. The committed
 * exports have none of these.
 */
export function mergeByMaterial(
  root: Object3D,
  preserved: ReadonlySet<Object3D> = new Set(),
): void {
  root.updateMatrixWorld(true);
  const toRoot = root.matrixWorld.clone().invert();
  const batches = new Map<string, { material: Material; meshes: Mesh[] }>();
  root.traverse((node) => {
    if (!(node instanceof Mesh) || node.children.length > 0 || preserved.has(node)) return;
    const material = node.material as Material | Material[];
    if (Array.isArray(material) || node.matrixWorld.determinant() < 0) return;
    // Geometries merge only when they carry the same attributes.
    const geometry = node.geometry as BufferGeometry;
    const layout = `${Object.keys(geometry.attributes).sort().join(",")}:${String(geometry.index !== null)}`;
    const key = `${material.uuid}:${layout}`;
    const batch = batches.get(key) ?? { material, meshes: [] };
    batch.meshes.push(node as Mesh);
    batches.set(key, batch);
  });
  for (const { material, meshes } of batches.values()) {
    if (meshes.length < 2) continue;
    const parts = meshes.map((mesh) =>
      mesh.geometry.clone().applyMatrix4(new Matrix4().multiplyMatrices(toRoot, mesh.matrixWorld)),
    );
    const merged = mergeGeometries(parts) as BufferGeometry | null;
    for (const part of parts) part.dispose();
    if (!merged) continue;
    for (const mesh of meshes) {
      mesh.removeFromParent();
      mesh.geometry.dispose();
    }
    const whole = new Mesh(merged, material);
    whole.name = `merged:${material.name}`;
    root.add(whole);
  }
  root.updateMatrixWorld(true);
}

/**
 * Finds the station, its sockets and the facility modules in the two loaded scenes, moves each
 * root to the origin (the GLBs keep their places in the Blender scene), and merges each one's
 * meshes by material.
 */
export function indexModels(stationScene: Object3D, facilitiesScene: Object3D): OutpostModels {
  const station = stationScene.getObjectByName("Station_ROOT");
  if (!station) throw new ModelError("station.glb has no Station_ROOT");
  station.position.set(0, 0, 0);
  station.updateMatrixWorld(true);
  const sockets = new Map<string, Object3D>();
  station.traverse((node) => {
    const match = SOCKET.exec(node.name);
    if (match?.[1]) sockets.set(match[1].toLowerCase(), node);
  });
  mergeByMaterial(station);
  const facilities = new Map<string, Object3D>();
  for (const node of [...facilitiesScene.children]) {
    const match = FACILITY.exec(node.name);
    if (!match?.[1] || !match[2]) continue;
    node.position.set(0, 0, 0);
    mergeByMaterial(node);
    facilities.set(facilityKey(match[1], Number(match[2])), node);
  }
  return { station, sockets, facilities };
}

/** Parses a GLB's bytes into its scene. */
export function parseGlb(bytes: ArrayBuffer): Promise<Object3D> {
  return new Promise((resolve, reject) => {
    new GLTFLoader().parse(
      bytes,
      "",
      (gltf) => {
        resolve(gltf.scene);
      },
      (error) => {
        reject(error instanceof Error ? error : new ModelError("the GLB could not be parsed"));
      },
    );
  });
}

async function fetchGlb(url: string): Promise<Object3D> {
  const response = await fetch(url);
  if (!response.ok) throw new ModelError(`${url}: HTTP ${String(response.status)}`);
  return parseGlb(await response.arrayBuffer());
}

let loading: Promise<LoadedModels> | null = null;

/** Loads the three GLBs once; later calls share the result. */
export function loadOutpostModels(): Promise<LoadedModels> {
  loading ??= Promise.all([fetchGlb(stationUrl), fetchGlb(facilitiesUrl), loadFrigate()]).then(
    ([station, facilities, frigate]) => ({ ...indexModels(station, facilities), frigate }),
  );
  return loading;
}

/** Loads the starter frigate on its own, for the greybox. */
export async function loadFrigate(): Promise<Object3D> {
  const ship = frigateModel(await fetchGlb(frigateUrl));
  // Four roster ships should not multiply every authored plate into a draw call.
  // Landing gear keeps its own transforms so deployment still animates.
  mergeByMaterial(ship, new Set(gearOf(ship)));
  return ship;
}
