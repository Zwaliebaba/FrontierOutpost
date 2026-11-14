import { BERTHS } from "./placement";
import { Box3, Group, Mesh, Vector3 } from "three";
import type { BufferGeometry, Object3D } from "three";
import { describe, expect, it } from "vitest";
import sensorGlb from "../../../Design/Art/facilities/sensor_array/sensor_array.glb?inline";
import refineryGlb from "../../../Design/Art/facilities/refinery/refinery.glb?inline";
import fabricationGlb from "../../../Design/Art/facilities/fabrication_bay/fabrication_bay.glb?inline";
import stationGlb from "../../../Design/FrontierOutpost/station.glb?inline";
import frigateGlb from "../../../Design/FrontierOutpost/frigate.glb?inline";
import { frigateModel, gearOf, setGear } from "../view/frigate";
import { facilityKey, facilityLoader, indexModels, mergeByMaterial, parseGlb } from "./models";
import type { OutpostModels } from "./models";

/** The bytes of a data URL, as Vite inlines an asset. */
const bytes = (url: string): ArrayBuffer =>
  Uint8Array.from(atob(url.slice(url.indexOf(",") + 1)), (c) => c.charCodeAt(0)).buffer;

const facilityScenes = new Group();
for (const url of [sensorGlb, refineryGlb, fabricationGlb]) {
  const scene = await parseGlb(bytes(url));
  facilityScenes.add(...scene.children);
}
// Frozen station plus migrated, checked pipeline exports.
const models: OutpostModels = indexModels(await parseGlb(bytes(stationGlb)), facilityScenes);

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
  it("accepts the same digit-bearing identifiers as the authoring contract", () => {
    const station = new Group();
    station.name = "Station_ROOT";
    const facilities = new Group();
    const facility = new Group();
    facility.name = "Facility_module_2_T1";
    facilities.add(facility);
    expect(indexModels(station, facilities).facilities.has("module_2:1")).toBe(true);
  });
  it("loads a catalogue facility only on demand and shares requests across its tiers", async () => {
    const templates = new Map<string, Object3D>();
    let downloads = 0;
    const load = facilityLoader(templates, async () => {
      downloads++;
      return parseGlb(bytes(sensorGlb));
    });
    expect(downloads).toBe(0);
    await Promise.all([load("sensor_array"), load("sensor_array")]);
    expect(downloads).toBe(1);
    expect([...templates.keys()].sort()).toEqual(["sensor_array:1", "sensor_array:2"]);
    await load("sensor_array");
    expect(downloads).toBe(1);
  });

  it("rejects a file missing a catalogue root and permits a retry", async () => {
    const templates = new Map<string, Object3D>();
    let downloads = 0;
    const load = facilityLoader(templates, () => {
      downloads++;
      return Promise.resolve(new Group());
    });
    await expect(load("refinery")).rejects.toThrow("Missing catalogue root");
    await expect(load("refinery")).rejects.toThrow("Missing catalogue root");
    expect(downloads).toBe(2);
    expect(templates.size).toBe(0);
  });
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
    const moving: Object3D[] = [];
    models.station.traverse((node) => {
      if (node.name.startsWith("SocketClosure_")) moving.push(node);
    });
    expect(moving).toHaveLength(3);
    expect(after.meshes - moving.length).toBeLessThanOrEqual(10);
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
