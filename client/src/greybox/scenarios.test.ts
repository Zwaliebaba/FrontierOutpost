import { describe, expect, it } from "vitest";
import { contains } from "./occlusion";
import { MAX_BOLTS, SCENE_IDS, createWorld, stepWorld } from "./scenarios";
import type { SceneId, World } from "./scenarios";
import {
  DEFAULT_STATION,
  MOUTH_FACINGS,
  STATION_SCALES,
  hangarOf,
  stationReach,
} from "./stationProxy";
import type { StationSetup } from "./stationProxy";

const idle = { thrust: 0, turn: 0 };
const STAGE_1: StationSetup = { scale: 1, facing: "site" };

function run(
  scene: SceneId,
  seed: number,
  seconds: number,
  station?: StationSetup,
  each?: (world: World) => void,
) {
  const world = createWorld(scene, seed, "varied", station);
  let peakBolts = 0;
  for (let i = 0; i < seconds * 60; i += 1) {
    stepWorld(world, idle, 1 / 60);
    peakBolts = Math.max(peakBolts, world.bolts.length);
    each?.(world);
  }
  return { world, peakBolts };
}

const fromCentre = (world: World, id: string) => {
  const ship = world.ships.find((candidate) => candidate.id === id);
  return ship ? Math.hypot(ship.state.x, ship.state.z) : Number.NaN;
};

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
    // The station scene's arena grows with its station.
    const arena =
      scene === "station" ? 30 + stationReach(DEFAULT_STATION) - stationReach(STAGE_1) : 30;
    expect(peakBolts).toBeGreaterThan(0);
    expect(peakBolts).toBeLessThanOrEqual(MAX_BOLTS);
    world.ships.forEach((ship, index) => {
      expect(Math.hypot(ship.state.x, ship.state.z)).toBeLessThan(arena);
      if (ship.allegiance !== "self") expect(ship.state).not.toEqual(start[index]);
    });
  });

  it("only uses structures in the scenes that need them", () => {
    expect(createWorld("crossing", 1, "flat").occluders).toEqual([]);
    expect(createWorld("headon", 1, "flat").occluders).toEqual([]);
    expect(createWorld("rock", 1, "flat").occluders).toHaveLength(1);
    expect(createWorld("station", 1, "flat").occluders.length).toBeGreaterThan(8);
  });

  it("leaves the other scenes as they were whatever the station", () => {
    for (const scene of ["crossing", "rock", "headon"] as const) {
      expect(createWorld(scene, 2, "varied", { scale: 3, facing: "away" })).toEqual(
        createWorld(scene, 2, "varied", STAGE_1),
      );
    }
  });
});

describe("greybox station scene", () => {
  it("at scale 1 is the Stage 1 scene: the tester 13 m out, traffic from 9.5 to 14.3 m", () => {
    const world = createWorld("station", 1, "flat", STAGE_1);
    expect(fromCentre(world, "self")).toBeCloseTo(13);
    const radii = world.ships
      .filter((ship) => ship.allegiance !== "self")
      .map((ship) => Math.hypot(ship.state.x, ship.state.z));
    expect(Math.min(...radii)).toBeCloseTo(9.5);
    expect(Math.max(...radii)).toBeCloseTo(14.3);
  });

  it("uses the ring at 2.2, mouth toward the site, by default", () => {
    expect(createWorld("station", 4, "varied")).toEqual(
      createWorld("station", 4, "varied", { scale: 2.2, facing: "site" }),
    );
  });

  it.each(STATION_SCALES.filter((scale) => scale !== 1))(
    "moves the tester and the orbiting traffic out as far as the ring grows at %s",
    (scale) => {
      const station: StationSetup = { scale, facing: "camera" };
      const world = createWorld("station", 1, "flat", station);
      const grown = stationReach(station) - stationReach(STAGE_1);
      expect(fromCentre(world, "self")).toBeCloseTo(13 + grown);
      // Every ship but the hangar's starts clear of the station.
      for (const id of ["H2", "H3", "H4", "H6", "H7", "H8", "A2", "A3"]) {
        expect(fromCentre(world, id)).toBeGreaterThan(stationReach(station) + 2);
        expect(fromCentre(world, id)).toBeLessThan(stationReach(station) + 7.5);
      }
    },
  );

  const rings = STATION_SCALES.filter((scale) => scale !== 1).flatMap((scale) =>
    MOUTH_FACINGS.map((facing): StationSetup => ({ scale, facing })),
  );

  it.each(rings)(
    "flies two hostiles and an ally in and out of the hangar at $scale, $facing",
    (station) => {
      const hangar = hangarOf(station);
      if (!hangar) throw new Error("a ring has a hangar");
      const inHangar = new Set<string>();
      const inLane = new Set<string>();
      const inStructure = new Set<string>();
      run("station", 1, 60, station, (world) => {
        for (const ship of world.ships) {
          const { x, z } = ship.state;
          const along = x * Math.cos(hangar.bearing) + z * Math.sin(hangar.bearing);
          const across = -x * Math.sin(hangar.bearing) + z * Math.cos(hangar.bearing);
          if (Math.abs(across) < hangar.halfWidth) {
            if (along > hangar.deep - 2 && along < hangar.mouth) inHangar.add(ship.id);
            if (along > hangar.mouth + 5) inLane.add(ship.id);
          }
          // On the plane, ship centres stay out of the structure.
          if (world.occluders.some((part) => contains({ x, y: 0, z }, part))) {
            inStructure.add(ship.id);
          }
        }
      });
      expect([...inHangar].sort()).toEqual(["A1", "H1", "H5"]);
      expect([...inLane]).toEqual(expect.arrayContaining(["A1", "H1", "H5"]));
      expect([...inStructure]).toEqual([]);
    },
  );
});
