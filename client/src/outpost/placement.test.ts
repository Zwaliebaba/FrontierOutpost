import { Vector3 } from "three";
import { describe, expect, it } from "vitest";
import exteriorGlb from "../../../Design/Art/station/exterior/exterior.glb?inline";
import interiorGlb from "../../../Design/Art/station/interior/interior.glb?inline";
import { SHIP_HEIGHT } from "../view/hulls";
import { indexStation, parseGlb } from "./models";
import { HOME_STATION, STATION_HEIGHT, placeStation } from "./placement";

const bytes = (url: string): ArrayBuffer =>
  Uint8Array.from(atob(url.slice(url.indexOf(",") + 1)), (c) => c.charCodeAt(0)).buffer;

const station = indexStation(
  await parseGlb(bytes(exteriorGlb)),
  await parseGlb(bytes(interiorGlb)),
);

/** The rules' distance from the station's centre to its mouth (frontier_sim::station). */
const MOUTH_DISTANCE = 14;

/** A station as the server sends it: the mouth `MOUTH_DISTANCE` out along `forward`. */
function info(centreX: number, centreZ: number, angle: number) {
  const [forwardX, forwardZ] = [Math.cos(angle), Math.sin(angle)];
  return {
    centreX,
    centreZ,
    forwardX,
    forwardZ,
    mouthX: centreX + forwardX * MOUTH_DISTANCE,
    mouthZ: centreZ + forwardZ * MOUTH_DISTANCE,
  };
}

describe("placing the station at the dock", () => {
  for (const [name, at] of [
    ["facing +x", info(-120, 0, 0)],
    ["facing +z", info(10, -40, Math.PI / 2)],
    ["facing -x", info(900, 30, Math.PI)],
    ["diagonal", info(-30, 20, -2.3)],
    ["shallow", info(5, 5, 0.4)],
  ] as const) {
    it(`puts the hangar mouth where the rules do (${name})`, () => {
      const placement = placeStation(at);
      for (const root of [station.exterior, station.interior]) {
        root.position.set(placement.x, placement.y, placement.z);
        root.rotation.set(0, placement.yaw, 0);
        root.updateMatrixWorld(true);
      }
      const mouth = station.exterior.getObjectByName("Hangar_Mouth");
      const world = mouth?.getWorldPosition(new Vector3());
      expect(world?.x).toBeCloseTo(at.mouthX, 4);
      expect(world?.z).toBeCloseTo(at.mouthZ, 4);
      // The sill at deck level, half a hull below the plane ships fly on.
      expect(world?.y).toBeCloseTo(-SHIP_HEIGHT / 2, 6);
      expect(placement.y).toBe(STATION_HEIGHT);
      expect(placement.heading).toBeCloseTo(Math.atan2(at.forwardZ, at.forwardX), 6);
      // The station's frame: +across is (−Fz, Fx), so Tell_B (Blender +X 14) lies that way.
      const tell = station.hardpoints.get("b")?.getWorldPosition(new Vector3());
      expect(tell?.x).toBeCloseTo(at.centreX - at.forwardZ * 14, 4);
      expect(tell?.z).toBeCloseTo(at.centreZ + at.forwardX * 14, 4);
      // The interior shares the frame: its hangar deck's parking place lies toward the mouth.
      const parked = station.parking.get("frigate")?.getWorldPosition(new Vector3());
      const along =
        ((parked?.x ?? 0) - at.centreX) * at.forwardX +
        ((parked?.z ?? 0) - at.centreZ) * at.forwardZ;
      expect(along).toBeCloseTo(10, 4);
    });
  }

  it("faces the rules' own default before a contract names a site", () => {
    const placement = placeStation(HOME_STATION);
    expect(placement).toMatchObject({ x: 0, z: 0, heading: 0 });
    // A zero facing falls back the same way.
    expect(placeStation({ ...HOME_STATION, forwardX: 0 }).heading).toBe(0);
  });
});
