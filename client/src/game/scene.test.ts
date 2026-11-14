import { afterEach, describe, expect, it } from "vitest";
import { BoxGeometry, Group, Mesh, MeshStandardMaterial, Vector3 } from "three";
import { ExpeditionScene, SIZE, allegianceOf } from "./scene";
import { FLASH_SECONDS, JumpEffects } from "./jumpEffects";
import type { EntityPose } from "../net/interpolation";
import type { SiteInfo } from "../protocol";
import { GEAR_LIFT, GEAR_SECONDS } from "../view/frigate";
import { SHIP_HEIGHT } from "../view/hulls";
import { RENDER_HEIGHT, STRUCTURE, hullGeometry, useShipModel } from "../view/readability";

/** A stand-in for the frigate: one hull part and one leg, as view/frigate sets them up. */
function model(): Group {
  const ship = new Group();
  ship.add(new Mesh(new BoxGeometry(2, 1, 1), new MeshStandardMaterial({ name: "Armor | test" })));
  const leg = new Mesh(new BoxGeometry(0.1, 0.3, 0.1), new MeshStandardMaterial());
  leg.name = "Frigate_|_landing_strut";
  leg.position.y = -0.4;
  leg.userData.deployedY = -0.4;
  ship.add(leg);
  return ship;
}

