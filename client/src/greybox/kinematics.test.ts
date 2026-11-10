import { describe, expect, it } from "vitest";
import { PLAYER_HANDLING, angleDifference, seek, stepShip } from "./kinematics";
import type { ShipState } from "./kinematics";

const rest: ShipState = { x: 0, z: 0, heading: 0, speed: 0 };

describe("greybox steering", () => {
  it("accelerates along the heading and never exceeds top speed", () => {
    let state = rest;
    for (let i = 0; i < 600; i += 1) {
      state = stepShip(state, { thrust: 1, turn: 0 }, PLAYER_HANDLING, 1 / 60);
    }
    expect(state.x).toBeGreaterThan(0);
    expect(state.z).toBeCloseTo(0);
    expect(state.speed).toBeLessThanOrEqual(PLAYER_HANDLING.maxSpeed);
  });

  it("turns clockwise seen from above for positive turn", () => {
    const state = stepShip({ ...rest, speed: 5 }, { thrust: 0, turn: 1 }, PLAYER_HANDLING, 0.1);
    expect(state.heading).toBeGreaterThan(0);
    expect(state.z).toBeGreaterThan(0);
  });

  it("brakes to a stop without reversing", () => {
    let state: ShipState = { ...rest, speed: 5 };
    for (let i = 0; i < 300; i += 1) {
      state = stepShip(state, { thrust: -1, turn: 0 }, PLAYER_HANDLING, 1 / 60);
    }
    expect(state.speed).toBe(0);
  });

  it("clamps out-of-range input", () => {
    const wild = stepShip(rest, { thrust: 50, turn: -50 }, PLAYER_HANDLING, 0.1);
    const full = stepShip(rest, { thrust: 1, turn: -1 }, PLAYER_HANDLING, 0.1);
    expect(wild).toEqual(full);
  });

  it("wraps angle differences into (-PI, PI]", () => {
    expect(angleDifference(0.1, 2 * Math.PI - 0.1)).toBeCloseTo(0.2);
    expect(angleDifference(-3, 3)).toBeCloseTo(2 * Math.PI - 6);
    expect(angleDifference(Math.PI, 0)).toBeCloseTo(Math.PI);
  });

  it("seeks toward a point on either side", () => {
    expect(seek(rest, 0, 5).turn).toBeGreaterThan(0);
    expect(seek(rest, 0, -5).turn).toBeLessThan(0);
    expect(seek(rest, 10, 0)).toEqual({ turn: 0, thrust: 1 });
    expect(seek(rest, -10, 0.001).thrust).toBeLessThan(1);
  });
});
