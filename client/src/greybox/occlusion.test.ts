import { describe, expect, it } from "vitest";
import { occludersHiding, rayHits } from "./occlusion";
import type { Occluder } from "./occlusion";
import { stationOccluders } from "./stationProxy";
import { hiddenDepthBehind, viewDirectionAtElevation } from "./view";

const toward = (elevation: number) => viewDirectionAtElevation(elevation);
const onPlane = (x: number, z: number) => ({ x, y: 0, z });

describe("greybox occlusion", () => {
  const sphere: Occluder = { kind: "sphere", x: 0, y: 0, z: 0, radius: 4.6 };
  const box: Occluder = { kind: "box", x: 0, z: 0, halfX: 2, halfZ: 2, yMin: 0, yMax: 4 };
  const cylinder: Occluder = { kind: "cylinder", x: 0, z: 0, radius: 2, yMin: 0, yMax: 4 };

  it("hides a ship directly behind each kind of structure and not one in front", () => {
    const view = toward(33);
    // "Behind" is the side away from the camera: step back along the horizontal view axis.
    const horizontal = Math.hypot(view.x, view.z);
    const behind = onPlane((-view.x / horizontal) * 3.5, (-view.z / horizontal) * 3.5);
    const inFront = onPlane((view.x / horizontal) * 6, (view.z / horizontal) * 6);
    for (const occluder of [box, cylinder]) {
      expect(rayHits(behind, view, occluder)).toBe(true);
      expect(rayHits(inFront, view, occluder)).toBe(false);
    }
    const behindRock = onPlane((-view.x / horizontal) * 6, (-view.z / horizontal) * 6);
    expect(rayHits(behindRock, view, sphere)).toBe(true);
    expect(rayHits(inFront, view, sphere)).toBe(false);
  });

  it("stops hiding the plane beyond the structure's shadow length", () => {
    const view = toward(33);
    const horizontal = Math.hypot(view.x, view.z);
    // The cylinder is round, so its back edge is 2 m along the view axis whatever the bearing.
    const reach = 2 + hiddenDepthBehind(4, 33);
    const at = (distance: number) =>
      onPlane((-view.x / horizontal) * distance, (-view.z / horizontal) * distance);
    expect(rayHits(at(reach - 0.3), view, cylinder)).toBe(true);
    expect(rayHits(at(reach + 0.3), view, cylinder)).toBe(false);
  });

  it("never lets a structure below the plane hide a ship on it", () => {
    const sunk: Occluder = { kind: "cylinder", x: 0, z: 0, radius: 1.4, yMin: -3, yMax: -1 };
    for (let angle = 0; angle < 2 * Math.PI; angle += 0.3) {
      const point = onPlane(Math.cos(angle) * 3, Math.sin(angle) * 3);
      expect(rayHits(point, toward(33), sunk)).toBe(false);
    }
  });

  it("hides less from a steeper camera", () => {
    const horizontal = (elevation: number) => {
      const view = toward(elevation);
      return Math.hypot(view.x, view.z);
    };
    const hiddenAt = (elevation: number) => {
      const view = toward(elevation);
      const point = onPlane(
        (-view.x / horizontal(elevation)) * 5.5,
        (-view.z / horizontal(elevation)) * 5.5,
      );
      return rayHits(point, view, cylinder);
    };
    expect(hiddenAt(33)).toBe(true);
    expect(hiddenAt(50)).toBe(false);
  });

  it("reports which station parts hide a ship behind the hub", () => {
    const view = toward(33);
    const horizontal = Math.hypot(view.x, view.z);
    const occluders = stationOccluders();
    const behindHub = onPlane((-view.x / horizontal) * 5, (-view.z / horizontal) * 5);
    expect(occludersHiding(behindHub, view, occluders)).toContain(0);
    expect(occludersHiding(onPlane(0, 30), view, occluders)).toEqual([]);
  });
});
