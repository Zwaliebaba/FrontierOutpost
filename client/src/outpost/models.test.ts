import { BERTHS } from "./placement";
import { Box3, Mesh, Vector3 } from "three";
import type { BufferGeometry, Object3D } from "three";
import { describe, expect, it } from "vitest";
import facilitiesGlb from "../../../Design/FrontierOutpost/facilities.glb?inline";
import stationGlb from "../../../Design/FrontierOutpost/station.glb?inline";
import frigateGlb from "../../../Design/FrontierOutpost/frigate.glb?inline";
import { frigateModel, gearOf, setGear } from "../view/frigate";
import { facilityKey, indexModels, mergeByMaterial, parseGlb } from "./models";
import type { OutpostModels } from "./models";

/** The bytes of a data URL, as Vite inlines an asset. */
const bytes = (url: string): ArrayBuffer =>
  Uint8Array.from(atob(url.slice(url.indexOf(",") + 1)), (c) => c.charCodeAt(0)).buffer;

// The committed exports, as create_scene.py writes them.
const models: OutpostModels = indexModels(
  await parseGlb(bytes(stationGlb)),
  await parseGlb(bytes(facilitiesGlb)),
);

/** What `root` costs to draw and what it draws: meshes, triangles and bounds. */
function drawn(root: Object3D) {
  root.updateMatrixWorld(true);
  let meshes = 0;
  let triangles = 0;
  root.traverse((node) => {
    if (!(node instanceof Mesh)) return;
    meshes += 1;
    triangles += ((node.geometry as BufferGeometry).index?.count ?? 0) / 3;
  });
  const box = new Box3().setFromObject(root);
  return {
    meshes,
    triangles,
    size: box.getSize(new Vector3()),
    centre: box.getCenter(new Vector3()),
  };
}

function socketAt(id: string): Vector3 {
  const anchor = models.sockets.get(id);
  if (!anchor) throw new Error(`no socket ${id}`);
  return anchor.getWorldPosition(new Vector3());
}

describe("the outpost models", () => {
  it("batches ship armor while preserving independently animated landing gear", async () => {
    const ship = frigateModel(await parseGlb(bytes(frigateGlb)));
    const before = drawn(ship);
    const legs = gearOf(ship);
    const deployed = legs.map((leg) => leg.position.y);
    mergeByMaterial(ship, new Set(legs));
    const after = drawn(ship);
    expect(after.meshes).toBeLessThanOrEqual(16);
    expect(after.triangles).toBe(before.triangles);
    expect(after.size.distanceTo(before.size)).toBeLessThan(0.0001);
    expect(after.centre.distanceTo(before.centre)).toBeLessThan(0.0001);
    expect(gearOf(ship)).toEqual(legs);
    setGear(legs, 0);
    expect(legs.every((leg, index) => leg.position.y > (deployed[index] ?? 0))).toBe(true);
  });

  it("exports four berth anchors aligned with the shared layout", () => {
    for (const berth of BERTHS) {
      const anchor = models.station.getObjectByName(`Berth_${berth.id}`);
      expect(anchor, berth.id).toBeDefined();
      const at = anchor?.getWorldPosition(new Vector3());
      expect(at?.x).toBeCloseTo(berth.x, 4);
      expect(at?.y).toBeCloseTo(berth.deck, 4);
      expect(at?.z).toBeCloseTo(berth.z, 4);
    }
  });

  it("has both construction sockets on the station", () => {
    expect([...models.sockets.keys()].sort()).toEqual(["a", "b"]);
    // Socket A beside the port pod, socket B beside the berth (glTF is Y-up; Blender -Y is +Z).
    expect(socketAt("a").x).toBeLessThan(-7);
    expect(socketAt("b").x).toBeGreaterThan(3);
    expect(socketAt("b").z).toBeGreaterThan(3);
  });

  it("draws the station in a mesh per material, the same triangles in the same place", async () => {
    // As exported: hundreds of small objects, each a draw call of its own.
    const authored = (await parseGlb(bytes(stationGlb))).getObjectByName("Station_ROOT");
    if (!authored) throw new Error("no Station_ROOT");
    authored.position.set(0, 0, 0);
    const before = drawn(authored);
    const after = drawn(models.station);
    expect(before.meshes).toBeGreaterThan(300);
    expect(after.meshes).toBeLessThanOrEqual(10);
    expect(after.triangles).toBe(before.triangles);
    for (const axis of ["x", "y", "z"] as const) {
      expect(after.size[axis]).toBeCloseTo(before.size[axis], 3);
      expect(after.centre[axis]).toBeCloseTo(before.centre[axis], 3);
    }
  });

  it("draws each facility module in a mesh per material", () => {
    for (const [key, model] of models.facilities) {
      const { meshes, triangles } = drawn(model);
      expect(meshes, key).toBeLessThanOrEqual(9);
      expect(triangles, key).toBeGreaterThan(100);
    }
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
