import { describe, expect, it } from "vitest";
import { CATALOG } from "../protocol/generated/catalogFixture";
import type { AccountInfo } from "../protocol";
import {
  costText,
  fittingRows,
  launchLines,
  shortfall,
  socketView,
  toggleFitting,
} from "./options";

/** A home as the server would describe it. */
function home(overrides: Partial<AccountInfo> = {}): AccountInfo {
  return {
    credits: 260,
    ore: 10,
    salvage: 0,
    volatiles: 0,
    alloy: 0,
    components: 0,
    outpost: {
      sockets: [
        { id: "a", open: true, run: 0 },
        { id: "b", open: false, run: 0 },
      ],
      unlocks: [],
      powerUsed: 0,
      opens: [],
      processorBatches: 4,
      fittingDiscount: 0,
      openTiers: 1,
    },
    processing: [],
    hangar: [],
    modules: [],
    launch: { flown: [], missing: [], stake: 0, supplies: 40, capacity: 20 },
    ...overrides,
  };
}

/** A home with a refinery in socket A at `tier`, run at `run`. */
function withRefinery(tier: number, run: number, credits = 2_000): AccountInfo {
  const base = home({ credits, alloy: 20, components: 10 });
  return {
    ...base,
    outpost: {
      ...base.outpost,
      sockets: [
        { id: "a", open: true, facility: "refinery", run },
        { id: "b", open: true, run: 0 },
      ],
      unlocks: [{ facility: "refinery", tier }],
      powerUsed: run === 0 ? 0 : run + 1,
    },
  };
}

describe("costs", () => {
  it("reads like a price tag and says what is missing", () => {
    const tier1 = CATALOG.facilities[0]?.tiers[0]?.cost;
    expect(tier1 && costText(tier1)).toBe("150 cr + 10 ore");
    expect(tier1 && shortfall(home(), tier1)).toBeNull();
    expect(tier1 && shortfall(home({ credits: 100, ore: 4 }), tier1)).toBe(
      "Needs 50 cr and 6 ore more",
    );
  });
});

describe("a socket's choices", () => {
  it("offers every facility in an empty socket, naming what each opens", () => {
    const view = socketView(CATALOG, home(), "a");
    expect(view.state).toBe("empty");
    expect(view.choices.map((c) => c.label)).toEqual([
      "Build a Sensor array",
      "Build a Refinery",
      "Build a Fabrication bay",
    ]);
    const refinery = view.choices[1];
    expect(refinery?.blocked).toBeUndefined();
    expect(refinery?.details).toContain("Cost 150 cr + 10 ore");
    expect(refinery?.details).toContain(
      "Opens: Volatile veins pocket, Extraction rig, Cargo frame module",
    );
    expect(refinery?.details).toContain("Processor: 4 → 10 batches per run");
    expect(refinery?.details).toContain("Here it processes 2 more batches per run");
    expect(refinery?.details).toContain("Comes with a extraction rig");
    expect(refinery?.message).toEqual({ type: "install", socket: "a", facility: "refinery" });
  });

  it("explains a closed socket and blocks what the pilot cannot afford", () => {
    expect(socketView(CATALOG, home(), "b").state).toBe("closed");
    const poor = socketView(CATALOG, home({ credits: 40 }), "a");
    expect(poor.choices[0]?.blocked).toBe("Needs 110 cr more");
  });

  it("previews an upgrade, running at a lower tier and removing", () => {
    const view = socketView(CATALOG, withRefinery(1, 1), "a");
    expect(view.state).toBe("powered");
    expect(view.summary[1]).toBe(
      "Opens: Volatile veins pocket, Extraction rig, Cargo frame module",
    );
    const [upgrade, unpower, remove] = view.choices;
    expect(upgrade?.label).toBe("Upgrade to tier 2");
    expect(upgrade?.details).toContain("Cost 550 cr + 12 alloy + 5 components");
    expect(upgrade?.details).toContain("Opens: Heat sink module");
    expect(upgrade?.details).toContain("Processor: 10 → 16 batches per run");
    expect(unpower?.label).toBe("Unpower");
    expect(unpower?.details).toContain(
      "Closes: Volatile veins pocket, Extraction rig, Cargo frame module",
    );
    expect(remove?.details[0]).toBe("Free; installing it again costs 40 cr + 6 alloy");
  });

  it("offers a removed facility again at the reinstallation cost", () => {
    const base = withRefinery(2, 0);
    const removed: AccountInfo = {
      ...base,
      outpost: {
        ...base.outpost,
        sockets: [
          { id: "a", open: true, run: 0 },
          { id: "b", open: true, run: 0 },
        ],
      },
    };
    const again = socketView(CATALOG, removed, "b").choices.find((c) =>
      c.label.includes("Refinery"),
    );
    expect(again?.label).toBe("Install the Refinery again");
    expect(again?.details).toContain("Cost 40 cr + 6 alloy (installing again)");
    expect(again?.details).toContain("Runs at tier 2, drawing 3");
  });

  it("blocks a tier the free power cannot carry", () => {
    const base = withRefinery(2, 2);
    const both: AccountInfo = {
      ...base,
      outpost: {
        ...base.outpost,
        sockets: [
          { id: "a", open: true, facility: "refinery", run: 2 },
          { id: "b", open: true, facility: "sensor_array", run: 1 },
        ],
        unlocks: [
          { facility: "refinery", tier: 2 },
          { facility: "sensor_array", tier: 2 },
        ],
      },
    };
    const sensor = socketView(CATALOG, both, "b");
    const tier2 = sensor.choices.find((c) => c.label === "Run at tier 2");
    expect(tier2?.blocked).toBe("Draws 3; 2 power is free");
  });
});

