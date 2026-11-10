import { Vector3 } from "three";
import type { Camera } from "three";

export interface ScreenPoint {
  x: number;
  y: number;
}

export interface Viewport {
  width: number;
  height: number;
}

/** Projects a world position to CSS pixels, origin at the top-left of the viewport. */
export function worldToScreen(position: Vector3, camera: Camera, viewport: Viewport): ScreenPoint {
  const ndc = position.clone().project(camera);
  return { x: ((ndc.x + 1) / 2) * viewport.width, y: ((1 - ndc.y) / 2) * viewport.height };
}

/**
 * Where the view ray through a screen point meets the horizontal plane at `planeY`. Null when
 * the ray runs level with the plane or meets it behind the camera.
 */
export function screenToPlane(
  point: ScreenPoint,
  camera: Camera,
  viewport: Viewport,
  planeY = 0,
): Vector3 | null {
  const x = (point.x / viewport.width) * 2 - 1;
  const y = 1 - (point.y / viewport.height) * 2;
  // Two points on the ray, so the same code serves orthographic and perspective cameras.
  const near = new Vector3(x, y, -1).unproject(camera);
  const ray = new Vector3(x, y, 1).unproject(camera).sub(near);
  if (Math.abs(ray.y) < 1e-9) return null;
  const along = (planeY - near.y) / ray.y;
  return along < 0 ? null : near.addScaledVector(ray, along);
}

export interface PickCandidate {
  id: string;
  screen: ScreenPoint;
}

/** The candidate nearest `point` within `radiusPx`, or null when the click hits nothing. */
export function pickNearest(
  point: ScreenPoint,
  candidates: readonly PickCandidate[],
  radiusPx: number,
): string | null {
  let best: string | null = null;
  let bestDistance = radiusPx;
  for (const candidate of candidates) {
    const distance = Math.hypot(candidate.screen.x - point.x, candidate.screen.y - point.y);
    if (distance <= bestDistance) {
      best = candidate.id;
      bestDistance = distance;
    }
  }
  return best;
}
