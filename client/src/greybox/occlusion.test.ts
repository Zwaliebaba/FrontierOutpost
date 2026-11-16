import { describe, expect, it } from "vitest";
import { centreOf, contains, firstHit, occludersHiding, rayEntry, rayHits } from "./occlusion";
import type { Occluder } from "./occlusion";
import { legacyStationOccluders } from "./stationProxy";
import { hiddenDepthBehind, viewDirectionAtElevation } from "../view/view";

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

  it("measures the distance to where a ray enters each kind of structure", () => {
    const along = { x: 1, y: 0, z: 0 };
    const from = { x: -10, y: 1, z: 0 };
    expect(rayEntry(from, along, box)).toBeCloseTo(8);
    expect(rayEntry(from, along, cylinder)).toBeCloseTo(8);
    expect(rayEntry({ x: -10, y: 0, z: 0 }, along, sphere)).toBeCloseTo(5.4);
    expect(rayEntry(from, { x: -1, y: 0, z: 0 }, box)).toBeNull();
    expect(rayEntry({ x: -10, y: 9, z: 0 }, along, box)).toBeNull();
    // A ray that starts inside is already in the structure.
    expect(rayEntry({ x: 0, y: 1, z: 0 }, along, box)).toBeCloseTo(0, 5);
  });

  it("picks the structure a ray meets first", () => {
    const near: Occluder = { kind: "box", x: -6, z: 0, halfX: 1, halfZ: 1, yMin: 0, yMax: 2 };
    const along = { x: 1, y: 0, z: 0 };
    const from = { x: -20, y: 1, z: 0 };
    expect(firstHit(from, along, [box, near, cylinder])).toBe(1);
    expect(firstHit(from, along, [box, cylinder])).toBe(0);
    expect(firstHit(from, { x: 0, y: 1, z: 0 }, [box, near])).toBe(-1);
    expect(firstHit(from, along, [])).toBe(-1);
  });

  it("gives the middle of a structure as its point to look at", () => {
    expect(centreOf(box)).toEqual({ x: 0, y: 2, z: 0 });
    expect(centreOf(sphere)).toEqual({ x: 0, y: 0, z: 0 });
  });

  it("turns a box by its yaw and leaves an unturned one as it was", () => {
    const long: Occluder = { kind: "box", x: 0, z: 0, halfX: 6, halfZ: 0.5, yMin: 0, yMax: 2 };
    const turned: Occluder = { ...long, yaw: Math.PI / 2 };
    const across = { x: 1, y: 0, z: 0 };
    // Unturned, the box is long in x; a quarter turn makes it long in z.
    expect(rayEntry({ x: -10, y: 1, z: 3 }, across, long)).toBeNull();
    expect(rayEntry({ x: -10, y: 1, z: 3 }, across, turned)).toBeCloseTo(9.5);
    expect(rayEntry({ x: -10, y: 1, z: 0 }, across, { ...long, yaw: 0 })).toBeCloseTo(4);
    // A bearing of 45°: its X axis runs along (1, 1) on the plane.
    const diagonal: Occluder = { ...long, yaw: Math.PI / 4 };
    expect(contains({ x: 4, y: 1, z: 4 }, diagonal)).toBe(true);
    expect(contains({ x: 4, y: 1, z: -4 }, diagonal)).toBe(false);
    expect(rayHits({ x: 4, y: 1, z: 4 }, { x: 0, y: 1, z: 0 }, diagonal)).toBe(true);
    expect(rayHits({ x: 4, y: 1, z: -4 }, { x: 0, y: 1, z: 0 }, diagonal)).toBe(false);
  });

  it("tells whether a point is inside each kind of structure", () => {
    expect(contains({ x: 1.9, y: 3.9, z: -1.9 }, box)).toBe(true);
    expect(contains({ x: 2.1, y: 1, z: 0 }, box)).toBe(false);
    expect(contains({ x: 0, y: 4.1, z: 0 }, box)).toBe(false);
    expect(contains({ x: 1.4, y: 1, z: 1.4 }, cylinder)).toBe(true);
    expect(contains({ x: 1.5, y: 1, z: 1.5 }, cylinder)).toBe(false);
    expect(contains({ x: 0, y: -0.1, z: 0 }, cylinder)).toBe(false);
    expect(contains({ x: 0, y: 4.5, z: 0 }, sphere)).toBe(true);
    expect(contains({ x: 0, y: 4.7, z: 0 }, sphere)).toBe(false);
  });

  it("reports which station parts hide a ship behind the hub", () => {
    const view = toward(33);
    const horizontal = Math.hypot(view.x, view.z);
    const occluders = legacyStationOccluders();
    const behindHub = onPlane((-view.x / horizontal) * 5, (-view.z / horizontal) * 5);
    expect(occludersHiding(behindHub, view, occluders)).toContain(0);
    expect(occludersHiding(onPlane(0, 30), view, occluders)).toEqual([]);
  });
});
