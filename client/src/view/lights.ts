/**
 * The lights every view shares (style guide §6), so the station looks the same in the outpost
 * view and at a run's dock, and launching changes only what the camera looks at.
 */
import { DirectionalLight, HemisphereLight } from "three";
import type { Object3D } from "three";
import { linear } from "./readability";

export function stationLights(): Object3D[] {
  const starlight = new DirectionalLight(linear(0.67, 0.77, 1), 2.4);
  starlight.position.set(-4, 10, 6);
  // The restrained warm rim on industrial surfaces.
  const warm = new DirectionalLight(linear(1, 0.62, 0.37), 1.2);
  warm.position.set(6, 4, -5);
  return [
    new HemisphereLight(linear(0.3, 0.48, 1), linear(0.025, 0.038, 0.055), 0.7),
    starlight,
    warm,
  ];
}
