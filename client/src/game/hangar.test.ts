import { describe, expect, it } from "vitest";
import { SHIP_LENGTH } from "../view/hulls";
import { cameraFocus, insideHangar, mouthDistance, stationPoint, worldPoint } from "./hangar";

/** The rules' numbers (frontier_sim::station), only to build stations as the server sends them. */
const MOUTH_DISTANCE = 14;
const HANGAR_START = 8;

function station(centreX: number, centreZ: number, angle: number) {
  const [forwardX, forwardZ] = [Math.cos(angle), Math.sin(angle)];
  return {
    centreX,
    centreZ,
    forwardX,
    forwardZ,
    mouthX: centreX + forwardX * MOUTH_DISTANCE,
    mouthZ: centreZ + forwardZ * MOUTH_DISTANCE,
    mouthWidth: 8,
  };
}

const STATIONS = [station(-120, 0, 0), station(30, -40, Math.PI / 2), station(5, 9, -2.3)];

describe("the station's frame", () => {
  it("matches the rules': +across is forward turned to (−Fz, Fx), and round-trips", () => {
    const facingX = station(-120, 0, 0);
    // Facing +x, +across is +z.
    const side = stationPoint(facingX, -120, 3);
    expect(side.across).toBeCloseTo(3, 9);
    expect(side.along).toBeCloseTo(0, 9);
    for (const at of STATIONS) {
      expect(mouthDistance(at)).toBeCloseTo(MOUTH_DISTANCE, 6);
      const back = stationPoint(at, ...xz(worldPoint(at, { across: 2.5, along: -7 })));
      expect(back.across).toBeCloseTo(2.5, 6);
      expect(back.along).toBeCloseTo(-7, 6);
    }
  });
});

const xz = (point: { x: number; z: number }): [number, number] => [point.x, point.z];

describe("ships inside the hangar", () => {
  it("are not drawn while they wait at the hangar's start, and are once they reach the mouth", () => {
    for (const at of STATIONS) {
      const along = (distance: number, across = 0) =>
        xz(worldPoint(at, { across, along: distance }));
      // The departure queue: every waiting ship on one spot, so none is drawn there.
      expect(insideHangar(at, ...along(HANGAR_START))).toBe(true);
      expect(insideHangar(at, ...along(MOUTH_DISTANCE - SHIP_LENGTH / 2 - 0.01))).toBe(true);
      // Its end at the mouth: seen leaving, or still seen going in.
      expect(insideHangar(at, ...along(MOUTH_DISTANCE - SHIP_LENGTH / 2 + 0.01))).toBe(false);
      expect(insideHangar(at, ...along(MOUTH_DISTANCE + 4))).toBe(false);
      // A larger hull shows sooner.
      expect(insideHangar(at, ...along(MOUTH_DISTANCE - 2), 5)).toBe(false);
    }
  });

  it("does not hide ships flying beside or behind the station", () => {
    for (const at of STATIONS) {
      const point = (across: number, along: number) => xz(worldPoint(at, { across, along }));
      expect(insideHangar(at, ...point(6, 10))).toBe(false);
      expect(insideHangar(at, ...point(0, -20))).toBe(false);
      expect(insideHangar(at, ...point(0, -3))).toBe(false);
    }
  });
});

describe("the camera on a ship in the hangar", () => {
  it("holds on the mouth while the ship is behind it, then follows the ship without a jump", () => {
    for (const at of STATIONS) {
      const mouth = { x: at.mouthX, z: at.mouthZ };
      const start = worldPoint(at, { across: 0, along: HANGAR_START });
      const focus = cameraFocus(at, start.x, start.z);
      expect(focus.x).toBeCloseTo(mouth.x, 6);
      expect(focus.z).toBeCloseTo(mouth.z, 6);
      // Just inside and just outside the mouth, the camera barely moves.
      const inside = worldPoint(at, { across: 0, along: MOUTH_DISTANCE - 0.001 });
      const outside = worldPoint(at, { across: 0, along: MOUTH_DISTANCE + 0.001 });
      const a = cameraFocus(at, inside.x, inside.z);
      const b = cameraFocus(at, outside.x, outside.z);
      expect(Math.hypot(a.x - b.x, a.z - b.z)).toBeLessThan(0.01);
      // Out on the lane, or anywhere else, it looks at the ship itself.
      const lane = worldPoint(at, { across: 1, along: 30 });
      expect(cameraFocus(at, lane.x, lane.z)).toEqual(lane);
    }
  });
});
