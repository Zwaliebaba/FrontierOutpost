import { Group, Mesh, MeshStandardMaterial, OrthographicCamera, Raycaster, Vector2 } from "three";
import { BoxGeometry } from "three";
import type { Object3D } from "three";
import { describe, expect, it } from "vitest";
import type { AccountInfo } from "../protocol";
import { facilityKey } from "./models";
import type { OutpostModels } from "./models";
import { STRUCTURE } from "../view/readability";
import { placeStation } from "./placement";
import { OutpostScene } from "./scene";

const box = (size = 1) => new Mesh(new BoxGeometry(size, size, size), new MeshStandardMaterial());

/**
 * A station with three sockets, their hardpoints, a parking place and every module and tell, as
 * small stand-ins, laid out as the pipeline's are (glTF: Blender +Y is −z).
 */
function models(): OutpostModels {
  const exterior = new Group();
  exterior.add(box(30));
  const interior = new Group();
  interior.add(box(2));
  const sockets = new Map<string, Object3D>();
  const hardpoints = new Map<string, Object3D>();
  const closures = new Map<string, Object3D>();
  for (const [id, x, z] of [
    ["a", -6, 6],
    ["b", 6, 0],
    ["c", -6, -6],
  ] as const) {
    const anchor = new Group();
    anchor.name = `Socket_${id.toUpperCase()}`;
    anchor.position.set(x, 0, z);
    const closure = new Group();
    closure.name = `Closure_${id.toUpperCase()}`;
    closure.add(box(0.5));
    anchor.add(closure);
    interior.add(anchor);
    sockets.set(id, anchor);
    closures.set(id, closure);
    const hardpoint = new Group();
    hardpoint.name = `Tell_${id.toUpperCase()}`;
    hardpoint.position.set(x * 2, 1.5, z * 2);
    exterior.add(hardpoint);
    hardpoints.set(id, hardpoint);
  }
  const reactor = new Group();
  reactor.add(box(0.8));
  reactor.name = "Reactor_Upgrade drum";
  interior.add(reactor);
  const parking = new Group();
  parking.name = "Parking_frigate";
  parking.position.set(1, 0, -10);
  interior.add(parking);
  const facilities = new Map<string, Group>();
  const tells = new Map<string, Group>();
  for (const facility of ["sensor_array", "refinery", "fabrication_bay"]) {
    for (const tier of [1, 2]) {
      const root = new Group();
      root.name = `Facility_${facility}_T${String(tier)}`;
      root.add(
        new Mesh(new BoxGeometry(1, tier, 1), new MeshStandardMaterial({ emissive: 0xffffff })),
      );
      for (let level = 1; level <= tier; level++) {
        const indicator = new Mesh(new BoxGeometry(0.1, 0.1, 0.1), new MeshStandardMaterial());
        indicator.name = `Operating_Tier_${String(level)}`;
        root.add(indicator);
      }
      facilities.set(facilityKey(facility, tier), root);
      const tell = new Group();
      tell.name = `Tell_${facility}_T${String(tier)}`;
      tell.add(box(0.5 * tier));
      tells.set(facilityKey(facility, tier), tell);
    }
  }
  return {
    exterior,
    interior,
    sockets,
    hardpoints,
    closures,
    parking: new Map([["frigate", parking]]),
    reactor: [reactor],
    facilities,
    tells,
  };
}

function account(
  sockets: AccountInfo["outpost"]["sockets"],
  unlocks: AccountInfo["outpost"]["unlocks"],
) {
  return { outpost: { sockets, unlocks } } as unknown as AccountInfo;
}

/** A camera straight down on the origin, 40 m across, and a ray through `(x, z)` on the plane. */
function rayAt(x: number, z: number): Raycaster {
  const camera = new OrthographicCamera(-20, 20, 12, -12, 0.1, 200);
  camera.position.set(0, 50, 0);
  camera.up.set(0, 0, -1);
  camera.lookAt(0, 0, 0);
  camera.updateMatrixWorld();
  const ray = new Raycaster();
  ray.setFromCamera(new Vector2(x / 20, -z / 12), camera);
  return ray;
}

