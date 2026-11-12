import { BoxGeometry, Group, Mesh, MeshStandardMaterial } from "three";
import { describe, expect, it } from "vitest";
import {
  centreOf,
  firstHit,
  modelOccluder,
  occludersHiding,
  rayEntry,
  rayHits,
  topOf,
} from "./occlusion";
import type { Occluder } from "./occlusion";
import { stationOccluders } from "./stationProxy";
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

  it("reports which station parts hide a ship behind the hub", () => {
    const view = toward(33);
    const horizontal = Math.hypot(view.x, view.z);
    const occluders = stationOccluders();
    const behindHub = onPlane((-view.x / horizontal) * 5, (-view.z / horizontal) * 5);
    expect(occludersHiding(behindHub, view, occluders)).toContain(0);
    expect(occludersHiding(onPlane(0, 30), view, occluders)).toEqual([]);
  });
});

describe("a loaded model as an occluder", () => {
  // A 4 m cube centred 2 m up at x = 10, inside a group as a loaded scene is.
  const model = new Group();
  const cube = new Mesh(new BoxGeometry(4, 4, 4), new MeshStandardMaterial());
  cube.position.set(10, 2, 0);
  model.add(cube);
  const occluder = modelOccluder(model);

  it("is bounded by the model where it stands", () => {
    const centre = centreOf(occluder);
    expect(centre.x).toBeCloseTo(10, 5);
    expect(centre.y).toBeCloseTo(2, 5);
    expect(centre.z).toBeCloseTo(0, 5);
    expect(topOf(occluder)).toBeCloseTo(4, 5);
  });

  it("is entered where a ray meets its triangles", () => {
    expect(rayEntry({ x: 0, y: 2, z: 0 }, { x: 1, y: 0, z: 0 }, occluder)).toBeCloseTo(8);
    expect(rayEntry({ x: 0, y: 2, z: 0 }, { x: -1, y: 0, z: 0 }, occluder)).toBeNull();
    // Inside the model's box but past its triangles: a miss, not the box.
    expect(rayEntry({ x: 0, y: 5, z: 0 }, { x: 1, y: 0, z: 0 }, occluder)).toBeNull();
  });

  it("hides what is behind it and is picked before what is farther", () => {
    const far: Occluder = { kind: "sphere", x: 20, y: 2, z: 0, radius: 1 };
    expect(occludersHiding({ x: 15, y: 2, z: 0 }, { x: -1, y: 0, z: 0 }, [occluder])).toEqual([0]);
    expect(firstHit({ x: 0, y: 2, z: 0 }, { x: 1, y: 0, z: 0 }, [far, occluder])).toBe(1);
  });
});
