/**
 * The outpost's models, from the art pipeline (Design/Art, plan M9): the station's exterior with
 * its tell hardpoints, its interior's cutaway with the construction sockets and the hangar deck,
 * and each facility's modules and tells at every tier. Node names are the pipeline's:
 * `StationExterior_ROOT`, `Tell_A`, `StationInterior_ROOT`, `Socket_A`, `Closure_C*`,
 * `Parking_frigate`, `Facility_<facility>_T<tier>`, `Tell_<facility>_T<tier>`.
 */
import { Group, Matrix4, Mesh, MeshStandardMaterial } from "three";
import type { BufferGeometry, Material, Object3D } from "three";
import { GLTFLoader } from "three/addons/loaders/GLTFLoader.js";
import { mergeGeometries } from "three/addons/utils/BufferGeometryUtils.js";
import { facilityEntry, hullEntry, npcEntry, stationEntry } from "./catalogue";
import { frigateModel, gearOf } from "../view/frigate";
import { npcModel } from "../view/npc";

/** The station's exterior and interior, each with its root at the origin. */
export interface StationModels {
  /** The station as it stands at the dock: one hangar mouth, the tells' hardpoints. */
  readonly exterior: Object3D;
  /** The cutaway: sockets, processor, cargo bay and the hangar deck. */
  readonly interior: Object3D;
  /** Each interior socket's anchor, by the game's socket identifier: "a", "b", "c". */
  readonly sockets: ReadonlyMap<string, Object3D>;
  /**
   * Each exterior tell hardpoint, by the socket it belongs to ("a", "b", "c") or "reactor".
   */
  readonly hardpoints: ReadonlyMap<string, Object3D>;
  /**
   * The closure plate shown on each socket while it is closed, a child of the socket's anchor.
   * The interior models socket C's (`Closure_C*`); the other sockets get a copy of it, since a
   * closed socket looks the same wherever it is (style guide §10).
   */
  readonly closures: ReadonlyMap<string, Object3D>;
  /** The hangar deck's parking places, by hull: "frigate". */
  readonly parking: ReadonlyMap<string, Object3D>;
  /** The reactor upgrade's meshes (`Reactor_Upgrade*`), shown once it is bought (plan M10). */
  readonly reactor: readonly Object3D[];
}

export interface OutpostModels extends StationModels {
  /** Each facility module, its root at the origin, by {@link facilityKey}. */
  readonly facilities: ReadonlyMap<string, Object3D>;
  /** Each facility's exterior tell, its root at the origin, by {@link facilityKey}. */
  readonly tells: ReadonlyMap<string, Object3D>;
  /** Load one facility on demand, sharing concurrent requests and parsed templates. */
  readonly loadFacility?: (id: string) => Promise<void>;
}

/** The outpost's models and the starter frigate (view/frigate), parked on the hangar deck. */
export interface LoadedModels extends OutpostModels {
  readonly frigate: Object3D;
}

/** The parsed templates of every facility loaded so far, modules and tells. */
export interface FacilityTemplates {
  readonly facilities: Map<string, Object3D>;
  readonly tells: Map<string, Object3D>;
}

export function facilityKey(facility: string, tier: number): string {
  return `${facility}:${String(tier)}`;
}

const SOCKET = /^Socket_([A-Z])$/;
const HARDPOINT = /^Tell_([A-Z]|reactor)$/;
const PARKING = /^Parking_([a-z][a-z0-9_]*)$/;
const FACILITY = /^Facility_([a-z][a-z0-9_]*)_T(\d+)$/;
const TELL = /^Tell_([a-z][a-z0-9_]*)_T(\d+)$/;

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

function rootOf(scene: Object3D, name: string): Object3D {
  const root = scene.getObjectByName(name);
  if (!root) throw new ModelError(`The station's model has no ${name}`);
  root.position.set(0, 0, 0);
  root.updateMatrixWorld(true);
  return root;
}

