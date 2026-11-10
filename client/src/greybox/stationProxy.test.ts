import { describe, expect, it } from "vitest";
import { stationOccluders } from "./stationProxy";

describe("station proxy", () => {
  const parts = stationOccluders();
  const extent = (axis: "x" | "z") =>
    parts.map((part) => {
      const half = part.kind === "box" ? (axis === "x" ? part.halfX : part.halfZ) : part.radius;
      return [part[axis] - half, part[axis] + half] as const;
    });

  it("spans the generator's station width of about 14.6 m, pod to pod", () => {
    const xs = extent("x");
    const width = Math.max(...xs.map(([, max]) => max)) - Math.min(...xs.map(([min]) => min));
    expect(width).toBeCloseTo(14.64, 1);
  });

  it("puts the landing deck on the gameplay plane and toward the camera", () => {
    const apron = parts.find((part) => part.kind === "box" && part.halfX > 2);
    expect(apron?.kind === "box" ? apron.yMax : Number.NaN).toBeCloseTo(0);
    expect(apron?.z).toBeGreaterThan(0);
  });

  it("rises to the hub roof and the tallest mast", () => {
    const tops = parts.map((part) => (part.kind === "sphere" ? part.radius : part.yMax));
    expect(tops[0]).toBeCloseTo(3.85);
    expect(Math.max(...tops)).toBeCloseTo(9.37);
  });
});
