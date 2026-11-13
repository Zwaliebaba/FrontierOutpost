/**
 * A jump as the pilot sees it. The drive's spool shows on the pilot's own ship as rings closing
 * on the hull; any pilot's jump leaves a flash and a streak where the ship was and another where
 * it arrives. The server decides every jump: this only draws them, and it finds one by what a
 * jump looks like in the snapshots, a ship that moved farther than any drive flies, so a crew
 * mate's jump shows without the server saying so. Kept free of the DOM and of WebGL so it can be
 * tested.
 */
import type { EntityKind } from "../protocol";
import { teleported } from "../net/interpolation";

export interface Point {
  x: number;
  z: number;
}

/** The spool on a ship. */
export interface Charge {
  ship: number;
  /** How far through the spool, 0 to 1. */
  share: number;
  /** Where the closing ring is in its loop, 0 (far out) to 1 (at the hull). */
  pulse: number;
}

/** A flash where a ship left, or where it arrived. */
export interface Flash {
  kind: "leave" | "arrive";
  x: number;
  z: number;
  /** The way the ship jumped, radians on the plane, and how far, metres. */
  heading: number;
  distance: number;
  /** 0 when it starts, 1 when it has faded. */
  age: number;
}

/** How long each flash lasts, seconds. */
export const FLASH_SECONDS = 0.6;
/** The closing ring loops this often at the start of a spool and at its end, per second. */
const PULSE_RATE = { start: 0.7, end: 3 };
/** Flashes kept at once: four pilots jumping together leave eight. */
const MOST_FLASHES = 16;

interface Seen extends Point {
  kind: EntityKind;
}

export class JumpEffects {
  #seen = new Map<number, Seen>();
  #flashes: (Omit<Flash, "age"> & { seconds: number })[] = [];
  #charge: { ship: number; share: number } | null = null;
  #phase = 0;

  /**
   * Notes where each ship is drawn this frame, `seconds` after the last. A pilot's ship that
   * moved farther than flight allows jumped: it gets a flash at each end. Returns the ships
   * that jumped.
   */
  observe(
    poses: readonly { id: number; kind: EntityKind; x: number; z: number }[],
    seconds: number,
  ): number[] {
    const jumped: number[] = [];
    const now = new Map<number, Seen>();
    for (const pose of poses) {
      now.set(pose.id, { kind: pose.kind, x: pose.x, z: pose.z });
      const before = this.#seen.get(pose.id);
      // Only pilots have a jump drive. A ship first seen has no place it came from.
      if (pose.kind !== "playerShip" || before?.kind !== "playerShip") continue;
      const dx = pose.x - before.x;
      const dz = pose.z - before.z;
      const distance = Math.hypot(dx, dz);
      if (!teleported(distance, seconds)) continue;
      const heading = Math.atan2(dz, dx);
      this.#flashes.push(
        { kind: "leave", x: before.x, z: before.z, heading, distance, seconds: 0 },
        { kind: "arrive", x: pose.x, z: pose.z, heading, distance, seconds: 0 },
      );
      jumped.push(pose.id);
    }
    this.#seen = now;
    if (this.#flashes.length > MOST_FLASHES) {
      this.#flashes.splice(0, this.#flashes.length - MOST_FLASHES);
    }
    return jumped;
  }

  /** The spool on `ship`, `share` of the way through; null when no drive is spooling. */
  setCharge(ship: number | null, share = 0): void {
    if (ship === null) {
      this.#charge = null;
      this.#phase = 0;
      return;
    }
    this.#charge = { ship, share: Math.min(1, Math.max(0, share)) };
  }

  /** Ages the flashes and turns the spool's closing ring, `dt` seconds on. */
  advance(dt: number): void {
    for (const flash of this.#flashes) flash.seconds += dt;
    this.#flashes = this.#flashes.filter((flash) => flash.seconds < FLASH_SECONDS);
    if (this.#charge) {
      // The ring closes faster as the jump nears, so the last second reads as the last.
      const rate = PULSE_RATE.start + (PULSE_RATE.end - PULSE_RATE.start) * this.#charge.share;
      this.#phase = (this.#phase + dt * rate) % 1;
    }
  }

  get charge(): Charge | null {
    return this.#charge ? { ...this.#charge, pulse: this.#phase } : null;
  }

  get flashes(): Flash[] {
    return this.#flashes.map(({ seconds, ...flash }) => ({
      ...flash,
      age: seconds / FLASH_SECONDS,
    }));
  }

  /** A new run, or none: nothing carries over. */
  reset(): void {
    this.#seen.clear();
    this.#flashes = [];
    this.#charge = null;
    this.#phase = 0;
  }
}
