import { MathUtils, OrthographicCamera, Vector3 } from "three";
import { tacticalViewDirection } from "../camera";

/** Elevations the greybox compares. 33 is the current tactical view (Visual Style Guide §6). */
export const ELEVATIONS = [33, 40, 45, 50] as const;

/** View direction at `elevationDeg`, keeping the tactical camera's compass bearing. */
export function viewDirectionAtElevation(elevationDeg: number): Vector3 {
  const base = tacticalViewDirection();
  const elevation = MathUtils.degToRad(elevationDeg);
  const horizontal = new Vector3(base.x, 0, base.z).normalize().multiplyScalar(Math.cos(elevation));
  return horizontal.setY(Math.sin(elevation));
}

export function createGreyboxCamera(
  aspect: number,
  viewWidth: number,
  elevationDeg: number,
  target: Vector3,
  distance = 200,
): OrthographicCamera {
  const halfWidth = viewWidth / 2;
  const halfHeight = halfWidth / aspect;
  const camera = new OrthographicCamera(
    -halfWidth,
    halfWidth,
    halfHeight,
    -halfHeight,
    0.1,
    distance * 4,
  );
  camera.position.copy(target).addScaledVector(viewDirectionAtElevation(elevationDeg), distance);
  camera.lookAt(target);
  camera.updateMatrixWorld();
  return camera;
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