/**
 * Finds the station's anchors in its two loaded scenes, moves each root to the origin, gathers
 * each socket's closure plate under its anchor, and merges the rest of each model's meshes by
 * material. The closures and the reactor's meshes stay separate, so they can be shown and hidden.
 */
export function indexStation(
  exteriorScene: Object3D,
  interiorScene: Object3D,
  roots: { exterior: string; interior: string } = {
    exterior: "StationExterior_ROOT",
    interior: "StationInterior_ROOT",
  },
): StationModels {
  const exterior = rootOf(exteriorScene, roots.exterior);
  const interior = rootOf(interiorScene, roots.interior);
  const hardpoints = new Map<string, Object3D>();
  exterior.traverse((node) => {
    const match = HARDPOINT.exec(node.name);
    if (match?.[1]) hardpoints.set(match[1].toLowerCase(), node);
  });
  const sockets = new Map<string, Object3D>();
  const parking = new Map<string, Object3D>();
  const plates = new Map<string, Object3D[]>();
  const reactor: Object3D[] = [];
  interior.traverse((node) => {
    const socket = SOCKET.exec(node.name)?.[1];
    if (socket) sockets.set(socket.toLowerCase(), node);
    const hull = PARKING.exec(node.name)?.[1];
    if (hull) parking.set(hull, node);
    const closed = /^Closure_([A-Z])/.exec(node.name)?.[1];
    if (closed && node instanceof Mesh) plates.set(closed, [...(plates.get(closed) ?? []), node]);
    if (node.name.startsWith("Reactor_Upgrade")) reactor.push(node);
  });
  mergeByMaterial(exterior);
  mergeByMaterial(interior, new Set([...[...plates.values()].flat(), ...reactor]));
  const closures = new Map<string, Object3D>();
  for (const [letter, parts] of plates) {
    const anchor = sockets.get(letter.toLowerCase());
    if (!anchor) continue;
    const closure = new Group();
    closure.name = `Closure_${letter}`;
    anchor.add(closure);
    for (const part of parts) closure.attach(part);
    closures.set(letter.toLowerCase(), closure);
  }
  const [model] = closures.values();
  for (const [id, anchor] of sockets) {
    if (closures.has(id) || !model) continue;
    const closure = model.clone();
    closure.name = `Closure_${id.toUpperCase()}`;
    anchor.add(closure);
    closures.set(id, closure);
  }
  exterior.updateMatrixWorld(true);
  interior.updateMatrixWorld(true);
  return { exterior, interior, sockets, hardpoints, closures, parking, reactor };
}

/**
 * Adds the facility modules and tells named `names` in `scene` to `templates`, each moved to the
 * origin and merged by material. A module's operating indicators stay separate, so they can be
 * lit one by one.
 */
export function indexFacilities(
  scene: Object3D,
  templates: FacilityTemplates,
  names: readonly string[] = scene.children.map((node) => node.name),
): void {
  for (const name of names) {
    const module = FACILITY.exec(name);
    const tell = TELL.exec(name);
    const match = module ?? tell;
    if (!match?.[1] || !match[2]) continue;
    const root = scene.getObjectByName(name);
    if (!root) throw new ModelError(`Missing ${name}`);
    root.position.set(0, 0, 0);
    const preserved = new Set<Object3D>();
    root.traverse((node) => {
      if (node.name.startsWith("Operating_Tier_")) preserved.add(node);
    });
    mergeByMaterial(root, preserved);
    (module ? templates.facilities : templates.tells).set(
      facilityKey(match[1], Number(match[2])),
      root,
    );
  }
}

