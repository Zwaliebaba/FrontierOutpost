/**
 * The jump drive as the pilot works it: choose a nav marker, press J, and the drive spools before
 * the ship jumps (frontier_sim::jump). Choosing works by click or tap, never only by right click
 * or hover (design §5); right click on a marker is a shortcut that chooses and jumps at once.
 * Kept free of the DOM so it can be tested.
 */
import type { JumpCancel, JumpRefusal, JumpTarget, PocketKind } from "../protocol";
import { POCKET_NAME } from "./contract";

/** The nav marker id of the dock; pockets' markers are named by their kind. */
export const DOCK_MARKER = "dock";

/** The jump target a nav marker names, or null for none it can. */
export function targetOf(
  marker: string,
  pockets: readonly { kind: PocketKind }[],
): JumpTarget | null {
  if (marker === DOCK_MARKER) return { type: "dock" };
  const index = pockets.findIndex((pocket) => pocket.kind === marker);
  return index >= 0 ? { type: "pocket", index } : null;
}

/** The nav marker id a jump target names. */
export function markerOf(target: JumpTarget, pockets: readonly { kind: PocketKind }[]): string {
  return target.type === "dock" ? DOCK_MARKER : (pockets[target.index]?.kind ?? "");
}

/** What a pilot calls a jump target. */
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
}

/** The pilot's chosen marker and any spool in progress. */
export class JumpDrive {
  /** The nav marker chosen as the next jump's target. */
  selected: string | null = null;
  spool: Spool | null = null;

  /** Chooses `marker`, or clears the choice if it is chosen already. */
  toggle(marker: string): void {
    this.selected = this.selected === marker ? null : marker;
  }

  spooling(target: JumpTarget, seconds: number, now: number): void {
    this.spool = { target, endsAt: now + seconds * 1_000 };
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
   * What J does now: cancel a spool in progress, jump to the chosen marker, or nothing when
   * there is neither. `null` in `jump` cancels, as on the wire.
   */
  press(pockets: readonly { kind: PocketKind }[]): { jump: JumpTarget | null } | null {
    if (this.spool) return { jump: null };
    const target = this.selected === null ? null : targetOf(this.selected, pockets);
    return target ? { jump: target } : null;
  }

  /** The run line's words for the drive, or empty when there is nothing to say. */
  line(now: number, pockets: readonly { kind: PocketKind }[]): string {
    const left = this.secondsLeft(now);
    if (this.spool && left !== null) {
      return `jumping to ${targetName(this.spool.target, pockets)} in ${String(left)} s · J cancels`;
    }
    if (this.selected !== null) {
      const target = targetOf(this.selected, pockets);
      if (target) return `J jumps to ${targetName(target, pockets)}`;
    }
    return "";
  }
}
