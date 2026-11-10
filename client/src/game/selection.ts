/**
 * What the player has selected and locked (design §5): one selection serves the camera and
 * targeting. Selecting a hostile locks it; selecting an ally only centres the view on it. The
 * lock is what the weapon fires at, and the server accepts it only for an existing hostile.
 */
import type { EntityKind } from "../protocol";

export interface Candidate {
  id: number;
  kind: EntityKind;
  x: number;
  z: number;
}

const isHostile = (kind: EntityKind) => kind !== "playerShip";

export class Selection {
  /** The ship the camera centres on, if not the player's own. */
  focus: number | null = null;
  /** The locked hostile. */
  lock: number | null = null;
  readonly #onLock: (target: number | null) => void;

  /** `onLock` is called whenever the lock changes, to tell the server. */
  constructor(onLock: (target: number | null) => void) {
    this.#onLock = onLock;
  }

  /**
   * A click on a ship, or on empty space with `null`. A hostile within `lockRange` of `from` is
   * locked; one beyond it is only centred on, because the server would refuse the lock and the
   * player would see a lock their weapon cannot use. Returns `"beyond"` in that case.
   */
  click(
    picked: Candidate | null,
    from?: { x: number; z: number },
    lockRange = Infinity,
  ): "locked" | "centred" | "beyond" {
    if (!picked) {
      // Empty space returns the view to your own ship; the lock stays.
      this.focus = null;
      return "centred";
    }
    this.focus = picked.id;
    if (!isHostile(picked.kind)) return "centred";
    const distance = from ? Math.hypot(picked.x - from.x, picked.z - from.z) : 0;
    if (distance > lockRange) return "beyond";
    this.#setLock(picked.id);
    return "locked";
  }

  /**
   * Locks the nearest hostile to `from` within `lockRange`, or on a repeated press the next
   * nearest after the current lock, wrapping round. The view centres on it. Returns whether
   * anything could be locked.
   */
  cycle(
    from: { x: number; z: number },
    candidates: readonly Candidate[],
    lockRange = Infinity,
  ): boolean {
    const hostiles = candidates
      .filter(
        (candidate) =>
          isHostile(candidate.kind) &&
          Math.hypot(candidate.x - from.x, candidate.z - from.z) <= lockRange,
      )
      .sort(
        (a, b) => Math.hypot(a.x - from.x, a.z - from.z) - Math.hypot(b.x - from.x, b.z - from.z),
      );
    if (hostiles.length === 0) return false;
    const current = hostiles.findIndex((candidate) => candidate.id === this.lock);
    const next = hostiles[(current + 1) % hostiles.length] ?? hostiles[0];
    if (!next) return false;
    this.focus = next.id;
    this.#setLock(next.id);
    return true;
  }

  /** Releases the lock and returns the view to your own ship. */
  release(): void {
    this.focus = null;
    this.#setLock(null);
  }

  /** Steering takes the view back to your own ship, so it is never flown off screen. */
  steer(): void {
    this.focus = null;
  }

  /** Forgets ships that are no longer in the expedition. */
  prune(present: ReadonlySet<number>): void {
    if (this.focus !== null && !present.has(this.focus)) this.focus = null;
    if (this.lock !== null && !present.has(this.lock)) this.#setLock(null);
  }

  #setLock(target: number | null): void {
    if (target === this.lock) return;
    this.lock = target;
    this.#onLock(target);
  }
}
