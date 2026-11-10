import { Vector3 } from "three";
import { describe, expect, it } from "vitest";
import { pickNearest, screenToPlane, worldToScreen } from "./picking";
import { createGreyboxCamera } from "./view";

const viewport = { width: 1366, height: 768 };
const camera = createGreyboxCamera(viewport.width / viewport.height, 40, 33, new Vector3(3, 0, -2));

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
