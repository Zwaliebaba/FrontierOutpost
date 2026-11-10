import { Vector3 } from "three";
import { describe, expect, it } from "vitest";
import { pickNearest, screenRay, screenToPlane, worldToScreen } from "./picking";
import { aimRig, createRig } from "./view";
import type { Projection } from "./view";

const viewport = { width: 1366, height: 768 };
const target = new Vector3(3, 0, -2);

function cameraFor(projection: Projection, elevation = 33, yaw = 0) {
  const rig = createRig(viewport.width / viewport.height, 40, projection);
  aimRig(rig, target, elevation, yaw);
  return rig.camera;
}
const camera = cameraFor("ortho");

describe("greybox picking", () => {
  it("projects the camera target to the centre of the viewport", () => {
    const centre = worldToScreen(new Vector3(3, 0, -2), camera, viewport);
    expect(centre.x).toBeCloseTo(683, 3);
    expect(centre.y).toBeCloseTo(384, 3);
  });

  it("draws raised points higher on screen", () => {
    const low = worldToScreen(new Vector3(0, 0, 0), camera, viewport);
    const high = worldToScreen(new Vector3(0, 2, 0), camera, viewport);
    expect(high.y).toBeLessThan(low.y);
    expect(high.x).toBeCloseTo(low.x, 6);
  });

  it("round-trips plane points through the screen, at any plane height", () => {
    for (const point of [new Vector3(-6, 0, 4), new Vector3(9, 1.5, -11)]) {
      const back = screenToPlane(worldToScreen(point, camera, viewport), camera, viewport, point.y);
      expect(back?.distanceTo(point)).toBeLessThan(1e-6);
    }
  });

  it.each(["ortho", "narrow", "wide"] as const)(
    "round-trips plane points in the %s projection from a rotated camera",
    (projection) => {
      const rotated = cameraFor(projection, 50, 1.1);
      for (const point of [new Vector3(-6, 0, 4), new Vector3(9, 1.5, -11)]) {
        const screen = worldToScreen(point, rotated, viewport);
        expect(screenToPlane(screen, rotated, viewport, point.y)?.distanceTo(point)).toBeLessThan(
          1e-4,
        );
      }
    },
  );

  it("shows the same width at the target in every projection", () => {
    for (const projection of ["ortho", "narrow", "wide"] as const) {
      const view = cameraFor(projection);
      const right = new Vector3(1, 0, 0).applyQuaternion(view.quaternion);
      const left = worldToScreen(target.clone().addScaledVector(right, -20), view, viewport);
      const edge = worldToScreen(target.clone().addScaledVector(right, 20), view, viewport);
      expect(left.x).toBeCloseTo(0, 3);
      expect(edge.x).toBeCloseTo(viewport.width, 3);
    }
  });

  it.each(["ortho", "wide"] as const)(
    "casts a %s view ray through the point clicked",
    (projection) => {
      const view = cameraFor(projection, 40, 0.6);
      const point = new Vector3(7, 1, -5);
      const { origin, direction } = screenRay(worldToScreen(point, view, viewport), view, viewport);
      expect(direction.length()).toBeCloseTo(1, 9);
      // The clicked point lies on the ray, ahead of its origin.
      const toPoint = point.clone().sub(origin);
      expect(toPoint.dot(direction)).toBeGreaterThan(0);
      expect(toPoint.clone().cross(direction).length()).toBeLessThan(1e-4);
    },
  );

  it("finds no plane point for a ray that points at the sky", () => {
    const low = cameraFor("wide", 8);
    expect(screenToPlane({ x: 683, y: 0 }, low, viewport)).toBeNull();
  });

  it("picks the nearest candidate inside the radius, and nothing outside it", () => {
    const candidates = [
      { id: "H1", screen: { x: 100, y: 100 } },
      { id: "H2", screen: { x: 112, y: 100 } },
    ];
    expect(pickNearest({ x: 108, y: 101 }, candidates, 20)).toBe("H2");
    expect(pickNearest({ x: 95, y: 100 }, candidates, 20)).toBe("H1");
    expect(pickNearest({ x: 160, y: 100 }, candidates, 20)).toBeNull();
    expect(pickNearest({ x: 0, y: 0 }, [], 20)).toBeNull();
  });
});
