import { Group, Mesh, MeshStandardMaterial, OrthographicCamera, Raycaster, Vector2 } from "three";
import { BoxGeometry } from "three";
import { describe, expect, it } from "vitest";
import type { AccountInfo } from "../protocol";
import { facilityKey } from "./models";
import type { OutpostModels } from "./models";
import { STRUCTURE } from "../view/readability";
import { placeStation } from "./placement";
import { OutpostScene } from "./scene";

/** A station with two sockets and every module, as small stand-ins. */
function models(): OutpostModels {
  const station = new Group();
  const sockets = new Map<string, Group>();
  for (const [id, x] of [
    ["a", -9],
    ["b", 5],
  ] as const) {
    const anchor = new Group();
    anchor.name = `Socket_${id.toUpperCase()}`;
    anchor.position.set(x, 1, 0);
    station.add(anchor);
    sockets.set(id, anchor);
  }
  const facilities = new Map<string, Group>();
  for (const facility of ["sensor_array", "refinery", "fabrication_bay"]) {
    for (const tier of [1, 2]) {
      const root = new Group();
      root.name = `Facility_${facility}_T${String(tier)}`;
      root.add(
        new Mesh(new BoxGeometry(1, tier, 1), new MeshStandardMaterial({ emissive: 0xffffff })),
      );
      facilities.set(facilityKey(facility, tier), root);
    }
  }
  return { station, sockets, facilities };
}

function account(
  sockets: AccountInfo["outpost"]["sockets"],
  unlocks: AccountInfo["outpost"]["unlocks"],
) {
  return { outpost: { sockets, unlocks } } as unknown as AccountInfo;
}

describe("the outpost scene", () => {
  it("puts each installed module on its socket at the tier it owns", () => {
    const scene = new OutpostScene(models());
    scene.apply(
      account(
        [
          { id: "a", open: true, facility: "refinery", run: 2 },
          { id: "b", open: true, run: 0 },
        ],
        [{ facility: "refinery", tier: 2 }],
      ),
    );
    expect(scene.report().installed).toEqual([
      { socket: "a", facility: "refinery", tier: 2, powered: true },
    ]);
    expect(scene.report().sockets).toEqual(["a", "b"]);
    expect(scene.report().facilities).toHaveLength(6);
  });

  it("replaces a module when it moves, upgrades or loses power", () => {
    const scene = new OutpostScene(models());
    scene.apply(
      account(
        [
          { id: "a", open: true, facility: "sensor_array", run: 1 },
          { id: "b", open: true, run: 0 },
        ],
        [{ facility: "sensor_array", tier: 1 }],
      ),
    );
    scene.apply(
      account(
        [
          { id: "a", open: true, run: 0 },
          { id: "b", open: true, facility: "sensor_array", run: 0 },
        ],
        [{ facility: "sensor_array", tier: 1 }],
      ),
    );
    expect(scene.report().installed).toEqual([
      { socket: "b", facility: "sensor_array", tier: 1, powered: false },
    ]);
  });

  it("picks the socket under the pointer and rings the selection", () => {
    const scene = new OutpostScene(models());
    const camera = new OrthographicCamera(-20, 20, 12, -12, 0.1, 200);
    camera.position.set(0, 50, 0);
    camera.lookAt(0, 0, 0);
    camera.updateMatrixWorld();
    const ray = new Raycaster();
    // Socket A sits at x = -9: a quarter of the way across a 40 m view, from the left.
    ray.setFromCamera(new Vector2(-9 / 20, 0), camera);
    expect(scene.pick(ray)).toBe("a");
    ray.setFromCamera(new Vector2(0.9, 0.9), camera);
    expect(scene.pick(ray)).toBeNull();
    scene.select("b");
    expect(scene.selected).toBe("b");
  });

  it("stands where the dock puts it, with the pilot's ship on the berth until launch", () => {
    const scene = new OutpostScene(models());
    const placement = placeStation({ x: -120, z: 0 }, { x: 70, z: 0 });
    scene.place(placement);
    // Turned to face +x, socket A (station x -9) lies along -z from the station's origin.
    const a = scene.socketPosition("a");
    expect(a?.x).toBeCloseTo(placement.x, 6);
    expect(a?.z).toBeCloseTo(placement.z + 9, 6);
    expect(scene.centre.y).toBe(0);
    const ships = () => {
      const found: boolean[] = [];
      scene.root.traverse((node) => {
        if (node.name === "berth ship") found.push(node.visible);
      });
      return found;
    };
    expect(ships()).toEqual([true, true]);
    const ship = scene.root.getObjectByName("berth ship");
    const at = ship?.getWorldPosition(ship.position.clone());
    expect(at?.x).toBeCloseTo(-120, 1);
    expect(at?.z).toBeCloseTo(0, 1);
    // Drawn on the plane, as the run draws the same ship once it launches.
    expect(at?.y).toBeCloseTo(0, 6);

    const root = scene.launch();
    expect(ships()).toEqual([false, false]);
    expect(root).toBe(scene.root);
    scene.home();
    expect(scene.root.parent).toBe(scene.scene);
    expect(ships()).toEqual([true, true]);
  });

  it("makes the station and its modules structures, so ships behind them show as silhouettes", () => {
    const parts = models();
    parts.station.add(new Mesh(new BoxGeometry(4, 4, 4), new MeshStandardMaterial()));
    const scene = new OutpostScene(parts);
    scene.apply(
      account(
        [{ id: "a", open: true, facility: "refinery", run: 0 }],
        [{ facility: "refinery", tier: 1 }],
      ),
    );
    const refs: number[] = [];
    parts.station.traverse((node) => {
      if (node instanceof Mesh && node.material instanceof MeshStandardMaterial) {
        refs.push(node.material.stencilRef);
      }
    });
    // The hull and the dimmed refinery; pick targets and the ring are not drawn as structure.
    expect(refs).toEqual([STRUCTURE, STRUCTURE]);
  });
});
