/**
 * The prompts: the keys that would do something right now, shown only while they would. A
 * permanent line of key hints is read once and then ignored; a prompt that appears beside a
 * deposit says the extractor can start here. Kept free of the DOM so it can be tested.
 */
import type { OwnStatus } from "../protocol";

export interface Prompt {
  id:
    | "vent"
    | "extract"
    | "deposit"
    | "fire"
    | "lock"
    | "scan"
    | "scanRange"
    | "cruise"
    | "departing"
    | "docking"
    | "dock";
  /** The key to press, or empty for a prompt that only tells. */
  key: string;
  text: string;
}

export interface PromptInput {
  own: OwnStatus;
  /** The nearest deposit with anything left, if one is close by. */
  deposit: { distance: number; left: number; inReach: boolean } | null;
  /** Whether a scan has revealed the site. */
  scanned: boolean;
  /** How many metres the ship is beyond scan range of the site's centre; 0 within it. Ion fog
   * shortens the range to less than the site's own radius, so a ship can be in the site and
   * still out of range. */
  scanShort: number;
  /** Whether a hostile is locked, and whether the weapons reach it. */
  locked: boolean;
  lockedInRange: boolean;
  /** Whether a hostile is close enough to lock. */
  hostileInLockRange: boolean;
  /** Metres from the ship to the station's hangar mouth, or null if unknown. */
  toMouth: number | null;
}

/** As many prompts as fit beside the gauges without becoming a list to read. */
export const MOST_PROMPTS = 3;
/** The heat share from which venting is prompted: the gauge's own warning level. */
const VENT_FROM = 0.75;
/**
 * How near the hangar mouth, metres, the return leg says how to dock: the lane's 30 m and the
 * capture zone's 8 m in front of it, with room to line up.
 */
export const DOCK_PROMPT_RANGE = 60;

/** The prompts for now, most urgent first, at most [`MOST_PROMPTS`]. */
export function prompts(input: PromptInput): Prompt[] {
  const { own, deposit } = input;
  // On the launch's and the draw-in's script the rules fly the ship: no key does anything, so
  // the one prompt says so, and that the pilot's keys come back at the lane's end.
  if (own.phase === "atDock") {
    return [{ id: "departing", key: "", text: "Leaving the hangar · yours at the lane's end" }];
  }
  if (own.phase === "docking") {
    return [{ id: "docking", key: "", text: "Captured at the mouth · drawn in to dock" }];
  }
  const flying = own.phase === "approach" || own.phase === "return";
  const found: Prompt[] = [];
  if (own.extractor === "on" && own.heat / 255 >= VENT_FROM) {
    found.push({ id: "vent", key: "V", text: "Vent heat before the cut-out" });
  }
  if (input.locked && input.lockedInRange) {
    found.push({ id: "fire", key: "Space", text: "Fire" });
  } else if (!input.locked && input.hostileInLockRange) {
    found.push({ id: "lock", key: "Tab", text: "Lock the nearest hostile" });
  }
  if (deposit && own.extractor === "off" && own.cargo < own.capacity) {
    const left = `${String(deposit.left)} left`;
    found.push(
      deposit.inReach
        ? { id: "extract", key: "X", text: `Extract · ${left} · hold still` }
        : {
            id: "deposit",
            key: "",
            text: `Deposit ${deposit.distance.toFixed(0)} m · ${left} · move closer`,
          },
    );
  }
  if (own.phase === "objective" && !input.scanned && own.scan === null) {
    // Deposits show without a scan, so the prompt says what one adds (design §10): a playtester
    // who could already see the deposits did not know why to press the key. Out of range the key
    // would do nothing, so the prompt only tells, as the deposit's does.
    found.push(
      input.scanShort > 0
        ? {
            id: "scanRange",
            key: "",
            text: `Scan · ${input.scanShort.toFixed(0)} m nearer the site's centre`,
          }
        : {
            id: "scan",
            key: "C",
            text: "Scan · finds the rich pocket, relays and waves · hold still",
          },
    );
  }
  if (own.phase === "return" && input.toMouth !== null && input.toMouth <= DOCK_PROMPT_RANGE) {
    // Docking is flown (design §11): straight in along the lane, where the mouth catches it.
    found.push({
      id: "dock",
      key: "",
      text: `Dock · fly straight into the hangar mouth · ${input.toMouth.toFixed(0)} m`,
    });
  }
  if (flying && own.drive === "ready") {
    found.push({ id: "cruise", key: "Shift", text: "Hold to cruise" });
  }
  return found.slice(0, MOST_PROMPTS);
}
