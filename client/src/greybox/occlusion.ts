import { Box3, Mesh, Raycaster, Vector3 } from "three";
import type { BufferGeometry, Object3D } from "three";
import { MeshBVH, acceleratedRaycast } from "three-mesh-bvh";

/**
 * Structures in Three.js space (Y up, metres). The gameplay plane is y = 0. A `model` is a loaded
 * one, such as the real station, tested against its triangles; `min` and `max` bound it.
 */
export type Occluder =
  | { kind: "sphere"; x: number; y: number; z: number; radius: number }
  | { kind: "cylinder"; x: number; z: number; radius: number; yMin: number; yMax: number }
  | { kind: "box"; x: number; z: number; halfX: number; halfZ: number; yMin: number; yMax: number }
  | { kind: "model"; object: Object3D; min: Point3; max: Point3 };

/** The occluders made of simple shapes, such as the station proxy's. */
export type Primitive = Exclude<Occluder, { kind: "model" }>;

export interface Point3 {
  x: number;
  y: number;
  z: number;
}

const EPSILON = 1e-6;

/**
 * `object`, where it stands now, as an occluder. Move it before this, not after. Each mesh gets a
 * bounding-volume hierarchy: tested triangle by triangle, the station's 17,000 cost about 6 ms a
 * frame for the scene's ships, a third of a frame.
 */
export function modelOccluder(object: Object3D): Occluder {
  object.updateMatrixWorld(true);
  object.traverse((node) => {
    if (!(node instanceof Mesh)) return;
    const geometry = node.geometry as BufferGeometry;
    geometry.boundsTree ??= new MeshBVH(geometry);
    node.raycast = acceleratedRaycast;
  });
  const bounds = new Box3().setFromObject(object);
  return {
    kind: "model",
    object,
    min: { x: bounds.min.x, y: bounds.min.y, z: bounds.min.z },
    max: { x: bounds.max.x, y: bounds.max.y, z: bounds.max.z },
  };
}

const raycaster = new Raycaster();
// Whether a structure is in the way needs only the nearest hit, ahead of the ray's start.
raycaster.firstHitOnly = true;
raycaster.near = EPSILON;

/** [enter, exit] of a ray through the slab min..max on one axis, or null if it misses. */
function slab(origin: number, dir: number, min: number, max: number): [number, number] | null {
  if (Math.abs(dir) < EPSILON) {
    return origin >= min && origin <= max ? [-Infinity, Infinity] : null;
  }
  const a = (min - origin) / dir;
  const b = (max - origin) / dir;
  return a < b ? [a, b] : [b, a];
}

/** Where a ray enters the overlap of `intervals`, or null if they do not overlap ahead of it. */
function entryAhead(intervals: ([number, number] | null)[]): number | null {
  let enter = EPSILON;
  let exit = Infinity;
  for (const interval of intervals) {
    if (interval === null) return null;
    enter = Math.max(enter, interval[0]);
    exit = Math.min(exit, interval[1]);
  }
  return enter <= exit ? enter : null;
}

/**
 * Distance along a ray from `origin`, in unit direction `dir`, to where it enters `occluder`.
 * Null if it misses; close to zero if the ray starts inside.
 */
export function rayEntry(origin: Point3, dir: Point3, occluder: Occluder): number | null {
  switch (occluder.kind) {
    case "sphere": {
      const ox = origin.x - occluder.x;
      const oy = origin.y - occluder.y;
      const oz = origin.z - occluder.z;
      const b = ox * dir.x + oy * dir.y + oz * dir.z;
      const c = ox * ox + oy * oy + oz * oz - occluder.radius * occluder.radius;
      const discriminant = b * b - c;
      if (discriminant < 0) return null;
      const root = Math.sqrt(discriminant);
      return entryAhead([[-b - root, -b + root]]);
    }
    case "cylinder": {
      const ox = origin.x - occluder.x;
      const oz = origin.z - occluder.z;
      const a = dir.x * dir.x + dir.z * dir.z;
      const c = ox * ox + oz * oz - occluder.radius * occluder.radius;
      let side: [number, number] | null;
      if (a < EPSILON) {
        side = c <= 0 ? [-Infinity, Infinity] : null;
      } else {
        const b = ox * dir.x + oz * dir.z;
        const discriminant = b * b - a * c;
        if (discriminant < 0) return null;
        const root = Math.sqrt(discriminant);
        side = [(-b - root) / a, (-b + root) / a];
      }
      return entryAhead([side, slab(origin.y, dir.y, occluder.yMin, occluder.yMax)]);
    }
    case "box":
      return entryAhead([
        slab(origin.x, dir.x, occluder.x - occluder.halfX, occluder.x + occluder.halfX),
        slab(origin.y, dir.y, occluder.yMin, occluder.yMax),
        slab(origin.z, dir.z, occluder.z - occluder.halfZ, occluder.z + occluder.halfZ),
      ]);
    case "model": {
      // Most rays miss the model's box; only those that enter it are tested against triangles.
      const { min, max } = occluder;
      const inBox = entryAhead([
        slab(origin.x, dir.x, min.x, max.x),
        slab(origin.y, dir.y, min.y, max.y),
        slab(origin.z, dir.z, min.z, max.z),
      ]);
      if (inBox === null) return null;
      raycaster.set(new Vector3(origin.x, origin.y, origin.z), new Vector3(dir.x, dir.y, dir.z));
      const [hit] = raycaster.intersectObject(occluder.object, true);
      return hit ? hit.distance : null;
    }
  }
}

/** Whether a ray from `origin` along unit vector `dir` passes through `occluder`. */
export function rayHits(origin: Point3, dir: Point3, occluder: Occluder): boolean {
  return rayEntry(origin, dir, occluder) !== null;
}

/** Index of the occluder a ray meets first, or -1 if it meets none. */
export function firstHit(origin: Point3, dir: Point3, occluders: readonly Occluder[]): number {
  let best = -1;
  let bestDistance = Infinity;
  occluders.forEach((occluder, index) => {
    const distance = rayEntry(origin, dir, occluder);
    if (distance !== null && distance < bestDistance) {
      best = index;
      bestDistance = distance;
    }
  });
  return best;
}

/** The middle of an occluder, as a point for the camera to look at. */
export function centreOf(occluder: Occluder): Point3 {
  switch (occluder.kind) {
    case "sphere":
      return { x: occluder.x, y: occluder.y, z: occluder.z };
    case "model": {
      const { min, max } = occluder;
      return { x: (min.x + max.x) / 2, y: (min.y + max.y) / 2, z: (min.z + max.z) / 2 };
    }
    default:
      return { x: occluder.x, y: (occluder.yMin + occluder.yMax) / 2, z: occluder.z };
  }
}

/** How high an occluder reaches above the plane. */
export function topOf(occluder: Occluder): number {
  switch (occluder.kind) {
    case "sphere":
      return occluder.y + occluder.radius;
    case "model":
      return occluder.max.y;
    default:
      return occluder.yMax;
  }
}

/** Indexes of the occluders between `point` and a camera in direction `towardCamera`. */
export function occludersHiding(
  point: Point3,
  towardCamera: Point3,
  occluders: readonly Occluder[],
): number[] {
  const hits: number[] = [];
  occluders.forEach((occluder, index) => {
    if (rayHits(point, towardCamera, occluder)) hits.push(index);
  });
  return hits;
}
