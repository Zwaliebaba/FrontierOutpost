/**
 * The ship gauges: hull, hold and heat as three labelled bars. Playtesting found the run line's
 * one bar, heat, read as the hold: it fills over a minute of extraction and empties on a vent or
 * cut-out, while the hold itself rises a unit every 30 seconds at the basic rate. Each gauge now
 * names what it measures and what happens next. Kept free of the DOM so it can be tested.
 */
import type { OwnStatus } from "../protocol";

export type GaugeKey = "hull" | "hold" | "heat";
/** How urgent a gauge is. Shown by a marker as well as colour, never by colour alone. */
export type GaugeLevel = "ok" | "warn" | "danger";

export interface Gauge {
  key: GaugeKey;
  label: string;
  /** 0 to 1. */
  share: number;
  value: string;
  /** What the gauge means right now, or empty. */
  note: string;
  level: GaugeLevel;
}

/** How long a change in the hold or the hull is called out, milliseconds. */
export const PULSE_MS = 2_500;

/**
 * Notices a value rising or falling and remembers the change for [`PULSE_MS`], adding up changes
 * that come close together, so a gauge can say "+1" when the hold gains a unit.
 */
export class Pulse {
  #last: number | null = null;
  #change = 0;
  #at = -Infinity;

  /** Records `value` at `now`; a first value, or a reset, is not a change. */
  observe(value: number, now: number): void {
    if (this.#last !== null && value !== this.#last) {
      const fresh =
        now - this.#at <= PULSE_MS && Math.sign(value - this.#last) === Math.sign(this.#change);
      this.#change = (fresh ? this.#change : 0) + (value - this.#last);
      this.#at = now;
    }
    this.#last = value;
  }

  /** The change still being called out at `now`, or 0. */
  recent(now: number): number {
    return now - this.#at <= PULSE_MS ? this.#change : 0;
  }
}

const percent = (share: number) => `${String(Math.round(share * 100))}%`;
const clampShare = (share: number) => Math.min(1, Math.max(0, share));

/** Hull, 0 to 255 as the snapshot carries it; `lost` is a recent fall in those units. */
export function hullGauge(hull: number, shield: number, lost: number): Gauge {
  const share = clampShare(hull / 255);
  const notes = [
    ...(shield > 0 ? [`shield ${String(shield)}`] : []),
    ...(lost < 0 ? [`hit −${percent(-lost / 255)}`] : []),
    ...(share < 0.3 ? ["critical: no repairs until you dock"] : []),
  ];
  return {
    key: "hull",
    label: "Hull",
    share,
    value: percent(share),
    note: notes.join(" · "),
    level: share < 0.3 ? "danger" : share < 0.6 ? "warn" : "ok",
  };
}

/**
 * The hold. `contractCargo` is what a ship must dock with for the contract to pay: until the hold
 * has it, the gauge says how far short it is, so a pilot never docks short by surprise.
 */
export function holdGauge(own: OwnStatus, gained: number, contractCargo = 0): Gauge {
  const share = own.capacity > 0 ? clampShare(own.cargo / own.capacity) : 0;
  const full = own.capacity > 0 && own.cargo >= own.capacity;
  const short = contractCargo - own.cargo;
  const notes = [
    ...(gained > 0 ? [`+${String(gained)} cargo`] : []),
    full
      ? "full: fly home and dock to bank it"
      : short > 0
        ? `contract pays from ${String(contractCargo)}: ${String(short)} more`
        : own.cargo > 0
          ? (contractCargo > 0 ? "enough for the contract · " : "") +
            "counts only once docked at home"
          : "",
  ].filter((note) => note !== "");
  return {
    key: "hold",
    label: "Hold",
    share,
    value: `${String(own.cargo)}/${String(own.capacity)}`,
    note: notes.join(" · "),
    level: full ? "warn" : "ok",
  };
}

export function heatGauge(own: OwnStatus): Gauge {
  const share = clampShare(own.heat / 255);
  const seconds = String(own.extractorSeconds);
  const note = {
    on:
      share >= 0.75 ? "extracting: near cut-out, V to vent" : "extracting: heat rises, V vents it",
    venting: `venting ${seconds} s: extraction paused`,
    cutOut: `CUT OUT ${seconds} s: overheated, extractor off`,
    // The key to start it shows as a prompt when a deposit is in reach (game/prompts).
    off: share > 0 ? "extractor off, cooling" : "extractor off",
  }[own.extractor];
  return {
    key: "heat",
    label: "Heat",
    share,
    value: percent(share),
    note,
    level: own.extractor === "cutOut" || share >= 0.9 ? "danger" : share >= 0.75 ? "warn" : "ok",
  };
}

/** The three gauges, top to bottom. */
export function gauges(
  own: OwnStatus,
  hull: number,
  hullChange: number,
  cargoChange: number,
  contractCargo = 0,
): Gauge[] {
  return [
    hullGauge(hull, own.shield, hullChange),
    holdGauge(own, cargoChange, contractCargo),
    heatGauge(own),
  ];
}