/** Facing −z, the station's frame is the models' own: no turn. */
const UNTURNED = placeStation({ centreX: 0, centreZ: 0, forwardX: 0, forwardZ: -1 });

describe("the outpost scene", () => {
  it("puts each installed module on its socket at the tier it owns, and its tell outside", () => {
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
    const report = scene.report();
    expect(report.installed).toEqual([
      { socket: "a", facility: "refinery", tier: 2, powered: true },
    ]);
    expect(report.tells).toEqual([
      { hardpoint: "a", facility: "refinery", tier: 2, powered: true },
    ]);
    expect(report.sockets).toEqual(["a", "b", "c"]);
    expect(report.facilities).toHaveLength(6);
  });

  it("replaces a module and its tell when it moves, upgrades or loses power", () => {
    const parts = models();
    const scene = new OutpostScene(parts);
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
    expect(scene.report().tells).toEqual([
      { hardpoint: "b", facility: "sensor_array", tier: 1, powered: false },
    ]);
    expect(parts.hardpoints.get("a")?.children).toHaveLength(0);
    // Upgraded, the tell grows.
    const size = () => {
      const tell = parts.hardpoints.get("b")?.children[0];
      const mesh = tell?.children[0];
      return mesh instanceof Mesh ? (mesh.geometry as BoxGeometry).parameters.width : 0;
    };
    const before = size();
    scene.apply(
      account(
        [
          { id: "a", open: true, run: 0 },
          { id: "b", open: true, facility: "sensor_array", run: 2 },
        ],
        [{ facility: "sensor_array", tier: 2 }],
      ),
    );
    expect(scene.report().tells).toEqual([
      { hardpoint: "b", facility: "sensor_array", tier: 2, powered: true },
    ]);
    expect(size()).toBeGreaterThan(before);
    // The reactor's hardpoint stays empty until the reactor is bought (plan M10).
    expect(scene.report().tells.some((tell) => tell.hardpoint === "reactor")).toBe(false);
  });

  it("draws the exterior until the station is selected, then the interior in its place", () => {
    const parts = models();
    const scene = new OutpostScene(parts);
    scene.place(UNTURNED);
    expect(scene.report().view).toBe("exterior");
    expect(parts.exterior.visible).toBe(true);
    expect(parts.interior.visible).toBe(false);
    // Only the exterior is selectable from outside, and only the station.
    expect(scene.pick(rayAt(-6, 6))).toBeNull();
    expect(scene.pickStation(rayAt(0, 0))).toBe(true);
    expect(scene.pickStation(rayAt(19, 11))).toBe(false);
    scene.setView("interior");
    expect(parts.exterior.visible).toBe(false);
    expect(parts.interior.visible).toBe(true);
    expect(scene.report().view).toBe("interior");
    expect(scene.pickStation(rayAt(0, 0))).toBe(false);
    // Both share the station's transform: the interior replaces the exterior in place.
    expect(parts.interior.parent).toBe(parts.exterior.parent);
  });

  it("picks an open socket under the pointer in the interior and rings the selection", () => {
    const scene = new OutpostScene(models());
    scene.place(UNTURNED);
    expect(UNTURNED.yaw).toBeCloseTo(0, 12);
    scene.apply(
      account(
        [
          { id: "a", open: true, run: 0 },
          { id: "b", open: false, run: 0 },
        ],
        [],
      ),
    );
    expect(scene.pick(rayAt(-6, 6))).toBeNull();
    scene.setView("interior");
    expect(scene.pick(rayAt(-6, 6))).toBe("a");
    // Socket B is closed, and socket C has no place in the account until the reactor.
    expect(scene.pick(rayAt(6, 0))).toBeNull();
    expect(scene.pick(rayAt(-6, -6))).toBeNull();
    expect(scene.pick(rayAt(18, 11))).toBeNull();
    scene.select("a");
    expect(scene.selected).toBe("a");
  });

  it("plates closed sockets, socket C until the reactor, and hides the reactor", () => {
    const parts = models();
    const scene = new OutpostScene(parts);
    const layout = (open: boolean) =>
      account(
        [
          { id: "a", open: true, run: 0 },
          { id: "b", open, run: 0 },
        ],
        [],
      );
    scene.apply(layout(false));
    expect(scene.report().closed).toEqual(["b", "c"]);
    scene.apply(layout(true));
    expect(scene.report().closed).toEqual(["c"]);
    expect(parts.reactor.every((part) => !part.visible)).toBe(true);
  });

  it("stands where the rules put it, with the pilot's ship parked inside until launch", () => {
    const parts = models();
    const scene = new OutpostScene(parts);
    const placement = placeStation({ centreX: -120, centreZ: 0, forwardX: 1, forwardZ: 0 });
    scene.place(placement);
    expect(scene.centre.toArray()).toEqual([-120, 0, 0]);
    // Facing +x: Blender +Y (glTF −z) runs along +x, Blender +X along +z.
    const a = scene.socketPosition("a");
    expect(a?.x).toBeCloseTo(-120 - 6, 6);
    expect(a?.z).toBeCloseTo(-6, 6);
    expect(scene.report().parked).toEqual(["frigate"]);
    const ship = scene.root.getObjectByName("parked ship");
    const at = ship?.getWorldPosition(ship.position.clone());
    expect(at?.x).toBeCloseTo(-110, 4);
    expect(at?.z).toBeCloseTo(1, 4);
    // Drawn on the plane, as the run draws the same ship once it launches.
    expect(at?.y).toBeCloseTo(0, 6);

    scene.setView("interior");
    const root = scene.launch();
    expect(root).toBe(scene.root);
    // Only the exterior stands at the dock; the ship flies from snapshots.
    expect(parts.exterior.visible).toBe(true);
    expect(parts.interior.visible).toBe(false);
    expect(scene.report()).toMatchObject({ view: "exterior", parked: [] });
    scene.home();
    expect(scene.root.parent).toBe(scene.scene);
    expect(scene.report()).toMatchObject({ view: "interior", parked: ["frigate"] });
  });

  it("makes the station, its modules and tells structures, so ships behind show as silhouettes", () => {
    const parts = models();
    const scene = new OutpostScene(parts);
    scene.apply(
      account(
        [{ id: "a", open: true, facility: "refinery", run: 0 }],
        [{ facility: "refinery", tier: 1 }],
      ),
    );
    const refs: number[] = [];
    for (const root of [parts.exterior, parts.interior]) {
      root.traverse((node) => {
        const ancestry: string[] = [node.name];
        node.traverseAncestors((above) => ancestry.push(above.name));
        if (ancestry.includes("parked ship")) return;
        if (node instanceof Mesh && node.material instanceof MeshStandardMaterial) {
          refs.push(node.material.stencilRef);
        }
      });
    }
    // Pick targets, the ring and the parked ship are not drawn as structure.
    expect(refs.length).toBeGreaterThanOrEqual(4);
    expect(new Set(refs)).toEqual(new Set([STRUCTURE]));
  });
});

