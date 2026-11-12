import { Object3D, Vector3 } from "three";
import { describe, expect, it } from "vitest";
import { SHIP_HEIGHT } from "../view/hulls";
import { BERTH, BERTHS, STATION_DROP, placeStation } from "./placement";

/** Where a point in station space lands once the station is placed. */
function world(placement: ReturnType<typeof placeStation>, x: number, y: number, z: number) {
  const station = new Object3D();
  station.position.set(placement.x, placement.y, placement.z);
  station.rotation.y = placement.yaw;
  station.updateMatrixWorld();
  return station.localToWorld(new Vector3(x, y, z));
}

describe("placing the station at the dock", () => {
  it("reads the apron centre from the shared berth layout", () => {
    expect(BERTH.x).toBeCloseTo(0, 3);
    expect(BERTH.z).toBeCloseTo(4.33, 2);
    expect(BERTH.deck).toBeCloseTo(0.86, 2);
  });

  for (const [name, dock, centre] of [
    ["compact", { x: -120, z: 0 }, { x: 70, z: 0 }],
    ["frontier", { x: -900, z: 0 }, { x: 620, z: 0 }],
    ["diagonal", { x: 10, z: -40 }, { x: -30, z: 20 }],
  ] as const) {
    it(`puts the berth on the dock and faces the site (${name})`, () => {
      const placement = placeStation(dock, centre);
      const berth = world(placement, BERTH.x, BERTH.deck, BERTH.z);
      expect(berth.x).toBeCloseTo(dock.x, 6);
      expect(berth.z).toBeCloseTo(dock.z, 6);
      // A hull centred on the plane rests its underside on the deck.
      expect(berth.y).toBeCloseTo(-SHIP_HEIGHT / 2, 6);
      expect(placement.y).toBeCloseTo(-STATION_DROP, 6);
      // The hub, at the station's origin, is behind the berth: away from the site.
      const hub = world(placement, 0, 0, 0);
      const toSite = Math.atan2(centre.z - dock.z, centre.x - dock.x);
      const toHub = Math.atan2(hub.z - dock.z, hub.x - dock.x);
      expect(Math.cos(toHub - toSite)).toBeCloseTo(-1, 6);
      expect(placement.heading).toBeCloseTo(toSite, 6);
      for (const slot of BERTHS) {
        const at = world(placement, slot.x, slot.deck, slot.z);
        // This is the authoritative simulation's lateral-offset convention.
        expect(at.x).toBeCloseTo(dock.x + Math.sin(toSite) * slot.x, 6);
        expect(at.z).toBeCloseTo(dock.z - Math.cos(toSite) * slot.x, 6);
      }
    });
  }
});
