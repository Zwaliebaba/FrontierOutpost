import { Box3, Group, Mesh, Vector3 } from "three";
import type { BufferGeometry, Object3D } from "three";
import { describe, expect, it } from "vitest";
import sensorGlb from "../../../Design/Art/facilities/sensor_array/sensor_array.glb?inline";
import refineryGlb from "../../../Design/Art/facilities/refinery/refinery.glb?inline";
import fabricationGlb from "../../../Design/Art/facilities/fabrication_bay/fabrication_bay.glb?inline";
import exteriorGlb from "../../../Design/Art/station/exterior/exterior.glb?inline";
import interiorGlb from "../../../Design/Art/station/interior/interior.glb?inline";
import frigateGlb from "../../../Design/Art/hulls/frigate/frigate.glb?inline";
import { frigateModel, gearOf, setGear } from "../view/frigate";
import {
  facilityKey,
  facilityLoader,
  indexFacilities,
  indexStation,
  mergeByMaterial,
  parseGlb,
} from "./models";
import type { FacilityTemplates } from "./models";

/** The bytes of a data URL, as Vite inlines an asset. */
const bytes = (url: string): ArrayBuffer =>
  Uint8Array.from(atob(url.slice(url.indexOf(",") + 1)), (c) => c.charCodeAt(0)).buffer;

const templates: FacilityTemplates = { facilities: new Map(), tells: new Map() };
for (const url of [sensorGlb, refineryGlb, fabricationGlb]) {
  indexFacilities(await parseGlb(bytes(url)), templates);
}
const station = indexStation(
  await parseGlb(bytes(exteriorGlb)),
  await parseGlb(bytes(interiorGlb)),
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
  const box = new Box3().setFromObject(root, true);
  return {
    meshes,
    triangles,
    size: box.getSize(new Vector3()),
    centre: box.getCenter(new Vector3()),
  };
}

function socketAt(id: string): Vector3 {
  const anchor = station.sockets.get(id);
  if (!anchor) throw new Error(`no socket ${id}`);
  return anchor.getWorldPosition(new Vector3());
}

