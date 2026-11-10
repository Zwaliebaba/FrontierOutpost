import { Vector3 } from "three";
import { describe, expect, it } from "vitest";
import { createTacticalCamera, elevationDegrees, tacticalViewDirection } from "./camera";

describe("tactical camera", () => {
  it("sits at the style guide's 33 degree elevation", () => {
    expect(elevationDegrees(tacticalViewDirection())).toBeCloseTo(33, 0);
  });

  it("looks at its target from the tactical direction", () => {
    const target = new Vector3(5, 0, -3);
    const camera = createTacticalCamera(16 / 9, 40, target);
    const forward = camera.getWorldDirection(new Vector3());
    expect(forward.dot(tacticalViewDirection())).toBeCloseTo(-1, 6);
  });

  it("shows the requested width at any aspect ratio", () => {
    const camera = createTacticalCamera(16 / 9, 40);
    expect(camera.right - camera.left).toBeCloseTo(40);
    expect(camera.top - camera.bottom).toBeCloseTo(22.5);
  });
});
