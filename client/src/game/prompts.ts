/**
 * The prompts: the keys that would do something right now, shown only while they would. A
 * permanent line of key hints is read once and then ignored; a prompt that appears beside a
 * deposit says the extractor can start here. Kept free of the DOM so it can be tested.
 */
import type { OwnStatus } from "../protocol";

export interface Prompt {
  id: "vent" | "extract" | "deposit" | "fire" | "lock" | "scan" | "cruise";
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
  /** Whether a hostile is locked, and whether the weapons reach it. */
  locked: boolean;
  lockedInRange: boolean;
  /** Whether a hostile is close enough to lock. */
  hostileInLockRange: boolean;
}

/** As many prompts as fit beside the gauges without becoming a list to read. */
export const MOST_PROMPTS = 3;
/** The heat share from which venting is prompted: the gauge's own warning level. */
const VENT_FROM = 0.75;

/** The prompts for now, most urgent first, at most [`MOST_PROMPTS`]. */
export function prompts(input: PromptInput): Prompt[] {
  const { own, deposit } = input;
  const flying = own.phase === "atDock" || own.phase === "approach" || own.phase === "return";
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
    // who could already see the deposits did not know why to press the key.
    found.push({
      id: "scan",
      key: "C",
      text: "Scan · finds the rich pocket, relays and waves · hold still",
    });
  }
  if (flying && own.drive === "ready") {
    found.push({ id: "cruise", key: "Shift", text: "Hold to cruise" });
  }
  return found.slice(0, MOST_PROMPTS);
}
