import { Vector3 } from "three";
import { describe, expect, it } from "vitest";
import { contains, rayHits } from "./occlusion";
import type { Occluder, Point3 } from "./occlusion";
import {
  DEFAULT_STATION,
  LANE_LENGTH,
  MOUTH_FACINGS,
  MOUTH_HEIGHT,
  STATION_SCALES,
  hangarOf,
  legacyStationOccluders,
  mouthBearing,
  onHangarAxis,
  stationOccluders,
  stationReach,
} from "./stationProxy";
import type { Hangar, StationSetup } from "./stationProxy";
import { worldToScreen } from "../view/picking";
import { aimRig, createRig } from "../view/view";

/** How far a part reaches from the station's centre on the plane. */
function reachOf(part: Occluder): number {
  if (part.kind !== "box") return Math.hypot(part.x, part.z) + part.radius;
  const yaw = part.yaw ?? 0;
  const corners = [-1, 1].flatMap((u) =>
    [-1, 1].map((v) =>
      Math.hypot(
        part.x + u * part.halfX * Math.cos(yaw) - v * part.halfZ * Math.sin(yaw),
        part.z + u * part.halfX * Math.sin(yaw) + v * part.halfZ * Math.cos(yaw),
      ),
    ),
  );
  return Math.max(...corners);
}

const reach = (parts: readonly Occluder[]) => Math.max(...parts.map(reachOf));
const top = (parts: readonly Occluder[]) =>
  Math.max(...parts.map((part) => (part.kind === "sphere" ? part.radius : part.yMax)));

const RINGS = STATION_SCALES.filter((scale) => scale !== 1);
const SETUPS: StationSetup[] = RINGS.flatMap((scale) =>
  MOUTH_FACINGS.map((facing) => ({ scale, facing })),
);

function ringHangar(setup: StationSetup): Hangar {
  const hangar = hangarOf(setup);
  if (!hangar) throw new Error("a ring has a hangar");
  return hangar;
}

const at = (hangar: Hangar, along: number, lateral: number, y: number): Point3 => {
  const [x, z] = onHangarAxis(hangar, along, lateral);
  return { x, y, z };
};

describe("Stage 1 station proxy", () => {
  const parts = legacyStationOccluders();
  const extent = (axis: "x" | "z") =>
    parts.map((part) => {
      const half = part.kind === "box" ? (axis === "x" ? part.halfX : part.halfZ) : part.radius;
      return [part[axis] - half, part[axis] + half] as const;
    });

  it("spans the generator's station width of about 14.6 m, pod to pod", () => {
    const xs = extent("x");
    const width = Math.max(...xs.map(([, max]) => max)) - Math.min(...xs.map(([min]) => min));
    expect(width).toBeCloseTo(14.64, 1);
    expect(stationReach({ scale: 1, facing: "site" })).toBeCloseTo(width / 2, 1);
  });

  it("puts the landing deck on the gameplay plane and toward the camera", () => {
    const apron = parts.find((part) => part.kind === "box" && part.halfX > 2);
    expect(apron?.kind === "box" ? apron.yMax : Number.NaN).toBeCloseTo(0);
    expect(apron?.z).toBeGreaterThan(0);
  });

  it("rises to the hub roof and the tallest mast", () => {
    const tops = parts.map((part) => (part.kind === "sphere" ? part.radius : part.yMax));
    expect(tops[0]).toBeCloseTo(3.85);
    expect(Math.max(...tops)).toBeCloseTo(9.37);
  });

  it("is the station at scale 1, whichever way a mouth would face, and has no hangar", () => {
    for (const facing of MOUTH_FACINGS) {
      expect(stationOccluders({ scale: 1, facing })).toEqual(parts);
      expect(hangarOf({ scale: 1, facing })).toBeNull();
    }
  });
});

