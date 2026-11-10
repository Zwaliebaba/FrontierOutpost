import { Vector3 } from "three";
import { describe, expect, it } from "vitest";
import { elevationDegrees, tacticalViewDirection } from "../camera";
import {
  ELEVATIONS,
  aimRig,
  createRig,
  hiddenDepthBehind,
  maxViewWidthForSize,
  orbitDirection,
  perspectiveDistance,
  pixelsPerMetre,
  planeDepthScale,
  towardCamera,
  viewDirectionAtElevation,
} from "./view";

describe("greybox view", () => {
  it("reproduces the tactical direction at 33 degrees", () => {
    const direction = viewDirectionAtElevation(elevationDegrees(tacticalViewDirection()));
    expect(direction.distanceTo(tacticalViewDirection())).toBeLessThan(1e-9);
  });

  it("changes only the elevation, never the compass bearing", () => {
    const base = tacticalViewDirection();
    for (const elevation of ELEVATIONS) {
      const direction = viewDirectionAtElevation(elevation);
      expect(elevationDegrees(direction)).toBeCloseTo(elevation, 6);
      expect(Math.atan2(direction.z, direction.x)).toBeCloseTo(Math.atan2(base.z, base.x), 9);
    }
  });

  it("aims the camera at its target from the requested elevation", () => {
    const target = new Vector3(4, 0, -7);
    for (const projection of ["ortho", "narrow", "wide"] as const) {
      const rig = createRig(16 / 9, 55, projection);
      aimRig(rig, target, 45);
      const forward = rig.camera.getWorldDirection(new Vector3());
      expect(forward.dot(viewDirectionAtElevation(45))).toBeCloseTo(-1, 6);
      expect(rig.camera.position.distanceTo(target)).toBeCloseTo(rig.distance, 6);
    }
  });

  it("orbits about the vertical axis without changing elevation", () => {
    const quarter = orbitDirection(33, Math.PI / 2);
    expect(quarter.y).toBeCloseTo(viewDirectionAtElevation(33).y, 9);
    expect(quarter.dot(viewDirectionAtElevation(33))).toBeCloseTo(Math.sin(0.576) ** 2, 2);
    expect(orbitDirection(33, 2 * Math.PI).distanceTo(orbitDirection(33))).toBeLessThan(1e-9);
    // Positive yaw is clockwise seen from above: the bearing moves from +x toward +z.
    const base = viewDirectionAtElevation(33);
    const turned = orbitDirection(33, 0.2);
    expect(Math.atan2(turned.z, turned.x)).toBeGreaterThan(Math.atan2(base.z, base.x));
  });

  it("puts a perspective camera where it sees the requested width at its target", () => {
    const distance = perspectiveDistance(40, 16 / 9, 20);
    const visibleHeight = 2 * distance * Math.tan((20 * Math.PI) / 360);
    expect(visibleHeight * (16 / 9)).toBeCloseTo(40, 9);
    expect(perspectiveDistance(40, 16 / 9, 45)).toBeLessThan(distance);
  });

  it("points toward the camera: one direction for orthographic, per point for perspective", () => {
    const ortho = createRig(16 / 9, 40, "ortho");
    aimRig(ortho, new Vector3(), 33, 0.7);
    const a = towardCamera(ortho.camera, new Vector3(-10, 0, 3));
    const b = towardCamera(ortho.camera, new Vector3(12, 2, -8));
    expect(a.distanceTo(b)).toBeLessThan(1e-9);
    expect(a.distanceTo(orbitDirection(33, 0.7))).toBeLessThan(1e-6);

    const wide = createRig(16 / 9, 40, "wide");
    aimRig(wide, new Vector3(), 33);
    const near = towardCamera(wide.camera, new Vector3(-10, 0, 3));
    const far = towardCamera(wide.camera, new Vector3(12, 0, -8));
    expect(near.distanceTo(far)).toBeGreaterThan(0.1);
    expect(towardCamera(wide.camera, new Vector3()).distanceTo(orbitDirection(33))).toBeLessThan(
      1e-6,
    );
  });

  it("matches the review's figures for the 33 degree view", () => {
    // Depth along the plane is drawn at about 0.545 of width, and a structure hides about
    // 1.54 times its height of plane behind it.
    expect(planeDepthScale(33)).toBeCloseTo(0.545, 3);
    expect(hiddenDepthBehind(1, 33)).toBeCloseTo(1.54, 2);
    // A 1.29 m beam stays 24 px wide on a 1366 px viewport up to about 73 m of view width.
    expect(maxViewWidthForSize(1.29, 24, 1366)).toBeCloseTo(73.4, 1);
    expect(1.29 * pixelsPerMetre(73.4, 1366)).toBeCloseTo(24, 1);
  });

  it("hides less of the plane as the camera rises", () => {
    expect(hiddenDepthBehind(4, 50)).toBeLessThan(hiddenDepthBehind(4, 33));
  });
});
