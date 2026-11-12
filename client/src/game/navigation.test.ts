import { describe, expect, it } from "vitest";
import { distanceText, placeMarkers, spreadBoxes } from "./navigation";
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

describe("spreadBoxes", () => {
  const bounds = { width: 1280, height: 720 };
  const overlaps = (a: Box, b: Box) =>
    a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height && b.y < a.y + a.height;

  it("leaves boxes that do not overlap where they are", () => {
    const boxes = [
      { x: 10, y: 10, width: 100, height: 20 },
      { x: 300, y: 10, width: 100, height: 20 },
    ];
    expect(spreadBoxes(boxes, bounds)).toEqual(boxes);
  });

  it("parts two labels stacked on the bottom edge, as the playtest showed", () => {
    // Nest core and Derelict pinned to the same stretch of edge, mostly side by side.
    const [a, b] = spreadBoxes(
      [
        { x: 690, y: 460, width: 200, height: 22 },
        { x: 790, y: 460, width: 200, height: 22 },
      ],
      bounds,
    );
    expect(a && b && overlaps(a, b)).toBe(false);
    // Stacked rather than slid along: the smaller move.
    expect(a && b && Math.abs(a.y - b.y)).toBeGreaterThanOrEqual(22);
    expect(a?.x).toBe(690);
  });

  it("parts boxes in the same place, and several at once, inside the bounds", () => {
    const boxes = spreadBoxes(
      [0, 1, 2, 3].map(() => ({ x: 600, y: 700, width: 180, height: 22 })),
      bounds,
    );
    for (let i = 0; i < boxes.length; i++) {
      for (let j = i + 1; j < boxes.length; j++) {
        const a = boxes[i];
        const b = boxes[j];
        expect(a && b && overlaps(a, b)).toBe(false);
      }
    }
    for (const box of boxes) {
      expect(box.y + box.height).toBeLessThanOrEqual(bounds.height);
      expect(box.y).toBeGreaterThanOrEqual(0);
    }
  });
});