describe("the outpost models", () => {
  it("accepts the same digit-bearing identifiers as the authoring contract", () => {
    const scene = new Group();
    for (const name of ["Facility_module_2_T1", "Tell_module_2_T1"]) {
      const root = new Group();
      root.name = name;
      scene.add(root);
    }
    const found: FacilityTemplates = { facilities: new Map(), tells: new Map() };
    indexFacilities(scene, found);
    expect(found.facilities.has("module_2:1")).toBe(true);
    expect(found.tells.has("module_2:1")).toBe(true);
  });
  it("loads a catalogue facility only on demand, its tells with it, in one download", async () => {
    const loaded: FacilityTemplates = { facilities: new Map(), tells: new Map() };
    let downloads = 0;
    const load = facilityLoader(loaded, async () => {
      downloads++;
      return parseGlb(bytes(sensorGlb));
    });
    expect(downloads).toBe(0);
    await Promise.all([load("sensor_array"), load("sensor_array")]);
    expect(downloads).toBe(1);
    expect([...loaded.facilities.keys()].sort()).toEqual(["sensor_array:1", "sensor_array:2"]);
    expect([...loaded.tells.keys()].sort()).toEqual(["sensor_array:1", "sensor_array:2"]);
    await load("sensor_array");
    expect(downloads).toBe(1);
  });

  it("rejects a file missing a catalogue root and permits a retry", async () => {
    const loaded: FacilityTemplates = { facilities: new Map(), tells: new Map() };
    let downloads = 0;
    const load = facilityLoader(loaded, () => {
      downloads++;
      return Promise.resolve(new Group());
    });
    await expect(load("refinery")).rejects.toThrow("Missing catalogue root");
    await expect(load("refinery")).rejects.toThrow("Missing catalogue root");
    expect(downloads).toBe(2);
    expect(loaded.facilities.size + loaded.tells.size).toBe(0);
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

  it("finds the station's anchors: hardpoints outside, sockets and parking inside", () => {
    expect([...station.hardpoints.keys()].sort()).toEqual(["a", "b", "c", "reactor"]);
    expect([...station.sockets.keys()].sort()).toEqual(["a", "b", "c"]);
    expect([...station.parking.keys()]).toEqual(["frigate"]);
    // Blender (X, Y) is glTF (x, −z): socket A beside the cargo bay at (−6, −6), B beside the
    // hangar deck at (6, 0), C beside the processor at (−6, 6); the mouth at (0, 14).
    expect(socketAt("a").toArray()).toEqual([-6, 0, 6]);
    expect(socketAt("b").toArray()).toEqual([6, 0, 0]);
    expect(socketAt("c").toArray()).toEqual([-6, 0, -6]);
    const mouth = station.exterior.getObjectByName("Hangar_Mouth");
    expect(mouth?.getWorldPosition(new Vector3()).toArray()).toEqual([0, 0, -14]);
  });

  it("plates every socket with a closure of its own, socket C's as authored", () => {
    expect([...station.closures.keys()].sort()).toEqual(["a", "b", "c"]);
    for (const [id, closure] of station.closures) {
      expect(closure.parent, id).toBe(station.sockets.get(id));
      expect(closure.children.length, id).toBe(3);
      // Lying on the socket's deck, wherever the socket is.
      const centre = new Box3().setFromObject(closure).getCenter(new Vector3());
      const socket = socketAt(id);
      expect(Math.hypot(centre.x - socket.x, centre.z - socket.z), id).toBeLessThan(0.01);
    }
    expect(station.reactor.length).toBeGreaterThanOrEqual(3);
    for (const part of station.reactor) expect(part.name).toMatch(/^Reactor_Upgrade/);
  });

  it("draws the station in a mesh per material, the same triangles in the same place", async () => {
    // As exported: hundreds of small objects, each a draw call of its own.
    const authored = async (glb: string) => {
      const root = (await parseGlb(bytes(glb))).children[0];
      if (!root) throw new Error("no root");
      return drawn(root);
    };
    const outside = await authored(exteriorGlb);
    const after = drawn(station.exterior);
    expect(outside.meshes).toBeGreaterThan(300);
    // One per material, or two where its parts differ in their attributes.
    expect(after.meshes).toBeLessThanOrEqual(14);
    expect(after.triangles).toBe(outside.triangles);
    for (const axis of ["x", "y", "z"] as const) {
      expect(after.size[axis]).toBeCloseTo(outside.size[axis], 3);
      expect(after.centre[axis]).toBeCloseTo(outside.centre[axis], 3);
    }
    // Inside, the closures and the reactor stay apart, and sockets A and B have copies of C's.
    const inside = await authored(interiorGlb);
    const closures = [...station.closures.values()].map(drawn);
    const kept =
      closures.reduce((sum, closure) => sum + closure.meshes, 0) + station.reactor.length;
    const copied = (closures[0]?.triangles ?? 0) * (closures.length - 1);
    const interior = drawn(station.interior);
    expect(inside.meshes).toBeGreaterThan(100);
    expect(interior.meshes - kept).toBeLessThanOrEqual(10);
    expect(interior.triangles - copied).toBe(inside.triangles);
  });

  it("draws each facility module in a mesh per material", () => {
    for (const [key, model] of templates.facilities) {
      const { meshes, triangles } = drawn(model);
      expect(meshes, key).toBeLessThanOrEqual(9);
      expect(triangles, key).toBeGreaterThan(100);
    }
  });

  it("has every facility at both tiers, each standing on its root", () => {
    for (const facility of ["sensor_array", "refinery", "fabrication_bay"]) {
      const [low, high] = [1, 2].map((tier) => {
        const model = templates.facilities.get(facilityKey(facility, tier));
        if (!model) throw new Error(`no ${facility} tier ${String(tier)}`);
        const box = new Box3().setFromObject(model);
        expect(box.min.y).toBeGreaterThan(-0.01);
        expect(Math.max(-box.min.x, box.max.x, -box.min.z, box.max.z)).toBeLessThan(1.3);
        return box.max.y;
      });
      expect((high ?? 0) - (low ?? 0), facility).toBeGreaterThan(0.4);
    }
  });

  it("has every facility's tell at both tiers, growing with the tier and within the cube", () => {
    for (const facility of ["sensor_array", "refinery", "fabrication_bay"]) {
      const [low, high] = [1, 2].map((tier) => {
        const tell = templates.tells.get(facilityKey(facility, tier));
        if (!tell) throw new Error(`no ${facility} tell ${String(tier)}`);
        const size = new Box3().setFromObject(tell).getSize(new Vector3());
        expect(Math.max(size.x, size.y, size.z)).toBeLessThanOrEqual(1.5);
        return size.x * size.y * size.z;
      });
      expect(high ?? 0, facility).toBeGreaterThan(low ?? 0);
    }
  });
});
