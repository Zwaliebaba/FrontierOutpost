import { Raycaster, Vector3 } from "three";
import { describe, expect, it } from "vitest";
import facilitiesGlb from "../../../Design/FrontierOutpost/facilities.glb?inline";
import stationGlb from "../../../Design/FrontierOutpost/station.glb?inline";
import { indexModels, parseGlb } from "../outpost/models";
import { BERTH } from "../outpost/placement";
import { SHIP_HEIGHT } from "../view/hulls";
import { occludersHiding, topOf } from "./occlusion";
import { realStation, stationOccluders } from "./stationProxy";

const bytes = (url: string): ArrayBuffer =>
  Uint8Array.from(atob(url.slice(url.indexOf(",") + 1)), (c) => c.charCodeAt(0)).buffer;

describe("station proxy", () => {
  const parts = stationOccluders();
  const extent = (axis: "x" | "z") =>
    parts.map((part) => {
      const half = part.kind === "box" ? (axis === "x" ? part.halfX : part.halfZ) : part.radius;
      return [part[axis] - half, part[axis] + half] as const;
    });

  it("spans the generator's station width of about 14.6 m, pod to pod", () => {
    const xs = extent("x");
    const width = Math.max(...xs.map(([, max]) => max)) - Math.min(...xs.map(([min]) => min));
    expect(width).toBeCloseTo(14.64, 1);
  });

  it("puts the landing deck where the game does, under a ship's hull, toward the camera", () => {
    const apron = parts.find((part) => part.kind === "box" && part.halfX > 2);
    // The inset deck is 3 cm below the landing feet the game rests a hull's underside on.
    expect(apron?.kind === "box" ? apron.yMax : Number.NaN).toBeCloseTo(-SHIP_HEIGHT / 2 - 0.03, 2);
    expect(apron?.z).toBeGreaterThan(0);
  });

  it("rises to the hub roof and the tallest mast", () => {
    const tops = parts.map(topOf);
    expect(tops[0]).toBeCloseTo(3.25, 1);
    expect(Math.max(...tops)).toBeCloseTo(8.77, 1);
  });
});

describe("the real station in the greybox", async () => {
  const { station } = indexModels(
    await parseGlb(bytes(stationGlb)),
    await parseGlb(bytes(facilitiesGlb)),
  );
  const occluder = realStation(station);
  const down = (x: number, z: number) => {
    const ray = new Raycaster(new Vector3(x, 50, z), new Vector3(0, -1, 0));
    return ray.intersectObject(station, true)[0]?.point.y ?? Number.NaN;
  };
  // The tactical camera's direction at rest: 33 degrees up, looking along -z.
  const towardCamera = new Vector3(
    0,
    Math.sin((33 * Math.PI) / 180),
    Math.cos((33 * Math.PI) / 180),
  );

  it("stands where the game stands it: a ship's underside on the berth's deck", () => {
    expect(down(BERTH.x, BERTH.z)).toBeCloseTo(-SHIP_HEIGHT / 2, 1);
  });

  it("is about the proxy's size, so the scene's orbits still fit around it", () => {
    if (occluder.kind !== "model") throw new Error("not a model");
    expect(occluder.max.x - occluder.min.x).toBeGreaterThan(14);
    expect(topOf(occluder)).toBeGreaterThan(8);
  });

  it("hides a ship behind it from the camera, and not one in front of it", () => {
    // Behind the hub (−z, away from the camera) and in front of the dock (+z, toward it).
    expect(occludersHiding({ x: 0, y: 0, z: -3.5 }, towardCamera, [occluder])).toEqual([0]);
    expect(occludersHiding({ x: 0, y: 0, z: 12 }, towardCamera, [occluder])).toEqual([]);
    expect(occludersHiding({ x: 30, y: 0, z: 0 }, towardCamera, [occluder])).toEqual([]);
  });
});
