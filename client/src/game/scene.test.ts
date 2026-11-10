import { describe, expect, it } from "vitest";
import { Mesh } from "three";
import type { MeshStandardMaterial } from "three";
import { ExpeditionScene, SIZE, allegianceOf } from "./scene";
import type { EntityPose } from "../net/interpolation";
import type { SiteInfo } from "../protocol";
import { RENDER_HEIGHT, STRUCTURE, hullGeometry } from "../view/readability";

const SITE: SiteInfo = {
  name: "Test",
  dockX: -120,
  dockZ: 0,
  centreX: 70,
  centreZ: 0,
  radius: 125,
  dockRadius: 15,
  pockets: [
    { kind: "oreField", x: 45, z: -10, radius: 36, locked: false, material: "ore" },
    { kind: "derelict", x: 115, z: 65, radius: 24, locked: true, material: "salvage" },
  ],
};

const pose = (id: number, kind: EntityPose["kind"], x = 0, z = 0): EntityPose => ({
  id,
  kind,
  x,
  z,
  heading: 0,
  hull: 255,
});

describe("allegianceOf", () => {
  it("makes the player's own ship self, other pilots allies and every NPC hostile", () => {
    expect(allegianceOf("playerShip", true)).toBe("self");
    expect(allegianceOf("playerShip", false)).toBe("ally");
    for (const kind of ["drone", "raider", "warden"] as const) {
      expect(allegianceOf(kind, false)).toBe("hostile");
    }
  });
});

describe("ExpeditionScene", () => {
  it("draws every ship at one height with its allegiance's shape and a silhouette", () => {
    const world = new ExpeditionScene(1);
    world.update(
      [pose(1, "playerShip", 3, 4), pose(2, "playerShip"), pose(3, "warden", -9, 2)],
      null,
    );
    for (const id of [1, 2, 3]) {
      const drawn = world.ship(id);
      expect(drawn?.hull.position.y).toBe(RENDER_HEIGHT);
      expect(drawn?.silhouette.position.y).toBe(RENDER_HEIGHT);
      expect(drawn?.silhouette.parent).toBe(world.scene);
    }
    expect(world.ship(1)?.allegiance).toBe("self");
    expect(world.ship(2)?.allegiance).toBe("ally");
    expect(world.ship(3)?.hull.geometry).toBe(hullGeometry("hostile"));
    expect(world.ship(3)?.hull.scale.x).toBe(SIZE.warden);
  });

  it("drops ships that leave and redraws one whose allegiance changes", () => {
    const world = new ExpeditionScene(1);
    world.update([pose(1, "playerShip"), pose(2, "playerShip")], null);
    const before = world.ship(2);
    world.setOwnShip(2);
    world.update([pose(2, "playerShip")], null);
    expect(world.ship(1)).toBeUndefined();
    expect(world.ship(2)?.allegiance).toBe("self");
    expect(world.ship(2)?.hull).not.toBe(before?.hull);
    expect(before?.hull.parent).toBeNull();
  });

  it("brackets the locked ship, sized to it, and hides the bracket without a lock", () => {
    const world = new ExpeditionScene(1);
    const ships = [pose(1, "playerShip"), pose(7, "warden", 20, -5)];
    world.update(ships, 7);
    expect(world.bracket.visible).toBe(true);
    expect(world.bracket.position.toArray()).toEqual([20, RENDER_HEIGHT, -5]);
    expect(world.bracket.scale.x).toBe(SIZE.warden);
    world.update(ships, null);
    expect(world.bracket.visible).toBe(false);
    world.update([pose(1, "playerShip")], 7);
    expect(world.bracket.visible).toBe(false);
  });

  it("makes obstacles and the dock structures, so ships behind them show as silhouettes", () => {
    const world = new ExpeditionScene(1);
    world.setSite(SITE, [{ x: 5, z: 5, radius: 3 }], []);
    const rock = world.obstacles[0] as Mesh<never, MeshStandardMaterial> | undefined;
    expect(rock?.material.stencilWrite).toBe(true);
    expect(rock?.material.stencilRef).toBe(STRUCTURE);
    const dock = world.scene.getObjectByName("dock");
    dock?.traverse((node) => {
      if (node instanceof Mesh) {
        expect((node.material as MeshStandardMaterial).stencilRef).toBe(STRUCTURE);
      }
    });
    expect(dock).toBeDefined();
  });

  it("shows a locked pocket by a caged beacon, an open one by a crystal", () => {
    const world = new ExpeditionScene(1);
    world.setSite(SITE, [], []);
    const [open, locked] = world.pockets;
    expect(open?.getObjectByName("open")).toBeDefined();
    expect(open?.getObjectByName("locked")).toBeUndefined();
    expect(locked?.getObjectByName("locked")).toBeDefined();
  });

  it("shrinks a deposit as it empties and replaces the site on the next run", () => {
    const world = new ExpeditionScene(1);
    const deposits = [
      { x: 40, z: 0, pocket: 0, material: "ore" as const, units: 8 },
      { x: 50, z: 0, pocket: 0, material: "ore" as const, units: 8 },
    ];
    world.setSite(SITE, [{ x: 5, z: 5, radius: 3 }], deposits);
    world.setDeposits([8, 4]);
    const [full, half] = world.depositScales;
    expect(full).toBe(1);
    expect(half).toBeLessThan(1);
    world.setDeposits([0, 4]);
    expect(world.depositScales[0]).toBeLessThan(half ?? 0);

    world.setSite(SITE, [], deposits.slice(0, 1));
    expect(world.obstacles).toHaveLength(0);
    expect(world.depositScales).toHaveLength(1);
  });

  it("drops every ship on reset, for a new run that numbers ships afresh", () => {
    const world = new ExpeditionScene(1);
    world.update([pose(1, "playerShip"), pose(2, "drone")], 2);
    world.reset(0);
    expect(world.ship(1)).toBeUndefined();
    expect(world.bracket.visible).toBe(false);
    world.update([pose(0, "playerShip"), pose(1, "raider")], null);
    expect(world.ship(0)?.allegiance).toBe("self");
    expect(world.ship(1)?.kind).toBe("raider");
  });

  it("redraws a ship whose kind changed under the same id", () => {
    const world = new ExpeditionScene(1);
    world.update([pose(3, "drone")], null);
    world.update([pose(3, "warden")], null);
    expect(world.ship(3)?.hull.scale.x).toBe(SIZE.warden);
  });
});
