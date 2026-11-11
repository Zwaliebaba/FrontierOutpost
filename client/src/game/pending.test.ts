import { describe, expect, it } from "vitest";

import { MAX_AGE_TICKS, PendingEvents } from "./pending";

describe("pending events", () => {
  it("hands each event to the next frame once, oldest first", () => {
    const pending = new PendingEvents<string>();
    pending.push(100, ["a", "b"]);
    pending.push(101, ["c"]);
    expect(pending.take()).toEqual(["a", "b", "c"]);
    expect(pending.take()).toEqual([]);
  });

  it("keeps only the last second while no frame is drawn", () => {
    // A tab hidden for five minutes of a fight: two shots a tick, and no frame to take them.
    const pending = new PendingEvents<number>();
    for (let tick = 0; tick < 6_000; tick += 1) {
      pending.push(tick, [tick, tick]);
      expect(pending.size).toBeLessThanOrEqual(2 * (MAX_AGE_TICKS + 1));
    }
    const drawn = pending.take();
    expect(drawn.length).toBe(2 * (MAX_AGE_TICKS + 1));
    expect(Math.min(...drawn)).toBe(5_999 - MAX_AGE_TICKS);
  });

  it("drops what went stale even when the newest snapshot has no events", () => {
    const pending = new PendingEvents<string>();
    pending.push(100, ["old"]);
    pending.push(100 + MAX_AGE_TICKS + 1, []);
    expect(pending.take()).toEqual([]);
  });
});
