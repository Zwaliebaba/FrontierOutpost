import { describe, expect, it } from "vitest";
import type { OwnStatus } from "../protocol";
import { NOTE_MOST, PULSE_MS, Pulse, gauges, heatGauge, holdGauge, hullGauge } from "./gauges";

const own: OwnStatus = {
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

describe("gauges", () => {
  it("shows hull, hold and heat in that order, each labelled", () => {
    const [hull, hold, heat] = gauges(own, 255, 0, 0);
    expect([hull?.label, hold?.label, heat?.label]).toEqual(["Hull", "Hold", "Heat"]);
    expect(hull?.value).toBe("100%");
    expect(hold?.value).toBe("7/20");
    expect(heat?.value).toBe("75%");
  });

  it("keeps every note to one line of the panel, whatever the ship is doing", () => {
    const notes: string[] = [];
    for (const extractor of ["off", "on", "venting", "cutOut"] as const) {
      for (const heat of [0, 40, 200, 255]) {
        for (const cargo of [0, 1, 99, 100, 250]) {
          for (const contractCargo of [0, 100]) {
            for (const hull of [255, 20]) {
              // The widest numbers the wire can carry: three digits everywhere.
              const widest = { ...own, extractor, heat, cargo, capacity: 250, shield: 255 };
              const status = { ...widest, extractorSeconds: 120 };
              notes.push(...gauges(status, hull, -200, 20, contractCargo).map((g) => g.note));
            }
          }
        }
      }
    }
    expect(notes.filter((note) => note.length > NOTE_MOST)).toEqual([]);
  });
});

describe("hullGauge", () => {
  it("grows more urgent as the hull falls and calls out a hit", () => {
    expect(hullGauge(255, 0, 0).level).toBe("ok");
    expect(hullGauge(128, 0, 0).level).toBe("warn");
    const low = hullGauge(51, 0, -26);
    expect(low.level).toBe("danger");
    expect(low.change).toBe("−10%");
    expect(low.note).toBe("critical: dock to repair");
    expect(hullGauge(255, 0, 0).change).toBe("");
  });

  it("names a shield when there is one", () => {
    expect(hullGauge(255, 33, 0).note).toBe("shield 33");
    expect(hullGauge(51, 33, 0).note).toBe("shield 33 · critical: dock to repair");
  });
});

describe("holdGauge", () => {
  it("calls out a unit gained and says cargo counts only once docked", () => {
    const gauge = holdGauge(own, 1);
    expect(gauge.share).toBeCloseTo(0.35);
    expect(gauge.change).toBe("+1");
    expect(gauge.note).toBe("counts only once docked at home");
    expect(holdGauge(own, 0).change).toBe("");
  });

  it("says how far the hold is from what the contract needs, until it has it", () => {
    expect(holdGauge({ ...own, cargo: 0 }, 0, 10).note).toBe("contract pays from 10: 10 more");
    expect(holdGauge({ ...own, cargo: 6 }, 1, 10).note).toBe("contract pays from 10: 4 more");
    expect(holdGauge({ ...own, cargo: 10 }, 0, 10).note).toBe(
      "contract met · counts only once docked",
    );
  });

  it("says to go home when full", () => {
    const full = holdGauge({ ...own, cargo: 20 }, 0);
    expect(full.note).toContain("fly home and dock");
    expect(full.level).toBe("warn");
  });
});

describe("heatGauge", () => {
  it("explains that heat is the extractor's, not the hold's", () => {
    expect(heatGauge(own).note).toContain("extracting");
    expect(heatGauge({ ...own, heat: 0, extractor: "off" }).note).toBe("extractor off");
    expect(heatGauge({ ...own, heat: 40, extractor: "off" }).note).toContain("cooling");
  });

  it("counts down a vent and a cut-out", () => {
    expect(heatGauge({ ...own, extractor: "venting", extractorSeconds: 3 }).note).toBe(
      "venting 3 s: extraction paused",
    );
    const cut = heatGauge({ ...own, heat: 255, extractor: "cutOut", extractorSeconds: 14 });
    expect(cut.note).toContain("CUT OUT 14 s");
    expect(cut.level).toBe("danger");
  });
});

describe("Pulse", () => {
  it("calls out a change for a while, adding up changes close together", () => {
    const pulse = new Pulse();
    pulse.observe(3, 0);
    expect(pulse.recent(0)).toBe(0);
    pulse.observe(4, 100);
    pulse.observe(5, 200);
    expect(pulse.recent(300)).toBe(2);
    expect(pulse.recent(200 + PULSE_MS + 1)).toBe(0);
  });

  it("starts afresh on a change the other way or after the call-out ends", () => {
    const pulse = new Pulse();
    pulse.observe(200, 0);
    pulse.observe(180, 10);
    pulse.observe(190, 20);
    expect(pulse.recent(20)).toBe(10);
    pulse.observe(170, 20 + PULSE_MS + 5);
    expect(pulse.recent(20 + PULSE_MS + 5)).toBe(-20);
  });
});
