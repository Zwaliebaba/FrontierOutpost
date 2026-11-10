import { describe, expect, it } from "vitest";
import { MAX_BOLTS, SCENE_IDS, createWorld, stepWorld } from "./scenarios";

const idle = { thrust: 0, turn: 0 };

function run(scene: (typeof SCENE_IDS)[number], seed: number, seconds: number) {
  const world = createWorld(scene, seed, "varied");
  let peakBolts = 0;
  for (let i = 0; i < seconds * 60; i += 1) {
    stepWorld(world, idle, 1 / 60);
    peakBolts = Math.max(peakBolts, world.bolts.length);
  }
  return { world, peakBolts };
}

describe("greybox scenarios", () => {
  it.each(SCENE_IDS)("%s has one player, three allies and eight hostiles", (scene) => {
    const { ships } = createWorld(scene, 1, "flat");
    const count = (allegiance: string) => ships.filter((s) => s.allegiance === allegiance).length;
    expect([count("self"), count("ally"), count("hostile")]).toEqual([1, 3, 8]);
    expect(new Set(ships.map((ship) => ship.id)).size).toBe(ships.length);
  });

  it("replays identically from the same seed and differs for another", () => {
    const a = run("station", 7, 20).world;
    const b = run("station", 7, 20).world;
    const c = run("station", 8, 20).world;
    expect(a.ships.map((ship) => ship.state)).toEqual(b.ships.map((ship) => ship.state));
    expect(a.ships.map((ship) => ship.state)).not.toEqual(c.ships.map((ship) => ship.state));
  });

  it("keeps every ship on one height in flat mode and spreads them in varied mode", () => {
    expect(createWorld("rock", 3, "flat").ships.every((ship) => ship.height === 0)).toBe(true);
    const varied = createWorld("rock", 3, "varied").ships;
    expect(varied.find((ship) => ship.allegiance === "self")?.height).toBe(0);
    expect(varied.some((ship) => Math.abs(ship.height) > 0.5)).toBe(true);
  });

  it.each(SCENE_IDS)("%s keeps ships moving, in the arena, and firing under the cap", (scene) => {
    const start = createWorld(scene, 1, "flat").ships.map((ship) => ({ ...ship.state }));
    const { world, peakBolts } = run(scene, 1, 30);
    expect(peakBolts).toBeGreaterThan(0);
    expect(peakBolts).toBeLessThanOrEqual(MAX_BOLTS);
    world.ships.forEach((ship, index) => {
      expect(Math.hypot(ship.state.x, ship.state.z)).toBeLessThan(30);
      if (ship.allegiance !== "self") expect(ship.state).not.toEqual(start[index]);
    });
  });

  it("only uses structures in the scenes that need them", () => {
    expect(createWorld("crossing", 1, "flat").occluders).toEqual([]);
    expect(createWorld("headon", 1, "flat").occluders).toEqual([]);
    expect(createWorld("rock", 1, "flat").occluders).toHaveLength(1);
    expect(createWorld("station", 1, "flat").occluders.length).toBeGreaterThan(8);
  });
});
