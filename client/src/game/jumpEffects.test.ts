import { describe, expect, it } from "vitest";
import { FLASH_SECONDS, JumpEffects } from "./jumpEffects";

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
      { kind: "leave", x: 0, z: 0, heading, distance: 500, age: 0 },
      { kind: "arrive", x: 300, z: 400, heading, distance: 500, age: 0 },
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
