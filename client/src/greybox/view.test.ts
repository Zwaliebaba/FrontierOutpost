import { Vector3 } from "three";
import { describe, expect, it } from "vitest";
import { elevationDegrees, tacticalViewDirection } from "../camera";
import {
  ELEVATIONS,
  createGreyboxCamera,
  hiddenDepthBehind,
  maxViewWidthForSize,
  pixelsPerMetre,
  planeDepthScale,
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
    const camera = createGreyboxCamera(16 / 9, 55, 45, target);
    const forward = camera.getWorldDirection(new Vector3());
    expect(forward.dot(viewDirectionAtElevation(45))).toBeCloseTo(-1, 6);
    expect(camera.right - camera.left).toBeCloseTo(55);
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
