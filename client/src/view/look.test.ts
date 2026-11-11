import { describe, expect, it } from "vitest";
import {
  PITCH_MAX,
  PITCH_MIN,
  ZOOM_MAX,
  ZOOM_MIN,
  dragLook,
  glideEase,
  isHome,
  settleLook,
  turnLook,
  zoomWidth,
} from "./look";
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

  it("zooms in when the wheel scrolls up and out when it scrolls down", () => {
    expect(zoomWidth(40, -100)).toBeLessThan(40);
    expect(zoomWidth(40, 100)).toBeGreaterThan(40);
    expect(zoomWidth(40, 0)).toBe(40);
  });

  it("zooms proportionally, so a notch in and a notch out cancel", () => {
    expect(zoomWidth(zoomWidth(40, -120), 120)).toBeCloseTo(40, 9);
    expect(zoomWidth(80, -120) / 80).toBeCloseTo(zoomWidth(20, -120) / 20, 9);
  });

  it("treats line and page wheel units as larger steps", () => {
    expect(zoomWidth(40, 3, 1)).toBeCloseTo(zoomWidth(40, 48), 9);
    expect(zoomWidth(40, 1, 2)).toBeGreaterThan(zoomWidth(40, 3, 1));
  });

  it("stops at the zoom limits", () => {
    expect(zoomWidth(40, -100_000)).toBe(ZOOM_MIN);
    expect(zoomWidth(40, 100_000)).toBe(ZOOM_MAX);
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

describe("glideEase", () => {
  it("starts and settles gently and stays in range", () => {
    expect(glideEase(-1)).toBe(0);
    expect(glideEase(0)).toBe(0);
    expect(glideEase(0.5)).toBeCloseTo(0.5, 9);
    expect(glideEase(1)).toBe(1);
    expect(glideEase(2)).toBe(1);
    expect(glideEase(0.05)).toBeLessThan(0.05);
    expect(glideEase(0.95)).toBeGreaterThan(0.95);
  });
});
