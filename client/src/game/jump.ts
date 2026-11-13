/**
 * The jump drive as the pilot works it: choose a destination on the system map, press J, and the
 * drive spools before the ship jumps (frontier_sim::jump). Choosing works by click, tap or a
 * digit key, never only by right click or hover (design §5). Kept free of the DOM so it can be
 * tested.
 */
import type { JumpCancel, JumpRefusal, JumpTarget, PocketKind } from "../protocol";
import { POCKET_NAME } from "./contract";

/** The dock's name as a destination; a pocket is named by its kind. */
export const DOCK_MARKER = "dock";

/** The jump target a destination names, or null for none it can. */
export function targetOf(
  marker: string,
  pockets: readonly { kind: PocketKind }[],
): JumpTarget | null {
  if (marker === DOCK_MARKER) return { type: "dock" };
  const index = pockets.findIndex((pocket) => pocket.kind === marker);
  return index >= 0 ? { type: "pocket", index } : null;
}

/** The destination a jump target names. */
export function markerOf(target: JumpTarget, pockets: readonly { kind: PocketKind }[]): string {
  return target.type === "dock" ? DOCK_MARKER : (pockets[target.index]?.kind ?? "");
}

/** What a pilot calls a jump target, in a sentence. */
export function targetName(target: JumpTarget, pockets: readonly { kind: PocketKind }[]): string {
  if (target.type === "dock") return "the dock";
  const pocket = pockets[target.index];
  return pocket ? POCKET_NAME[pocket.kind] : "the pocket";
}

export const REFUSAL_TEXT: Readonly<Record<JumpRefusal, string>> = {
  notFlying: "Jump drive: not while docked",
  hunted: "Jump drive jammed: a hostile has you locked",
  extracting: "Jump drive: stop the extractor first (X)",
  unavailable: "Jump drive: that pocket is locked",
  alreadyThere: "Jump drive: you are already there",
};

export const CANCEL_TEXT: Readonly<Record<JumpCancel, string>> = {
  pilot: "Jump cancelled",
  hunted: "Jump cancelled: a hostile locked you",
  extracting: "Jump cancelled: the extractor started",
};

/** A jump spooling, as the server announced it. */
export interface Spool {
  target: JumpTarget;
  /** When the ship jumps, on the page's clock, milliseconds. */
  endsAt: number;
  /** How long the whole spool takes, milliseconds. */
  lasts: number;
}

/** What the jump readout beside the minimap says. */
export interface JumpReadout {
  state: "idle" | "chosen" | "spooling";
  /** The destination chosen, or being jumped to. */
  target: string | null;
  text: string;
  /** The key that acts on it. */
  hint: string;
  /** How far through the spool, 0 to 1. */
  share: number;
}

/** The pilot's chosen destination and any spool in progress. */
export class JumpDrive {
  /** The destination chosen as the next jump's target. */
  selected: string | null = null;
  spool: Spool | null = null;

  /** Chooses `marker`, or clears the choice if it is chosen already. */
  toggle(marker: string): void {
    this.selected = this.selected === marker ? null : marker;
  }

  spooling(target: JumpTarget, seconds: number, now: number): void {
    this.spool = { target, endsAt: now + seconds * 1_000, lasts: seconds * 1_000 };
  }

  /** The spool ended: the ship jumped, or the spool was cancelled. */
  ended(): void {
    this.spool = null;
  }

  /** A new run starts with nothing chosen. */
  reset(): void {
    this.selected = null;
    this.spool = null;
  }

  /** Whole seconds until the jump, rounded up, or null with no spool. */
  secondsLeft(now: number): number | null {
    return this.spool ? Math.max(0, Math.ceil((this.spool.endsAt - now) / 1_000)) : null;
  }

  /**
   * What J does now: cancel a spool in progress, jump to the chosen destination, or nothing when
   * there is neither. `null` in `jump` cancels, as on the wire.
   */
  press(pockets: readonly { kind: PocketKind }[]): { jump: JumpTarget | null } | null {
    if (this.spool) return { jump: null };
    const target = this.selected === null ? null : targetOf(this.selected, pockets);
    return target ? { jump: target } : null;
  }

  /** The readout: the spool counting down, the destination chosen, or that there is none. */
  readout(now: number, pockets: readonly { kind: PocketKind }[]): JumpReadout {
    const left = this.secondsLeft(now);
    if (this.spool && left !== null) {
      const share = this.spool.lasts > 0 ? 1 - (this.spool.endsAt - now) / this.spool.lasts : 1;
      return {
        state: "spooling",
        target: markerOf(this.spool.target, pockets),
        text: `Jumping to ${targetName(this.spool.target, pockets)} in ${String(left)} s`,
        hint: "J cancels",
        share: Math.min(1, Math.max(0, share)),
      };
    }
    const target = this.selected === null ? null : targetOf(this.selected, pockets);
    if (target) {
      const name = target.type === "dock" ? "Dock" : targetName(target, pockets);
      return { state: "chosen", target: this.selected, text: name, hint: "J jumps", share: 0 };
    }
    return { state: "idle", target: null, text: "No destination", hint: "M map", share: 0 };
  }
}