afterEach(() => {
  useShipModel(null);
});

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
  it("draws the extractor beam from the ship to the deposit being worked", () => {
    const world = new ExpeditionScene(1);
    world.setSite(
      SITE,
      [],
      [
        { x: 40, z: 0, pocket: 0, material: "ore", units: 8 },
        { x: 50, z: 10, pocket: 0, material: "ore", units: 8 },
      ],
    );
    expect(world.beam.visible).toBe(false);
    world.setBeam({ x: 46, z: 10 }, 1);
    expect(world.beam.visible).toBe(true);
    expect(world.beam.position.x).toBeCloseTo(48);
    expect(world.beam.position.z).toBeCloseTo(10);
    expect(world.beam.scale.x).toBeGreaterThan(4);
    // It leaves from the keel, never the roof, where it was drawn across the hull.
    const half = new Vector3(world.beam.scale.x / 2, 0, 0).applyQuaternion(world.beam.quaternion);
    const start = world.beam.position.clone().sub(half);
    const end = world.beam.position.clone().add(half);
    expect(start.x).toBeCloseTo(46);
    expect(start.y).toBeCloseTo(-SHIP_HEIGHT / 2);
    expect(end.x).toBeCloseTo(50);
    expect(end.y).toBeGreaterThan(0);
    world.setBeam({ x: 46, z: 10 }, null);
    expect(world.beam.visible).toBe(false);
    world.setBeam({ x: 46, z: 10 }, 7);
    expect(world.beam.visible).toBe(false);
  });

  it("leans a turning ship into the turn and levels it when the turn ends", () => {
    const world = new ExpeditionScene(1);
    let heading = 0;
    world.update([pose(1, "playerShip")], null, undefined, 1 / 60);
    for (let i = 0; i < 30; i++) {
      heading += 2.4 / 60;
      world.update([{ ...pose(1, "playerShip"), heading }], null, undefined, 1 / 60);
    }
    const drawn = world.ship(1);
    expect(drawn?.bank).toBeGreaterThan(0.3);
    expect(drawn?.hull.rotation.x).toBe(drawn?.bank);
    expect(drawn?.hull.position.y).toBe(RENDER_HEIGHT);
    for (let i = 0; i < 60; i++) {
      world.update([{ ...pose(1, "playerShip"), heading }], null, undefined, 1 / 60);
    }
    expect(Math.abs(world.ship(1)?.bank ?? 1)).toBeLessThan(0.01);
  });

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
    expect((world.ship(3)?.hull as Mesh | undefined)?.geometry).toBe(hullGeometry("hostile"));
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
    const dock = world.scene.getObjectByName("stand-in dock");
    dock?.traverse((node) => {
      if (node instanceof Mesh) {
        expect((node.material as MeshStandardMaterial).stencilRef).toBe(STRUCTURE);
      }
    });
    expect(dock).toBeDefined();
  });

  it("draws the outpost's station at the dock in place of the stand-in, across runs", () => {
    const world = new ExpeditionScene(1);
    const station = new Group();
    station.add(new Mesh(new BoxGeometry(1, 1, 1), new MeshStandardMaterial()));
    world.setSite(SITE, [], []);
    world.setStation(station);
    const standIn = () => world.scene.getObjectByName("stand-in dock");
    expect(standIn()?.visible).toBe(false);
    expect(station.parent?.name).toBe("dock");
    // A new site clears and disposes the last one's meshes, but never the station's.
    world.setSite(SITE, [], []);
    expect(standIn()?.visible).toBe(false);
    expect(station.parent?.name).toBe("dock");
    world.setStation(null);
    expect(station.parent).toBeNull();
    expect(standIn()?.visible).toBe(true);
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

  it("lowers a player ship's gear on the berth and folds it once the ship lifts off", () => {
    useShipModel(model());
    const world = new ExpeditionScene(1);
    world.setSite(SITE, [], []);
    const leg = () => {
      let y = Number.NaN;
      world.ship(1)?.hull.traverse((node) => {
        if (node.name.includes("landing_strut")) y = node.position.y;
      });
      return y;
    };
    world.update([pose(1, "playerShip", SITE.dockX, SITE.dockZ + 6)], null, undefined, 0.1);
    expect(leg()).toBeCloseTo(-0.4, 6);
    // Off the berth the legs fold over the gear's time, not at once.
    world.update([pose(1, "playerShip", SITE.dockX + 3, 0)], null, undefined, GEAR_SECONDS / 2);
    expect(leg()).toBeGreaterThan(-0.4);
    expect(leg()).toBeLessThan(-0.4 + GEAR_LIFT);
    world.update([pose(1, "playerShip", SITE.dockX + 6, 0)], null, undefined, GEAR_SECONDS);
    expect(leg()).toBeCloseTo(-0.4 + GEAR_LIFT, 6);
    // A ship first seen in flight is drawn with its gear up.
    world.update([pose(2, "playerShip", 40, 0)], null, undefined, 0.016);
    let folded = Number.NaN;
    world.ship(2)?.hull.traverse((node) => {
      if (node.name.includes("landing_strut")) folded = node.position.y;
    });
    expect(folded).toBeCloseTo(-0.4 + GEAR_LIFT, 6);
  });

  it("draws ships in flight anew once the hull model loads", () => {
    const world = new ExpeditionScene(1);
    world.update([pose(1, "playerShip"), pose(3, "raider")], null);
    expect(world.ship(1)?.hull).toBeInstanceOf(Mesh);
    useShipModel(model());
    world.redraw();
    world.update([pose(1, "playerShip"), pose(3, "raider")], null);
    expect(world.ship(1)?.hull).toBeInstanceOf(Group);
    expect(world.ship(1)?.hull.parent).toBe(world.scene);
    // Hostiles have no model yet.
    expect(world.ship(3)?.hull).toBeInstanceOf(Mesh);
  });

  it("rings the ship whose jump drive spools, closer as the jump nears", () => {
    const scene = new ExpeditionScene(1);
    const jumps = new JumpEffects();
    scene.update([pose(1, "playerShip", 10, 4)], null, undefined, 0, jumps);
    expect(scene.jumpSpool.visible).toBe(false);

    jumps.setCharge(1, 0);
    scene.update([pose(1, "playerShip", 10, 4)], null, undefined, 0, jumps);
    expect(scene.jumpSpool.visible).toBe(true);
    expect(scene.jumpSpool.position.x).toBe(10);
    expect(scene.jumpSpool.position.z).toBe(4);
    const countdown = () => scene.jumpSpool.getObjectByName("countdown")?.scale.x ?? 0;
    const far = countdown();
    expect(scene.jumpSpool.getObjectByName("sparks")?.visible).toBe(true);
    expect(scene.ship(1)?.hull.scale.x).toBe(1);

    jumps.setCharge(1, 1);
    scene.update([pose(1, "playerShip", 10, 4)], null, undefined, 0, jumps);
    expect(countdown()).toBeLessThan(far);
    // The hull draws out along its length at the very end, and thins to match.
    const hull = scene.ship(1)?.hull.scale;
    expect(hull?.x).toBeGreaterThan(1.3);
    expect(hull?.z).toBeLessThan(1);
    expect(hull?.y).toBe(1);

    // No ring without the ship it is on, or once the spool ends.
    scene.update([], null, undefined, 0, jumps);
    expect(scene.jumpSpool.visible).toBe(false);
    jumps.setCharge(null);
    scene.update([pose(1, "playerShip", 10, 4)], null, undefined, 0, jumps);
    expect(scene.jumpSpool.visible).toBe(false);
    expect(scene.ship(1)?.hull.scale.x).toBe(1);
  });

  it("keeps the countdown but flies nothing and stretches nothing with reduced motion", () => {
    const scene = new ExpeditionScene(1);
    scene.configureFeedback(false, 0);
    const jumps = new JumpEffects();
    jumps.setCharge(1, 1);
    scene.update([pose(1, "playerShip", 10, 4)], null, undefined, 0, jumps);
    expect(scene.jumpSpool.visible).toBe(true);
    expect(scene.jumpSpool.getObjectByName("countdown")?.visible).toBe(true);
    expect(scene.jumpSpool.getObjectByName("sparks")?.visible).toBe(false);
    expect(scene.ship(1)?.hull.scale.x).toBe(1);
  });

  it("flashes at both ends of a jump, with the streak along the way the ship went", () => {
    const scene = new ExpeditionScene(1);
    const jumps = new JumpEffects();
    jumps.observe([pose(2, "playerShip", 0, 0)], 0.016);
    jumps.observe([pose(2, "playerShip", 0, 300)], 0.016);
    jumps.advance(FLASH_SECONDS / 2);
    scene.update([pose(2, "playerShip", 0, 300)], null, undefined, 0, jumps);
    const [leave, arrive] = scene.jumpFlashes;
    expect(scene.jumpFlashes).toHaveLength(2);
    expect(leave?.position.z).toBe(0);
    expect(arrive?.position.z).toBe(300);
    // The ship went toward +z: the streak leads from where it left and trails where it arrived.
    const streakOf = (flash: typeof leave) =>
      flash?.getObjectByName("streak")?.getWorldPosition(new Vector3()) ?? new Vector3();
    scene.scene.updateMatrixWorld();
    expect(streakOf(leave).z).toBeGreaterThan(1);
    expect(streakOf(arrive).z).toBeLessThan(300);
    expect(Math.abs(streakOf(leave).x)).toBeLessThan(1e-6);

    // A short hop's streak stops where the hop ends.
    jumps.observe([pose(2, "playerShip", 0, 330)], 0.016);
    jumps.advance(FLASH_SECONDS * 0.9);
    scene.update([pose(2, "playerShip", 0, 330)], null, undefined, 0, jumps);
    const hop = scene.jumpFlashes.at(-2)?.getObjectByName("streak");
    expect(hop?.scale.x).toBeGreaterThan(20);
    expect(hop?.scale.x).toBeLessThanOrEqual(30);

    // Faded, they are gone.
    jumps.advance(FLASH_SECONDS);
    scene.update([pose(2, "playerShip", 0, 300)], null, undefined, 0, jumps);
    expect(scene.jumpFlashes).toHaveLength(0);
  });
});

