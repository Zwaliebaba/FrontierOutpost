/**
 * The outpost's models, authored by Design/FrontierOutpost/create_scene.py (plan 5.1): the
 * station with its construction sockets, and the facility modules at each tier. Node names are
 * the generator's: `Station_ROOT`, `Socket_A`, `Facility_<facility>_T<tier>`.
 */
import type { Object3D } from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import facilitiesUrl from "../../../Design/FrontierOutpost/facilities.glb?url";
import frigateUrl from "../../../Design/FrontierOutpost/frigate.glb?url";
import stationUrl from "../../../Design/FrontierOutpost/station.glb?url";
import { frigateModel } from "../view/frigate";

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
 * Finds the station, its sockets and the facility modules in the two loaded scenes, and moves
 * each root to the origin: the GLBs keep their places in the Blender scene.
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
  const facilities = new Map<string, Object3D>();
  for (const node of [...facilitiesScene.children]) {
    const match = FACILITY.exec(node.name);
    if (!match?.[1] || !match[2]) continue;
    node.position.set(0, 0, 0);
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
  return frigateModel(await fetchGlb(frigateUrl));
}
