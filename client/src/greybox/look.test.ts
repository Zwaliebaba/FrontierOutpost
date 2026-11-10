import { describe, expect, it } from "vitest";
import { PITCH_MAX, PITCH_MIN, dragLook, isHome, settleLook, turnLook } from "./look";
import type { LookState } from "./look";

const home: LookState = { yaw: 0, pitch: 33 };

describe("greybox look", () => {
  it("orbits with the drag and raises the camera when dragged down", () => {
    const dragged = dragLook(home, 100, 40);
    expect(dragged.yaw).toBeGreaterThan(0);
    expect(dragged.pitch).toBeGreaterThan(33);
    expect(dragLook(home, -100, -40).yaw).toBeLessThan(0);
  });

  it("keeps the camera between the pitch limits", () => {
    expect(dragLook(home, 0, 10_000).pitch).toBe(PITCH_MAX);
    expect(dragLook(home, 0, -10_000).pitch).toBe(PITCH_MIN);
  });

  it("springs back to the tactical view and lands exactly on it", () => {
    let state = turnLook(dragLook(home, 250, 80), 0.4);
    expect(isHome(state, home)).toBe(false);
    for (let i = 0; i < 240 && !isHome(state, home); i += 1)
      state = settleLook(state, home, 1 / 60);
    expect(state).toEqual(home);
  });

  it("returns the short way round after more than half a turn", () => {
    const state = settleLook({ yaw: 1.5 * Math.PI, pitch: 33 }, home, 1 / 60);
    expect(state.yaw).toBeGreaterThan(1.5 * Math.PI);
  });

  it("moves closer every step without overshooting", () => {
    const start: LookState = { yaw: 1, pitch: 60 };
    const next = settleLook(start, home, 1 / 60);
    expect(next.yaw).toBeLessThan(start.yaw);
    expect(next.yaw).toBeGreaterThan(0);
    expect(next.pitch).toBeLessThan(start.pitch);
    expect(next.pitch).toBeGreaterThan(33);
  });
});
