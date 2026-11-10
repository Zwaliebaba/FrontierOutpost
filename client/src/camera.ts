import { MathUtils, OrthographicCamera, Vector3 } from "three";

/**
 * Unit vector from the scene toward the shared tactical camera, in Three.js / glTF Y-up space.
 *
 * Matches the Blender overview camera in `Design/FrontierOutpost/create_scene.py`: Blender's
 * Z-up view (0.43, -0.72, 0.545) becomes (0.43, 0.545, 0.72) after the glTF axis conversion,
 * about 33 degrees of elevation (Visual Style Guide §6).
 */
export function tacticalViewDirection(): Vector3 {
  return new Vector3(0.43, 0.545, 0.72).normalize();
}

/** Elevation of a view direction above the gameplay plane (XZ), in degrees. */
export function elevationDegrees(direction: Vector3): number {
  return MathUtils.radToDeg(Math.asin(direction.y / direction.length()));
}

/**
 * Orthographic tactical camera looking at `target`.
 *
 * `viewWidth` is the visible width in scene metres; 40 matches the Blender overview camera's
 * `ortho_scale`.
 */
export function createTacticalCamera(
  aspect: number,
  viewWidth = 40,
  target = new Vector3(0, 0, 0),
  distance = 60,
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
  camera.position.copy(target).addScaledVector(tacticalViewDirection(), distance);
  camera.lookAt(target);
  return camera;
}
