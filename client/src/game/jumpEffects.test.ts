import { describe, expect, it } from "vitest";
import {
  FLASH_SECONDS,
  JumpEffects,
  PUNCH_MOST,
  SETTLE_SECONDS,
  STRETCH_MOST,
  flashShape,
  scatterAt,
  sparkAt,
} from "./jumpEffects";
import type { Flash } from "./jumpEffects";

const ship = (id: number, x: number, z = 0, kind: "playerShip" | "drone" = "playerShip") => ({
  id,
  kind,
  x,
  z,
});

describe("JumpEffects", () => {
  it("sees nothing in a ship that flies", () => {
    const effects = new JumpEffects();
    expect(effects.observe([ship(1, 0)], 0.016)).toEqual([]);
    // The cruise drive at its fastest, frame after frame.
    expect(effects.observe([ship(1, 0.45)], 0.016)).toEqual([]);
    expect(effects.observe([ship(1, 0.9)], 0.016)).toEqual([]);
    expect(effects.flashes).toEqual([]);
  });

  it("flashes where a pilot's ship left and where it arrived", () => {
    const effects = new JumpEffects();
    effects.observe([ship(1, 0, 0), ship(2, 5, 5)], 0.016);
    expect(effects.observe([ship(1, 300, 400), ship(2, 5, 5)], 0.016)).toEqual([1]);
    const heading = Math.atan2(400, 300);
    expect(effects.flashes).toEqual([
      { kind: "leave", ship: 1, x: 0, z: 0, heading, distance: 500, age: 0 },
      { kind: "arrive", ship: 1, x: 300, z: 400, heading, distance: 500, age: 0 },
    ]);
  });

  it("flashes once for a jump, not again on the frames after", () => {
    const effects = new JumpEffects();
    effects.observe([ship(1, 0)], 0.016);
    effects.observe([ship(1, 500)], 0.016);
    expect(effects.observe([ship(1, 500.2)], 0.016)).toEqual([]);
    expect(effects.flashes).toHaveLength(2);
  });

  it("does not take a ship first seen, an NPC, or a long gap for a jump", () => {
    const effects = new JumpEffects();
    expect(effects.observe([ship(1, 900)], 0.016)).toEqual([]);
    effects.observe([ship(1, 900), ship(7, 0, 0, "drone")], 0.016);
    expect(effects.observe([ship(1, 900), ship(7, 300, 0, "drone")], 0.016)).toEqual([]);
    // The tab was hidden for ten seconds, and the ship cruised on.
    expect(effects.observe([ship(1, 1_150)], 10)).toEqual([]);
    expect(effects.flashes).toEqual([]);
  });

  it("ages its flashes and drops them when they have faded", () => {
    const effects = new JumpEffects();
    effects.observe([ship(1, 0)], 0.016);
    effects.observe([ship(1, 500)], 0.016);
    effects.advance(FLASH_SECONDS / 2);
    expect(effects.flashes.map((flash) => flash.age)).toEqual([0.5, 0.5]);
    effects.advance(FLASH_SECONDS / 2);
    expect(effects.flashes).toEqual([]);
  });

  it("shows the spool, with a ring that closes faster as the jump nears", () => {
    const effects = new JumpEffects();
    expect(effects.charge).toBeNull();
    effects.setCharge(4, 0);
    effects.advance(0.5);
    const early = effects.charge;
    expect(early).toMatchObject({ ship: 4, share: 0 });
    expect(early?.pulse).toBeCloseTo(0.35);
    effects.setCharge(4, 1);
    effects.advance(0.1);
    // A tenth of a second at the end of the spool turns the ring as far as 0.43 s at the start.
    expect(effects.charge?.pulse).toBeCloseTo(0.65);
    effects.setCharge(null);
    expect(effects.charge).toBeNull();
  });

  it("forgets everything on a new run", () => {
    const effects = new JumpEffects();
    effects.observe([ship(1, 0)], 0.016);
    effects.setCharge(1, 0.5);
    effects.reset();
    // The ship's first frame of the new run is far from its last of the old: not a jump.
    expect(effects.observe([ship(1, 800)], 0.016)).toEqual([]);
    expect(effects.charge).toBeNull();
  });
});

it("keeps independent crew charges and expires a missing end event by wall time", () => {
  const jumps = new JumpEffects();
  jumps.setCharge(1, 0.4);
  jumps.remoteCharge(2, 6);
  jumps.remoteCharge(3, 6);
  jumps.advance(0.1, 3);
  expect(jumps.charges.map((c) => c.ship)).toEqual([1, 2, 3]);
  expect(jumps.charges[1]?.share).toBeCloseTo(0.5);
  jumps.remoteCharge(2, 0);
  expect(jumps.charges.map((c) => c.ship)).toEqual([1, 3]);
  jumps.advance(0.1, 4);
  expect(jumps.charges.map((c) => c.ship)).toEqual([1]);
  jumps.remoteCharge(2, 6);
  jumps.reset();
  expect(jumps.charges).toEqual([]);
});

