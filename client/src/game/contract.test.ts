import { describe, expect, it } from "vitest";
import type { OwnStatus, RunSummary } from "../protocol";
import { bar, clock, runLine, scanLine, summaryLines } from "./contract";
import type { Scanned } from "./contract";

const working: OwnStatus = {
  phase: "objective",
  cargo: 7,
  capacity: 20,
  heat: 191,
  extractor: "on",
  extractorSeconds: 0,
  scan: null,
  alert: 2,
  elapsedSeconds: 187,
};

describe("clock", () => {
  it("shows minutes and padded seconds", () => {
    expect(clock(0)).toBe("0:00");
    expect(clock(187)).toBe("3:07");
    expect(clock(2700)).toBe("45:00");
  });
});

describe("bar", () => {
  it("fills ten cells, so heat reads without colour", () => {
    expect(bar(0)).toBe("[..........]");
    expect(bar(0.75)).toBe("[########..]");
    expect(bar(2)).toBe("[##########]");
  });
});

describe("runLine", () => {
  it("names the phase, clock, alert, hold, heat and extractor", () => {
    expect(runLine(working)).toBe(
      "In the site · 3:07 · alert 2 · hold 7/20 · heat [#######...] 75% · extractor working",
    );
    expect(runLine({ ...working, extractor: "cutOut", extractorSeconds: 14 })).toContain(
      "extractor CUT OUT 14 s",
    );
  });
});

describe("scanLine", () => {
  const name = (index: number) =>
    ["Ore field", "Derelict", "Volatile veins", "Nest core"][index] ?? "?";

  it("prompts for a scan, shows progress, then what it revealed", () => {
    expect(scanLine(working, null, name)).toContain("not scanned");
    expect(scanLine({ ...working, scan: 127 }, null, name)).toBe(
      "Scanning [#####.....] · hold still",
    );
    const reveal: Scanned = {
      type: "scanned",
      richPocket: 0,
      relays: [1, 3],
      waves: [{ level: 3, atSeconds: 610, drones: 2, raiders: 0 }],
    };
    expect(scanLine(working, reveal, name)).toBe(
      "Scanned · rich: Ore field · relays: Derelict and Nest core · next wave 10:10: 2 drones, 0 raiders",
    );
  });
});

describe("summaryLines", () => {
  const docked: RunSummary = {
    ending: "docked",
    ore: 20,
    salvage: 0,
    volatiles: 0,
    payout: 100,
    launchCost: 40,
    cargoLost: 0,
    modulesLost: 0,
    approachSeconds: 170,
    objectiveSeconds: 900,
    returnSeconds: 175,
  };

  it("reports what a docked run banked and earned", () => {
    expect(summaryLines(docked)).toEqual([
      "Docked at home",
      "Banked: 20 ore",
      "Contract: 100 cr · supplies −40 cr · net 60 cr",
      "Approach 2:50 · in the site 15:00 · return 2:55",
    ]);
  });

  it("reports what a destroyed run lost", () => {
    const lines = summaryLines({ ...docked, ending: "destroyed", ore: 0, payout: 0, cargoLost: 9 });
    expect(lines[1]).toBe("Lost: 9 cargo, 0 upgraded modules");
    expect(lines[2]).toContain("net −40 cr");
  });
});