describe("the hangar and the launch", () => {
  it("prices fittings with the discount and says what they need", () => {
    const base = withRefinery(1, 1);
    const account: AccountInfo = {
      ...base,
      outpost: {
        ...base.outpost,
        fittingDiscount: 20,
        opens: ["Volatile veins pocket", "Extraction rig", "Cargo frame module"],
      },
      hangar: ["extraction_rig"],
      rig: "extraction_rig",
    };
    const rows = fittingRows(CATALOG, account);
    const frame = rows.find((r) => r.fitting.id === "cargo_frame");
    expect(frame?.price).toBe(24);
    expect(frame?.blocked).toBeUndefined();
    expect(rows.find((r) => r.fitting.id === "heat_sink")?.blocked).toBe(
      "Needs the Heat sink module",
    );
    const rig = rows.find((r) => r.fitting.id === "extraction_rig");
    expect([rig?.owned, rig?.saved]).toEqual([1, true]);
  });

  it("switches one rig and up to two different modules", () => {
    const account = home({ rig: "scout_rig", modules: ["cargo_frame"] });
    expect(toggleFitting(CATALOG, account, "scout_rig")).toEqual({
      type: "saveFitting",
      rig: null,
      modules: ["cargo_frame"],
    });
    expect(toggleFitting(CATALOG, account, "heat_sink")?.modules).toEqual([
      "cargo_frame",
      "heat_sink",
    ]);
    const full = home({ modules: ["cargo_frame", "heat_sink"] });
    expect(toggleFitting(CATALOG, full, "deep_scan")).toBeNull();
  });

  it("shows the stake at replacement price and what stays home", () => {
    const account = home({
      launch: {
        flown: ["extraction_rig", "heat_sink", "cargo_frame"],
        missing: [{ fitting: "deep_scan", reason: "needs the Deep-scan array module powered" }],
        stake: 180,
        supplies: 40,
        capacity: 38,
      },
    });
    expect(launchLines(CATALOG, account)).toEqual([
      "Flies: Extraction rig, Heat sink, Cargo frame · hold 38",
      "Stake: 180 cr of rigs and modules at replacement price, and 40 cr of supplies",
      "Deep-scan array stays home: needs the Deep-scan array module powered",
    ]);
  });
});