describe("M9 ring proxy", () => {
  it("defaults to the ring at its authored size, mouth toward the site", () => {
    expect(DEFAULT_STATION).toEqual({ scale: 2.2, facing: "site" });
  });

  it("spans the art's 32.4 m ring at 2.2 and grows with the scale", () => {
    const authored = stationOccluders({ scale: 2.2, facing: "site" });
    // The mouth's threshold apron reaches a little beyond the ring, below the plane.
    const aboveThePlane = authored.filter((part) => part.kind === "sphere" || part.yMax > 0);
    expect(2 * reach(aboveThePlane)).toBeGreaterThan(32.4);
    expect(2 * reach(aboveThePlane)).toBeLessThan(33);
    expect(stationReach({ scale: 2.2, facing: "site" })).toBeCloseTo(16.3);
    // About 2.2 times the Stage 1 station's span, as the art study proposes.
    expect(reach(aboveThePlane) / reach(legacyStationOccluders())).toBeCloseTo(2.2, 1);
    // The tower's beacon is the top.
    expect(top(authored)).toBeCloseTo(10.23);

    const large = stationOccluders({ scale: 3, facing: "site" });
    expect(large).toHaveLength(authored.length);
    expect(reach(large)).toBeCloseTo((reach(authored) * 3) / 2.2);
    expect(top(large)).toBeCloseTo((top(authored) * 3) / 2.2);
    expect(stationReach({ scale: 3, facing: "site" })).toBeCloseTo((16.3 * 3) / 2.2);
  });

  it.each(SETUPS)("keeps the hangar and the mouth's lane clear at $scale, $facing", (setup) => {
    const parts = stationOccluders(setup);
    const hangar = ringHangar(setup);
    const height = (MOUTH_HEIGHT * setup.scale) / 2.2;
    const blocked: Point3[] = [];
    // From the middle of the hangar out through the mouth and the contract's whole lane.
    for (let along = hangar.deep - 2; along <= hangar.mouth + LANE_LENGTH; along += 0.5) {
      for (let lateral = -hangar.halfWidth; lateral <= hangar.halfWidth; lateral += 0.5) {
        for (let y = 0; y <= height; y += 0.5) {
          const point = at(hangar, along, lateral, y);
          if (parts.some((part) => contains(point, part))) blocked.push(point);
        }
      }
    }
    expect(blocked).toEqual([]);
  });

  it.each(SETUPS)("roofs the hangar and walls it with cheeks at $scale, $facing", (setup) => {
    const parts = stationOccluders(setup);
    const hangar = ringHangar(setup);
    const inside = at(hangar, hangar.deep, 0, 0);
    const hidden = (dir: Point3) => parts.some((part) => rayHits(inside, dir, part));
    const [alongX, alongZ] = onHangarAxis(hangar, 1, 0);
    const [acrossX, acrossZ] = onHangarAxis(hangar, 0, 1);
    expect(hidden({ x: 0, y: 1, z: 0 })).toBe(true);
    expect(hidden({ x: acrossX, y: 0, z: acrossZ })).toBe(true);
    expect(hidden({ x: -acrossX, y: 0, z: -acrossZ })).toBe(true);
    // Out through the mouth there is nothing.
    expect(hidden({ x: alongX, y: 0, z: alongZ })).toBe(false);
  });

  it.each(SETUPS)("leaves the ring's gap where the mouth faces at $scale, $facing", (setup) => {
    const parts = stationOccluders(setup);
    const hangar = ringHangar(setup);
    const ring = (stationReach(setup) * 14.4) / 16.3;
    const inRing = (along: number) => parts.some((part) => contains(at(hangar, along, 0, 0), part));
    expect(inRing(ring)).toBe(false);
    expect(inRing(-ring)).toBe(true);
    expect(hangar.bearing).toBeCloseTo(mouthBearing(setup.facing));
  });

  it("turns the mouth toward the site, the camera, the screen's right or away", () => {
    const rig = createRig(16 / 9, 40, "ortho");
    aimRig(rig, new Vector3(), 33);
    const viewport = { width: 1600, height: 900 };
    const screen = (facing: StationSetup["facing"]) => {
      const [x, z] = onHangarAxis(ringHangar({ scale: 2.2, facing }), 10, 0);
      return worldToScreen(new Vector3(x, 0, z), rig.camera, viewport);
    };
    const centre = worldToScreen(new Vector3(), rig.camera, viewport);
    // The camera sits above the bottom of the screen; away is up the screen.
    expect(screen("camera").x).toBeCloseTo(centre.x);
    expect(screen("camera").y).toBeGreaterThan(centre.y);
    expect(screen("away").x).toBeCloseTo(centre.x);
    expect(screen("away").y).toBeLessThan(centre.y);
    expect(screen("side").x).toBeGreaterThan(centre.x);
    expect(screen("side").y).toBeCloseTo(centre.y);
    // Both of frontier-sim's sites lie along +X.
    expect(mouthBearing("site")).toBe(0);
  });
});
