import { describe, expect, it } from "vitest";
import type { OwnStatus, RunSummary } from "../protocol";
import { clock, runCells, scanSummary, summaryLines } from "./contract";
import type { Scanned } from "./contract";
import { scanRefusalText } from "./contract";

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
  drive: "inSite",
  shield: 0,
  relay: 0,
};

describe("clock", () => {
  it("shows minutes and padded seconds", () => {
    expect(clock(0)).toBe("0:00");
    expect(clock(187)).toBe("3:07");
    expect(clock(2700)).toBe("45:00");
  });
});

const cell = (cells: ReturnType<typeof runCells>, key: string) =>
  cells.find((candidate) => candidate.key === key);

describe("runCells", () => {
  it("reads the phase, the clock, the alert and the drives, one cell each", () => {
    expect(runCells(working, null).map(({ key, value }) => [key, value])).toEqual([
      ["phase", "In the site"],
      ["time", "3:07"],
      ["alert", "2"],
      ["drive", "Cruise off in the site"],
    ]);
  });

  it("raises the alert's level as it climbs, and a jammed drive's at once", () => {
    expect(cell(runCells({ ...working, alert: 0 }, null), "alert")?.level).toBe("ok");
    expect(cell(runCells(working, null), "alert")?.level).toBe("warn");
    expect(cell(runCells({ ...working, alert: 3 }, null), "alert")?.level).toBe("danger");
    const jammed = cell(runCells({ ...working, drive: "hunted" }, null), "drive");
    expect(jammed).toMatchObject({ value: "Jammed: hostile lock", level: "danger" });
    expect(cell(runCells({ ...working, drive: "ready" }, null), "drive")?.value).toBe("Ready");
  });

  it("shows the crew's relay link while it is held and once linked", () => {
    expect(cell(runCells(working, null), "relay")).toBeUndefined();
    expect(cell(runCells({ ...working, relay: 12 }, null), "relay")?.value).toBe("Held 12/30 s");
    expect(cell(runCells({ ...working, relay: 255 }, null), "relay")?.value).toBe("Linked");
  });

  it("shows a scan in progress, then the next wave the alert has not reached", () => {
    expect(cell(runCells({ ...working, scan: 127 }, null), "scan")?.value).toBe("50% · hold still");
    const waves = [
      { level: 2, atSeconds: 400, drones: 1, raiders: 1 },
      { level: 3, atSeconds: 610, drones: 2, raiders: 0 },
    ];
    // The alert is at 2 already: its wave has come.
    expect(cell(runCells(working, { waves }), "wave")?.value).toBe("10:10 · 2 drones · 0 raiders");
    expect(cell(runCells({ ...working, alert: 3 }, { waves }), "wave")?.value).toBe(
      "None before the limit",
    );
    expect(cell(runCells(working, null), "wave")).toBeUndefined();
  });
});

describe("scanSummary", () => {
  it("names the rich pocket and the relays", () => {
    const name = (index: number) =>
      ["Ore field", "Derelict", "Volatile veins", "Nest core"][index] ?? "?";
    const reveal: Pick<Scanned, "richPocket" | "relays"> = { richPocket: 0, relays: [1, 3] };
    expect(scanSummary(reveal, name)).toBe(
      "Scan complete · rich pocket: Ore field · relays: Derelict and Nest core · marked on the map",
    );
  });
});

describe("summaryLines", () => {
  const docked: RunSummary = {
    ending: "docked",
    run: "receipt-a",
    settled: true,
    pot: { contract: 100, bounty: 0, relay: 0 },
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
      "Your crew-pot share: 100 cr · supplies −40 cr · net 60 cr",
      "Approach 2:50 · in the site 15:00 · return 2:55",
    ]);
  });

  it("reports what a destroyed run lost", () => {
    const lines = summaryLines({ ...docked, ending: "destroyed", ore: 0, payout: 0, cargoLost: 9 });
    expect(lines[1]).toBe("Lost: 9 cargo, 0 upgraded modules");
    expect(lines[2]).toContain("net −40 cr");
  });
});

describe("scanRefusalText", () => {
  it("says what to do when a scan does not start", () => {
    expect(scanRefusalText("moving")).toBe("Scan: slow to a stop first");
    expect(scanRefusalText("outOfRange")).toContain("fly nearer");
    expect(scanRefusalText("revealed")).toContain("map");
  });
});