describe("the hull's stretch", () => {
  it("draws out only in the spool's last moment, and settles after the arrival", () => {
    const jumps = new JumpEffects();
    jumps.observe([ship(1, 0)], 0.016);
    expect(jumps.stretch(1)).toBe(1);
    jumps.setCharge(1, 0.5);
    expect(jumps.stretch(1)).toBe(1);
    jumps.setCharge(1, 1);
    expect(jumps.stretch(1)).toBeCloseTo(1 + STRETCH_MOST);
    jumps.setCharge(null);
    // It arrives drawn out, and is itself again a moment later.
    jumps.observe([ship(1, 500)], 0.016);
    expect(jumps.stretch(1)).toBeCloseTo(1 + STRETCH_MOST);
    jumps.advance(SETTLE_SECONDS / 2);
    expect(jumps.stretch(1)).toBeGreaterThan(1);
    expect(jumps.stretch(1)).toBeLessThan(1 + STRETCH_MOST / 2);
    jumps.advance(SETTLE_SECONDS);
    expect(jumps.stretch(1)).toBe(1);
  });

  it("is a crewmate's too, from their spool's timer", () => {
    const jumps = new JumpEffects();
    jumps.remoteCharge(2, 6);
    jumps.advance(0.016, 5.95);
    expect(jumps.stretch(2)).toBeGreaterThan(1);
    expect(jumps.stretch(3)).toBe(1);
  });
});

describe("the view's lean", () => {
  it("leans in for the pilot's own jump only, and lets go after arriving", () => {
    const jumps = new JumpEffects();
    jumps.observe([ship(1, 0)], 0.016);
    jumps.remoteCharge(2, 0.1);
    expect(jumps.punch(2)).toBe(1);
    jumps.setCharge(1, 0.5);
    expect(jumps.punch(1)).toBe(1);
    jumps.setCharge(1, 1);
    expect(jumps.punch(1)).toBeCloseTo(1 + PUNCH_MOST);
    jumps.setCharge(null);
    jumps.observe([ship(1, 500)], 0.016);
    expect(jumps.punch(1)).toBeCloseTo(1 + PUNCH_MOST);
    jumps.advance(0.5);
    expect(jumps.punch(1)).toBe(1);
  });
});

describe("sparks", () => {
  it("spiral in from far out to the hull, fading in and out", () => {
    const radius = (pulse: number) => Math.hypot(sparkAt(0, 32, pulse).x, sparkAt(0, 32, pulse).z);
    expect(radius(0.05)).toBeGreaterThan(4);
    expect(radius(0.5)).toBeLessThan(radius(0.05));
    expect(radius(0.98)).toBeLessThan(1.3);
    expect(sparkAt(0, 32, 0.01).glow).toBeLessThan(0.1);
    expect(sparkAt(0, 32, 0.5).glow).toBe(1);
    expect(sparkAt(0, 32, 0.995).glow).toBeLessThan(0.1);
  });

  it("are spread round the hull and through the loop, not in a single file", () => {
    const angles = new Set(
      Array.from({ length: 32 }, (_, i) => {
        const spark = sparkAt(i, 32, 0);
        return Math.round((Math.atan2(spark.z, spark.x) + Math.PI) * 2);
      }),
    );
    expect(angles.size).toBeGreaterThan(8);
  });

  it("thrown from an arrival fly out behind the ship, slow and fade", () => {
    const heading = Math.PI / 2;
    for (let i = 0; i < 18; i += 1) {
      const early = scatterAt(i, 18, 0.1, heading);
      const late = scatterAt(i, 18, 0.6, heading);
      // The ship went toward +z: the sparks go back toward -z.
      expect(late.z).toBeLessThan(0);
      expect(Math.hypot(late.x, late.z)).toBeGreaterThan(Math.hypot(early.x, early.z));
      expect(late.glow).toBeLessThan(early.glow);
    }
    expect(scatterAt(0, 18, 1, heading).glow).toBe(0);
  });
});

describe("a jump's two ends", () => {
  const flash = (kind: Flash["kind"], age: number, distance = 300): Flash => ({
    kind,
    ship: 1,
    x: 0,
    z: 0,
    heading: 0,
    distance,
    age,
  });
  const at = (kind: Flash["kind"], seconds: number, distance?: number) =>
    flashShape(flash(kind, seconds / FLASH_SECONDS, distance));

  it("squeezes the departing hull into a line of light that pops to a point", () => {
    const start = at("leave", 0);
    const squeezed = at("leave", 0.1);
    expect(squeezed.line.length).toBeGreaterThan(start.line.length);
    expect(squeezed.line.width).toBeLessThan(start.line.width);
    expect(squeezed.point.opacity).toBeGreaterThan(start.point.opacity);
    const popped = at("leave", 0.2);
    expect(popped.line.length).toBe(0);
    expect(popped.point.opacity).toBeGreaterThan(0);
    expect(at("leave", 0.6).point.opacity).toBe(0);
  });

  it("opens a shockwave and shoots a trail ahead, no longer than the jump", () => {
    expect(at("leave", 0.3).ring.radius).toBeGreaterThan(at("leave", 0.05).ring.radius);
    expect(at("leave", 0.7).ring.opacity).toBe(0);
    expect(at("leave", 0.5).trail.length).toBeCloseTo(40);
    expect(at("leave", 0.5, 25).trail.length).toBeCloseTo(25);
    expect(at("leave", 0.8).trail.opacity).toBeLessThan(at("leave", 0.3).trail.opacity);
  });

  it("closes a ring on the arriving hull and draws its trail in behind it", () => {
    expect(at("arrive", 0.25).ring.radius).toBeLessThan(at("arrive", 0.02).ring.radius);
    expect(at("arrive", 0.5).ring.opacity).toBe(0);
    expect(at("arrive", 0).trail.length).toBeCloseTo(14);
    expect(at("arrive", 0.4).trail.length).toBe(0);
    expect(at("arrive", 0).point.opacity).toBe(1);
    expect(at("arrive", 0.25).point.opacity).toBe(0);
  });
});
