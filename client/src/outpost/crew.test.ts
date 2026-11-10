import { describe, expect, it } from "vitest";
import type { CrewInfo } from "../protocol";
import { launchRole, readSplit } from "./crew";

const crew = (overrides: Partial<CrewInfo> = {}): CrewInfo => ({
  code: "ABC234",
  members: [
    { callsign: "Vega", share: 50, you: true },
    { callsign: "Rigel", share: 50, you: false },
  ],
  hosting: true,
  ...overrides,
});

describe("who launches", () => {
  it("lets a solo pilot or a host launch, and a member only follow", () => {
    expect(launchRole(null)).toEqual({ launches: true, follow: null, note: null });
    expect(
      launchRole(crew({ members: [{ callsign: "Vega", share: 100, you: true }] })).launches,
    ).toBe(true);
    expect(launchRole(crew()).launches).toBe(true);
    const waiting = launchRole(crew({ hosting: false }));
    expect([waiting.launches, waiting.follow]).toEqual([false, null]);
    const following = launchRole(crew({ hosting: false, launch: { tier: 2, secondsLeft: 41 } }));
    expect(following.follow).toBe(2);
    expect(following.note).toBe("The host has launched: follow within 41 s.");
  });
});

describe("the split", () => {
  it("takes whole percentages that add up to 100", () => {
    expect(readSplit(["70", "30"])).toEqual([70, 30]);
    expect(readSplit(["70", "20"])).toBeNull();
    expect(readSplit(["50.5", "49.5"])).toBeNull();
    expect(readSplit(["-10", "110"])).toBeNull();
  });
});
