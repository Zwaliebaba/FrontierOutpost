import { describe, expect, it } from "vitest";
import { FrameTimer } from "./frameTimer";

describe("FrameTimer", () => {
  it("summarises once per window: frames per second, mean and worst work", () => {
    const timer = new FrameTimer(1_000);
    let summary = null;
    // 50 frames, 20 ms apart, each taking 2 ms except one of 9 ms.
    for (let frame = 0; frame < 51 && !summary; frame++) {
      const start = frame * 20;
      summary = timer.record(start, start + (frame === 10 ? 9 : 2));
    }
    expect(summary?.fps).toBeCloseTo(51_000 / 1_002, 1);
    expect(summary?.meanMs).toBeCloseTo((50 * 2 + 9) / 51, 5);
    expect(summary?.worstMs).toBe(9);
  });

  it("starts a fresh window after each summary", () => {
    const timer = new FrameTimer(100);
    expect(timer.record(0, 50)).toBeNull();
    expect(timer.record(60, 110)?.worstMs).toBe(50);
    expect(timer.record(120, 121)).toBeNull();
    expect(timer.record(200, 211)?.worstMs).toBe(11);
  });
});
