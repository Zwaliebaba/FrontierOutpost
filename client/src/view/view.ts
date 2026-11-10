import { MathUtils, OrthographicCamera, PerspectiveCamera, Vector3 } from "three";
import { tacticalViewDirection } from "../camera";

/** Elevations the greybox compares. 33 is the current tactical view (Visual Style Guide §6). */
export const ELEVATIONS = [33, 40, 45, 50] as const;

/** Orthographic is the design's current rule; the perspective options are for comparison. */
export const PROJECTIONS = ["ortho", "narrow", "wide"] as const;
export type Projection = (typeof PROJECTIONS)[number];

/** Vertical field of view, in degrees, of each perspective option. */
export const PERSPECTIVE_FOV: Record<Exclude<Projection, "ortho">, number> = {
  narrow: 20,
  wide: 45,
};

export type GreyboxCamera = OrthographicCamera | PerspectiveCamera;

/**
 * Unit vector from a target toward a camera orbiting it. `yaw` is measured from the tactical
 * camera's compass bearing, in radians, positive clockwise seen from above.
 */
export function orbitDirection(elevationDeg: number, yaw = 0): Vector3 {
  const base = tacticalViewDirection();
  const bearing = Math.atan2(base.z, base.x) + yaw;
  const elevation = MathUtils.degToRad(elevationDeg);
  return new Vector3(
    Math.cos(bearing) * Math.cos(elevation),
    Math.sin(elevation),
    Math.sin(bearing) * Math.cos(elevation),
  );
}

/** View direction at `elevationDeg`, keeping the tactical camera's compass bearing. */
export function viewDirectionAtElevation(elevationDeg: number): Vector3 {
  return orbitDirection(elevationDeg);
}

/** Distance at which a perspective camera sees `viewWidth` metres across at its target. */
export function perspectiveDistance(viewWidth: number, aspect: number, fovDeg: number): number {
  return viewWidth / (2 * aspect * Math.tan(MathUtils.degToRad(fovDeg) / 2));
}

export interface CameraRig {
  camera: GreyboxCamera;
  /** Distance from the camera to the point it orbits. */
  distance: number;
}

const FAR = 2000;

/** A camera that shows `viewWidth` metres across at its target, in the chosen projection. */
export function createRig(aspect: number, viewWidth: number, projection: Projection): CameraRig {
  if (projection === "ortho") {
    const halfWidth = viewWidth / 2;
    const halfHeight = halfWidth / aspect;
    return {
      camera: new OrthographicCamera(-halfWidth, halfWidth, halfHeight, -halfHeight, 0.1, FAR),
      distance: 200,
    };
  }
  const fov = PERSPECTIVE_FOV[projection];
  return {
    camera: new PerspectiveCamera(fov, aspect, 0.5, FAR),
    distance: perspectiveDistance(viewWidth, aspect, fov),
  };
}

/** Points the rig at `target` from the given elevation and yaw. */
export function aimRig(rig: CameraRig, target: Vector3, elevationDeg: number, yaw = 0): void {
  rig.camera.position.copy(target).addScaledVector(orbitDirection(elevationDeg, yaw), rig.distance);
  rig.camera.lookAt(target);
  rig.camera.updateMatrixWorld();
}

/** Unit vector from `point` toward the camera: constant for orthographic, per point otherwise. */
export function towardCamera(camera: GreyboxCamera, point: Vector3): Vector3 {
  return camera instanceof OrthographicCamera
    ? camera.getWorldDirection(new Vector3()).negate()
    : camera.position.clone().sub(point).normalize();
}

export function pixelsPerMetre(viewWidth: number, viewportWidthPx: number): number {
  return viewportWidthPx / viewWidth;
}

/** Screen compression of distance along the plane, away from the camera, relative to width. */
export function planeDepthScale(elevationDeg: number): number {
  return Math.sin(MathUtils.degToRad(elevationDeg));
}

/** Length of gameplay plane hidden behind the top edge of a structure `height` metres tall. */
export function hiddenDepthBehind(height: number, elevationDeg: number): number {
  return height / Math.tan(MathUtils.degToRad(elevationDeg));
}

/** Widest view at which an object `sizeMetres` across still covers `minPixels` on screen. */
export function maxViewWidthForSize(
  sizeMetres: number,
  minPixels: number,
  viewportWidthPx: number,
): number {
  return (sizeMetres * viewportWidthPx) / minPixels;
}
