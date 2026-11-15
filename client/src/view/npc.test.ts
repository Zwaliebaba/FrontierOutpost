import { Box3, Mesh, MeshBasicMaterial, MeshStandardMaterial, Vector3 } from "three";
import { afterEach, describe, expect, it } from "vitest";
import raiderGlb from "../../../Design/Art/npcs/raider/raider.glb?inline";
import { npcEntry } from "../outpost/catalogue";
import { parseGlb } from "../outpost/models";
import { SIZE } from "../game/scene";
import { NpcModelError, npcModel } from "./npc";
import { SIGNAL, hullGeometry, shipMeshes, useNpcModel } from "./readability";

const bytes = (url: string): ArrayBuffer =>
  Uint8Array.from(atob(url.slice(url.indexOf(",") + 1)), (c) => c.charCodeAt(0)).buffer;

/** frontier_sim's raider collision radius (profile.rs), which the pipeline fits the hull in. */
const RAIDER_RADIUS = 1.3;

const entry = npcEntry("raider");
const raider = npcModel(await parseGlb(bytes(raiderGlb)), entry?.root ?? "");

afterEach(() => {
  useNpcModel("raider", null);
});

/** Every vertex of `object`, in its own frame. */
function vertices(object: typeof raider): Vector3[] {
  object.updateMatrixWorld(true);
  const points: Vector3[] = [];
  object.traverse((node) => {
    if (!(node instanceof Mesh)) return;
    const position = (node as Mesh).geometry.getAttribute("position");
    for (let i = 0; i < position.count; i++) {
      points.push(new Vector3().fromBufferAttribute(position, i).applyMatrix4(node.matrixWorld));
    }
  });
  return points;
}

/** Where the parts named like `name` are, together, in the ship's frame. */
function centreOf(name: RegExp): Vector3 {
  raider.updateMatrixWorld(true);
  const bounds = new Box3();
  raider.traverse((node) => {
    if (node instanceof Mesh && name.test(node.name)) bounds.expandByObject(node);
  });
  if (bounds.isEmpty()) throw new Error(`no part ${String(name)}`);
  return bounds.getCenter(new Vector3());
}

describe("the raider model", () => {
  it("is in the catalogue, under its pipeline root", () => {
    expect(entry?.root).toBe("Npc_raider");
    expect(npcEntry("pirate")).toBeNull();
    expect(() => npcModel(raider.clone(), "Npc_warden")).toThrow(NpcModelError);
  });

  it("lies inside the raider's collision circle, centred on the plane", () => {
    const points = vertices(raider);
    const reach = Math.max(...points.map((p) => Math.hypot(p.x, p.z)));
    expect(reach).toBeLessThanOrEqual(RAIDER_RADIUS);
    // Most of the circle is used: a crescent as wide as it is long.
    expect(reach).toBeGreaterThan(RAIDER_RADIUS * 0.85);
    const bounds = new Box3().setFromObject(raider);
    expect((bounds.min.y + bounds.max.y) / 2).toBeCloseTo(0, 6);
    expect(SIZE.raider).toBe(1);
  });

  it("faces +x: its emitters forward, its drives aft", () => {
    expect(centreOf(/^Emitter_lens/).x).toBeGreaterThan(0.9);
    expect(centreOf(/^Drive_core/).x).toBeLessThan(-0.85);
    // Symmetric about its length, the crystal on the spine.
    expect(centreOf(/^Emitter_lens/).z).toBeCloseTo(0, 3);
    expect(centreOf(/^Command_crystal/).z).toBeCloseTo(0, 3);
    expect(centreOf(/^Command_crystal/).y).toBeGreaterThan(0);
  });

  it("is drawn for raiders once in use; other hostiles keep the forked stand-in", () => {
    expect(shipMeshes("hostile", 1, "raider").hull).toBeInstanceOf(Mesh);
    useNpcModel("raider", raider);
    const ship = shipMeshes("hostile", 1, "raider");
    expect(ship.hull.getObjectByName("Npc_raider")).toBeDefined();
    expect(ship.gear).toEqual([]);
    const materials = new Set<string>();
    ship.hull.traverse((node) => {
      if (node instanceof Mesh && node.material instanceof MeshStandardMaterial) {
        materials.add(node.material.name);
        // A visible hull clears the structure mark, as the stand-in's does.
        expect(node.material.stencilWrite).toBe(true);
        expect(node.material.stencilRef).toBe(0);
      }
    });
    expect(materials).toContain("HostileHull");
    ship.silhouette.traverse((node) => {
      if (node instanceof Mesh) {
        expect((node.material as MeshBasicMaterial).color.equals(SIGNAL.hostile)).toBe(true);
      }
    });

    const drone = shipMeshes("hostile", SIZE.drone, "drone");
    expect(drone.hull).toBeInstanceOf(Mesh);
    expect((drone.hull as Mesh).geometry).toBe(hullGeometry("hostile"));
    // The player's side never takes an NPC's model.
    expect(shipMeshes("ally", 1, "raider").hull.getObjectByName("Npc_raider")).toBeUndefined();
  });
});
