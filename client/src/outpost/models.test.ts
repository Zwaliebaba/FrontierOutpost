import { Box3, Vector3 } from "three";
import { describe, expect, it } from "vitest";
import facilitiesGlb from "../../../Design/FrontierOutpost/facilities.glb?inline";
import stationGlb from "../../../Design/FrontierOutpost/station.glb?inline";
import { facilityKey, indexModels, parseGlb } from "./models";
import type { OutpostModels } from "./models";

/** The bytes of a data URL, as Vite inlines an asset. */
const bytes = (url: string): ArrayBuffer =>
  Uint8Array.from(atob(url.slice(url.indexOf(",") + 1)), (c) => c.charCodeAt(0)).buffer;

// The committed exports, as create_scene.py writes them.
const models: OutpostModels = indexModels(
  await parseGlb(bytes(stationGlb)),
  await parseGlb(bytes(facilitiesGlb)),
);

function socketAt(id: string): Vector3 {
  const anchor = models.sockets.get(id);
  if (!anchor) throw new Error(`no socket ${id}`);
  return anchor.getWorldPosition(new Vector3());
}

describe("the outpost models", () => {
  it("has both construction sockets on the station", () => {
    expect([...models.sockets.keys()].sort()).toEqual(["a", "b"]);
    // Socket A beside the port pod, socket B beside the berth (glTF is Y-up; Blender -Y is +Z).
    expect(socketAt("a").x).toBeLessThan(-7);
    expect(socketAt("b").x).toBeGreaterThan(3);
    expect(socketAt("b").z).toBeGreaterThan(3);
  });

  it("has every facility at both tiers, each standing on its root", () => {
    for (const facility of ["sensor_array", "refinery", "fabrication_bay"]) {
      const [low, high] = [1, 2].map((tier) => {
        const model = models.facilities.get(facilityKey(facility, tier));
        if (!model) throw new Error(`no ${facility} tier ${String(tier)}`);
        const box = new Box3().setFromObject(model);
        expect(box.min.y).toBeGreaterThan(-0.01);
        expect(Math.max(-box.min.x, box.max.x, -box.min.z, box.max.z)).toBeLessThan(1.3);
        return box.max.y;
      });
      expect((high ?? 0) - (low ?? 0), facility).toBeGreaterThan(0.4);
    }
  });
});
