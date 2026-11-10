import { describe, expect, it } from "vitest";
import { distanceText, placeMarkers } from "./navigation";

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
