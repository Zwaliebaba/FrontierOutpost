/** Structure proxies in Three.js space (Y up, metres). The gameplay plane is y = 0. */
export type Occluder =
  | { kind: "sphere"; x: number; y: number; z: number; radius: number }
  | { kind: "cylinder"; x: number; z: number; radius: number; yMin: number; yMax: number }
  | { kind: "box"; x: number; z: number; halfX: number; halfZ: number; yMin: number; yMax: number };

export interface Point3 {
  x: number;
  y: number;
  z: number;
}

const EPSILON = 1e-6;

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
  return occluder.kind === "sphere"
    ? { x: occluder.x, y: occluder.y, z: occluder.z }
    : { x: occluder.x, y: (occluder.yMin + occluder.yMax) / 2, z: occluder.z };
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
