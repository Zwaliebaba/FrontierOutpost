import { describe, expect, it } from "vitest";
import type { Mesh, MeshStandardMaterial } from "three";
import { ExpeditionScene, SIZE, allegianceOf } from "./scene";
import type { EntityPose } from "../net/interpolation";
import { RENDER_HEIGHT, STRUCTURE, hullGeometry } from "../view/readability";

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

  it("makes obstacles structures, so ships behind them show as silhouettes", () => {
    const world = new ExpeditionScene(1);
    world.setObstacles([{ x: 5, z: 5, radius: 3 }]);
    const rock = world.obstacles[0] as Mesh<never, MeshStandardMaterial> | undefined;
    expect(rock?.material.stencilWrite).toBe(true);
    expect(rock?.material.stencilRef).toBe(STRUCTURE);
  });
});