it("marks a facility's socket inside with its tier and running state, not its model", () => {
  const pack = models();
  const scene = new OutpostScene(pack);
  const layout = (run: number) =>
    account(
      [
        { id: "a", open: true, facility: "sensor_array", run },
        { id: "b", open: true, run: 0 },
      ],
      [{ facility: "sensor_array", tier: 2 }],
    );
  const lit = () => {
    let count = 0;
    pack.sockets.get("a")?.traverse((node) => {
      if (node.name.endsWith("_lit")) count += 1;
    });
    return count;
  };
  scene.apply(layout(1));
  const socket = pack.sockets.get("a");
  // The facility shows outside only (owner decision, 15 November 2026): no module inside.
  expect(socket?.getObjectByName("Facility_sensor_array_T2")).toBeUndefined();
  expect(scene.report().modules).toBe(0);
  expect(socket?.getObjectByName("Service_sensor_array_T2")).toBeDefined();
  expect(lit()).toBe(1);
  scene.apply(layout(2));
  expect(lit()).toBe(2);
  scene.apply(layout(0));
  // Built at tier 2, running none: the marker keeps the tier, with no pip lit.
  expect(socket?.getObjectByName("Service_sensor_array_T2")).toBeDefined();
  expect(lit()).toBe(0);
  expect(scene.report().installed).toEqual([
    { socket: "a", facility: "sensor_array", tier: 2, powered: false },
  ]);
});
