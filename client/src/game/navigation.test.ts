import { describe, expect, it } from "vitest";
import { clearOf, distanceText, placeMarkers } from "./navigation";
import type { Box } from "./navigation";

// A plane seen straight down, one pixel per metre, centred on the origin.
const project = (x: number, z: number) => ({ x: 400 + x, y: 300 + z });
const viewport = { width: 800, height: 600 };
const even = { top: 20, right: 20, bottom: 20, left: 20 };

describe("placeMarkers", () => {
  it("labels a target where it is when it is on screen", () => {
    const [marker] = placeMarkers(
      [{ id: "dock", label: "Dock", x: 100, z: -50 }],
      { x: 0, z: 0 },
      project,
      viewport,
      even,
    );
    expect(marker?.onScreen).toBe(true);
    expect(marker?.screen).toEqual({ x: 500, y: 250 });
    expect(marker?.anchor, "the label sits above its target").toEqual({ x: 0.5, y: 1 });
    expect(marker?.distance).toBeCloseTo(Math.hypot(100, 50));
  });

  it("pins an off-screen target to the edge in its direction", () => {
    const [marker] = placeMarkers(
      [{ id: "ore", label: "Ore field", x: 1_000, z: 190 }],
      { x: 0, z: 0 },
      project,
      viewport,
      even,
    );
    expect(marker?.onScreen).toBe(false);
    expect(marker?.screen.x).toBeCloseTo(780);
    // Along the ray from the centre: 190 down for 1,000 across, scaled to reach x = 780.
    expect(marker?.screen.y).toBeCloseTo(300 + (190 * 380) / 1_000);
    expect(marker?.angle).toBeCloseTo(Math.atan2(190, 1_000));
  });

  it("keeps a target far above or below inside the top and bottom margins", () => {
    const [marker] = placeMarkers(
      [{ id: "nest", label: "Nest core", x: 0, z: -5_000 }],
      { x: 0, z: 0 },
      project,
      viewport,
      even,
    );
    expect(marker?.screen).toEqual({ x: 400, y: 20 });
    expect(marker?.anchor, "hangs below the top edge").toEqual({ x: 0.5, y: 0 });
  });

  it("keeps clear of the HUD and anchors edge labels inward so they stay whole", () => {
    const [marker] = placeMarkers(
      [{ id: "veins", label: "Volatile veins", x: 2_000, z: 0 }],
      { x: 0, z: 0 },
      project,
      viewport,
      { top: 100, right: 10, bottom: 100, left: 10 },
    );
    expect(marker?.screen).toEqual({ x: 790, y: 300 });
    expect(marker?.anchor.x).toBe(1);
    const [low] = placeMarkers(
      [{ id: "nest", label: "Nest core", x: 0, z: 5_000 }],
      { x: 0, z: 0 },
      project,
      viewport,
      { top: 100, right: 10, bottom: 100, left: 10 },
    );
    expect(low?.screen.y).toBe(500);
  });
});

describe("distanceText", () => {
  it("shows metres, then kilometres", () => {
    expect(distanceText(87.4)).toBe("87 m");
    expect(distanceText(1_460)).toBe("1.5 km");
  });
});

describe("clearOf", () => {
  // The screen between the top bar and the footer.
  const bounds: Box = { x: 0, y: 44, width: 1280, height: 646 };
  const gauges: Box = { x: 18, y: 500, width: 520, height: 180 };
  const minimap: Box = { x: 1030, y: 480, width: 232, height: 198 };
  const overlaps = (a: Box, b: Box) =>
    a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;

  it("leaves a waypoint that is on no panel where it is", () => {
    const box = { x: 600, y: 660, width: 140, height: 18 };
    expect(clearOf(box, [gauges, minimap], bounds)).toEqual(box);
  });

  it("lifts a waypoint pinned to the bottom edge over the gauges", () => {
    const moved = clearOf({ x: 200, y: 660, width: 140, height: 18 }, [gauges, minimap], bounds);
    expect(overlaps(moved, gauges)).toBe(false);
    expect(moved.x, "it keeps its place along the edge").toBe(200);
    expect(moved.y + moved.height).toBeLessThanOrEqual(gauges.y);
  });

  it("goes sideways when above and below are off the screen", () => {
    const tall: Box = { x: 0, y: 44, width: 300, height: 646 };
    const moved = clearOf({ x: 250, y: 300, width: 140, height: 18 }, [tall], bounds);
    expect(overlaps(moved, tall)).toBe(false);
    expect(moved.y).toBe(300);
  });

  it("clears a second panel the first move landed on", () => {
    const lock: Box = { x: 18, y: 470, width: 400, height: 24 };
    const moved = clearOf({ x: 200, y: 660, width: 140, height: 18 }, [gauges, lock], bounds);
    expect(overlaps(moved, gauges) || overlaps(moved, lock)).toBe(false);
    expect(moved.y + moved.height).toBeLessThanOrEqual(lock.y);
  });

  it("stays put when nowhere is clear", () => {
    const box = { x: 200, y: 300, width: 140, height: 18 };
    expect(clearOf(box, [bounds], bounds)).toEqual(box);
  });
});