it("recognizes each authored berth and does not lower gear at the apron centre", () => {
  useShipModel(model());
  const world = new ExpeditionScene(0);
  world.setSite(SITE, [], []);
  const ships = [6, 2, -2, -6].map((z, id) => pose(id, "playerShip", SITE.dockX, z));
  world.update(ships, null, undefined, 0.1);
  for (const ship of ships) expect(world.ship(ship.id)?.lowered).toBe(1);
  world.update([pose(4, "playerShip", SITE.dockX, 0)], null, undefined, 0.1);
  expect(world.ship(4)?.lowered).toBe(0);
});

it("confirms hull and own-shield loss without flashing another ship or a new contact", () => {
  useShipModel(model());
  const world = new ExpeditionScene(1);
  world.configureFeedback(true, 40);
  const own = pose(1, "playerShip");
  const ally = pose(2, "playerShip", 4);
  world.update([own, ally], null, undefined, 0.016);
  expect(world.ship(1)?.hit).toBe(0);
  world.update([{ ...own, hull: 200 }, ally], null, undefined, 0.016);
  expect(world.ship(1)?.hit).toBeGreaterThan(0);
  expect(world.ship(2)?.hit).toBe(0);
  expect(world.ship(1)?.feedback[0]?.material).not.toBe(world.ship(2)?.feedback[0]?.material);
  world.update([{ ...own, hull: 200 }, ally], null, undefined, 1);
  world.configureFeedback(true, 30);
  world.update([{ ...own, hull: 200 }, ally], null, undefined, 0.016);
  expect(world.ship(1)?.hit).toBeGreaterThan(0);
  world.configureFeedback(false, 20);
  world.update([{ ...own, hull: 100 }, ally], null, undefined, 0.016);
  expect(world.ship(1)?.hit).toBe(0);
  world.update([pose(3, "drone")], null, undefined, 0.016);
  expect(world.ship(3)?.hit).toBe(0);
});
