import { Box3, Mesh, MeshStandardMaterial, Vector3 } from "three";
import { afterEach, describe, expect, it } from "vitest";
import frigateGlb from "../../../Design/FrontierOutpost/frigate.glb?inline";
import { parseGlb } from "../outpost/models";
import { BERTH } from "../outpost/placement";
import { GEAR_LIFT, GEAR_SECONDS, frigateModel, gearOf, setGear, stepGear } from "./frigate";
import { SHIP_BEAM, SHIP_HEIGHT, SHIP_LENGTH } from "./hulls";
import { SIGNAL, STRUCTURE, shipMeshes, useShipModel } from "./readability";

const bytes = (url: string): ArrayBuffer =>
  Uint8Array.from(atob(url.slice(url.indexOf(",") + 1)), (c) => c.charCodeAt(0)).buffer;

// The committed export, as create_scene.py writes it.
const frigate = frigateModel(await parseGlb(bytes(frigateGlb)));

afterEach(() => {
  useShipModel(null);
});

/** Where the named part of `ship` is, in the ship's frame. */
function partAt(ship: typeof frigate, name: RegExp): Vector3 {
  ship.updateMatrixWorld(true);
  const parts: Vector3[] = [];
  ship.traverse((node) => {
    if (name.test(node.name)) parts.push(new Box3().setFromObject(node).getCenter(new Vector3()));
  });
  const [first] = parts;
  if (!first) throw new Error(`no part ${String(name)}`);
  return first;
}

describe("the starter frigate", () => {
  it("is drawn as the stand-in hulls are: nose along +x, resting its feet on the deck", () => {
    const bounds = new Box3().setFromObject(frigate);
    // The landed envelope the berth and the stand-in hulls were sized from.
    expect(bounds.max.x - bounds.min.x).toBeCloseTo(SHIP_LENGTH, 1);
    expect(bounds.max.z - bounds.min.z).toBeCloseTo(SHIP_BEAM, 1);
    expect(bounds.max.y - bounds.min.y).toBeCloseTo(SHIP_HEIGHT, 1);
    expect(bounds.min.y).toBeCloseTo(-SHIP_HEIGHT / 2, 6);
    expect((bounds.min.x + bounds.max.x) / 2).toBeCloseTo(0, 6);
    expect((bounds.min.z + bounds.max.z) / 2).toBeCloseTo(0, 6);
    // The bridge is forward and the drives aft.
    expect(partAt(frigate, /bridge_glazing/).x).toBeGreaterThan(0.5);
    expect(partAt(frigate, /idle_engine_aperture/).x).toBeLessThan(-0.5);
    // Its height on the deck matches the generator's docking check.
    expect(SHIP_HEIGHT / 2 + bounds.min.y).toBeCloseTo(0, 6);
    expect(BERTH.deck).toBeCloseTo(0.86, 2);
  });

  it("folds its three legs up into the hull and lowers them again", () => {
    const legs = gearOf(frigate);
    expect(legs).toHaveLength(6);
    const lowered = legs.map((leg) => leg.position.y);
    setGear(legs, 0);
    legs.forEach((leg, index) => {
      expect(leg.position.y).toBeCloseTo((lowered[index] ?? 0) + GEAR_LIFT, 6);
      expect(leg.scale.x).toBeLessThan(1);
    });
    // Folded, the feet no longer reach the deck.
    expect(new Box3().setFromObject(frigate).min.y).toBeGreaterThan(-SHIP_HEIGHT / 2 + 0.1);
    setGear(legs, 1);
    legs.forEach((leg, index) => {
      expect(leg.position.y).toBeCloseTo(lowered[index] ?? 0, 6);
      expect(leg.scale.x).toBe(1);
    });
  });

  it("moves the gear at its own pace", () => {
    expect(stepGear(1, 0, GEAR_SECONDS / 2)).toBeCloseTo(0.5, 6);
    expect(stepGear(0.5, 0, GEAR_SECONDS)).toBe(0);
    expect(stepGear(0, 1, GEAR_SECONDS * 2)).toBe(1);
    expect(stepGear(0.3, 0.3, 1)).toBe(0.3);
  });

  it("draws player-side ships as the frigate, an ally's armour tinted, hostiles as before", () => {
    useShipModel(frigate);
    const own = shipMeshes("self");
    const ally = shipMeshes("ally");
    const hostile = shipMeshes("hostile");
    expect(own.hull.getObjectByName("Frigate_ROOT")).toBeDefined();
    expect(own.gear).toHaveLength(12);
    expect(hostile.hull).toBeInstanceOf(Mesh);
    expect(hostile.gear).toEqual([]);

    const armour = (ship: typeof own) => {
      let colour: string | null = null;
      ship.hull.traverse((node) => {
        if (node instanceof Mesh && node.material instanceof MeshStandardMaterial) {
          if (node.material.name.startsWith("Armor")) colour ??= node.material.color.getHexString();
          // A visible hull clears the structure mark, as the stand-in's does.
          expect(node.material.stencilWrite).toBe(true);
          expect(node.material.stencilRef).toBe(0);
        }
      });
      return colour;
    };
    expect(armour(own)).not.toBeNull();
    expect(armour(ally)).not.toBe(armour(own));

    own.silhouette.traverse((node) => {
      if (node instanceof Mesh) {
        expect((node.material as MeshStandardMaterial).color.equals(SIGNAL.self)).toBe(true);
        expect((node.material as MeshStandardMaterial).stencilRef).toBe(STRUCTURE);
        expect(node.renderOrder).toBeGreaterThan(0);
      }
    });
    // Ships share the model's materials, so drawing another adds none.
    const another = shipMeshes("self");
    const material = (ship: typeof own) => {
      let found: unknown = null;
      ship.hull.traverse((node) => {
        if (node instanceof Mesh) found ??= node.material;
      });
      return found;
    };
    expect(material(another)).toBe(material(own));
  });
});