/** Parses a GLB's bytes into its scene. */
export function parseGlb(bytes: ArrayBuffer): Promise<Object3D> {
  return new Promise((resolve, reject) => {
    new GLTFLoader().parse(
      bytes,
      "",
      (gltf) => {
        gltf.scene.traverse((node) => {
          if (!(node instanceof Mesh)) return;
          const materials = Array.isArray(node.material) ? node.material : [node.material];
          for (const material of materials) {
            if (material instanceof MeshStandardMaterial) {
              if (
                material.name === "Amber" ||
                material.name === "Ember" ||
                material.name.startsWith("Windows")
              )
                material.emissiveIntensity = Math.min(material.emissiveIntensity, 1.8);
              else if (
                material.name === "Cyan" ||
                material.name.startsWith("Lights") ||
                material.name.startsWith("Navigation")
              )
                material.emissiveIntensity = Math.min(material.emissiveIntensity, 2);
            }
          }
        });
        resolve(gltf.scene);
      },
      (error) => {
        reject(error instanceof Error ? error : new ModelError("the GLB could not be parsed"));
      },
    );
  });
}

export async function fetchGlb(url: string): Promise<Object3D> {
  const response = await fetch(url);
  if (!response.ok) throw new ModelError(`${url}: HTTP ${String(response.status)}`);
  return parseGlb(await response.arrayBuffer());
}

let loading: Promise<LoadedModels> | null = null;

/**
 * Loads a facility's file the first time it is asked for: its modules and its tells, every tier,
 * in the one download, shared by concurrent requests.
 */
export function facilityLoader(
  templates: FacilityTemplates,
  load: (url: string) => Promise<Object3D> = fetchGlb,
): (id: string) => Promise<void> {
  const pending = new Map<string, Promise<void>>();
  return (id) => {
    let request = pending.get(id);
    if (!request) {
      const entry = facilityEntry(id);
      request = load(entry.url)
        .then((scene) => {
          for (const name of entry.roots) {
            if (!scene.getObjectByName(name))
              throw new ModelError(`Missing catalogue root ${name}`);
          }
          const staged: FacilityTemplates = { facilities: new Map(), tells: new Map() };
          indexFacilities(scene, staged, entry.roots);
          for (const [key, root] of staged.facilities) templates.facilities.set(key, root);
          for (const [key, root] of staged.tells) templates.tells.set(key, root);
        })
        .catch((error: unknown) => {
          pending.delete(id);
          throw error;
        });
      pending.set(id, request);
    }
    return request;
  };
}

/** Loads the station and the ship once; facilities wait until an installed one is first shown. */
export function loadOutpostModels(): Promise<LoadedModels> {
  const exterior = stationEntry("exterior");
  const interior = stationEntry("interior");
  loading ??= Promise.all([fetchGlb(exterior.url), fetchGlb(interior.url), loadFrigate()]).then(
    ([outside, inside, frigate]) => {
      const station = indexStation(outside, inside, {
        exterior: exterior.root,
        interior: interior.root,
      });
      const templates: FacilityTemplates = { facilities: new Map(), tells: new Map() };
      return { ...station, ...templates, loadFacility: facilityLoader(templates), frigate };
    },
  );
  return loading;
}

/** Loads the starter frigate on its own, for the greybox. */
export async function loadFrigate(): Promise<Object3D> {
  const entry = hullEntry("frigate");
  const ship = frigateModel(await fetchGlb(entry.url), entry.root);
  // Four roster ships should not multiply every authored plate into a draw call.
  // Landing gear keeps its own transforms so deployment still animates.
  mergeByMaterial(ship, new Set(gearOf(ship)));
  return ship;
}

/**
 * Loads an NPC ship's model, or resolves null when the catalogue has none for it: that kind
 * keeps its stand-in hull.
 */
export async function loadNpc(id: string): Promise<Object3D | null> {
  const entry = npcEntry(id);
  if (!entry) return null;
  const ship = npcModel(await fetchGlb(entry.url), entry.root);
  // Up to the NPC cap of one kind can fly at once: one draw call per material, not per plate.
  mergeByMaterial(ship, new Set());
  return ship;
}
