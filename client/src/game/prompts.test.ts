import { describe, expect, it } from "vitest";
import type { OwnStatus } from "../protocol";
import { MOST_PROMPTS, prompts } from "./prompts";
import type { PromptInput } from "./prompts";

const own: OwnStatus = {
  phase: "objective",
  cargo: 0,
  capacity: 20,
  heat: 0,
  extractor: "off",
  extractorSeconds: 0,
  scan: null,
  alert: 0,
  elapsedSeconds: 60,
  drive: "inSite",
  shield: 0,
  relay: 0,
};
const quiet: PromptInput = {
  own,
  deposit: null,
  scanned: true,
  locked: false,
  lockedInRange: false,
  hostileInLockRange: false,
};
const ids = (input: PromptInput) => prompts(input).map((prompt) => prompt.id);

describe("prompts", () => {
  it("says nothing when no key would do anything", () => {
    expect(prompts(quiet)).toEqual([]);
  });

  it("offers the extractor in reach of a deposit, and points to one that is near", () => {
    const near = { distance: 13.4, left: 8, inReach: false };
    expect(prompts({ ...quiet, deposit: near })).toEqual([
      { id: "deposit", key: "", text: "Deposit 13 m · 8 left · move closer" },
    ]);
    expect(prompts({ ...quiet, deposit: { ...near, inReach: true } })).toEqual([
      { id: "extract", key: "X", text: "Extract · 8 left · hold still" },
    ]);
  });

  it("does not offer the extractor while it works, or with the hold full", () => {
    const deposit = { distance: 3, left: 8, inReach: true };
    expect(ids({ ...quiet, deposit, own: { ...own, extractor: "on" } })).toEqual([]);
    expect(ids({ ...quiet, deposit, own: { ...own, cargo: 20 } })).toEqual([]);
  });

  it("prompts the vent only once the heat nears the cut-out", () => {
    const working = { ...own, extractor: "on" as const };
    expect(ids({ ...quiet, own: { ...working, heat: 150 } })).toEqual([]);
    expect(ids({ ...quiet, own: { ...working, heat: 200 } })).toEqual(["vent"]);
  });

  it("offers the lock with a hostile near, and the trigger once it is in range", () => {
    expect(ids({ ...quiet, hostileInLockRange: true })).toEqual(["lock"]);
    expect(ids({ ...quiet, hostileInLockRange: true, locked: true })).toEqual([]);
    expect(ids({ ...quiet, locked: true, lockedInRange: true })).toEqual(["fire"]);
  });

  it("offers the scan inside an unscanned site, until the scan starts", () => {
    expect(ids({ ...quiet, scanned: false })).toEqual(["scan"]);
    expect(ids({ ...quiet, scanned: false, own: { ...own, scan: 10 } })).toEqual([]);
    expect(ids({ ...quiet, scanned: false, own: { ...own, phase: "approach" } })).toEqual([]);
  });

  it("offers the cruise drive on the legs while it is ready", () => {
    const leg = { ...own, phase: "approach" as const, drive: "ready" as const };
    expect(ids({ ...quiet, own: leg })).toEqual(["cruise"]);
    expect(ids({ ...quiet, own: { ...leg, drive: "hunted" } })).toEqual([]);
  });

  it("keeps to the most urgent few", () => {
    const busy: PromptInput = {
      own: { ...own, extractor: "on", heat: 250 },
      deposit: { distance: 2, left: 5, inReach: true },
      scanned: false,
      locked: true,
      lockedInRange: true,
      hostileInLockRange: true,
    };
    expect(ids(busy)).toEqual(["vent", "fire", "scan"]);
    expect(prompts(busy).length).toBeLessThanOrEqual(MOST_PROMPTS);
  });
});
