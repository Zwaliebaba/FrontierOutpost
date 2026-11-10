/**
 * Client-side prediction of the player's own ship.
 *
 * Every tick the client applies its controls locally with the server's own movement rules, so
 * the ship answers at once instead of a round trip later. Each snapshot says which input the
 * server last applied; the client takes the server's state, replays the inputs the server has
 * not yet seen, and carries any difference as a visual offset that fades, so a correction
 * shows as a short glide rather than a jump.
 */
import { isNewer } from "../net/input";
import type { ShipControls, ShipState } from "../sim";

export type StepShip = (state: ShipState, controls: ShipControls) => ShipState;

interface Pending {
  sequence: number;
  controls: ShipControls;
}

/** How fast a correction fades, per second. */
const CORRECTION_DECAY = 10;
/** A correction larger than this is shown at once: gliding would look worse. */
const SNAP_DISTANCE = 4;
/** Inputs kept for replay; more means the server has stopped answering. */
const MAX_PENDING = 64;

export class Predictor {
  readonly #step: StepShip;
  #pending: Pending[] = [];
  #state: ShipState | null = null;
  #previous: ShipState | null = null;
  #offset = { x: 0, z: 0 };
  #lastCorrection = 0;

  constructor(step: StepShip) {
    this.#step = step;
  }

  /** The predicted state after the newest input, or null before the first snapshot. */
  get state(): ShipState | null {
    return this.#state;
  }

  /** Distance between the prediction and the server at the last snapshot, in metres. */
  get lastCorrection(): number {
    return this.#lastCorrection;
  }

  /** Applies this tick's controls, sent to the server as `sequence`. */
  advance(sequence: number, controls: ShipControls): void {
    this.#pending.push({ sequence, controls });
    if (this.#pending.length > MAX_PENDING) this.#pending.shift();
    if (!this.#state) return;
    this.#previous = this.#state;
    this.#state = this.#step(this.#state, controls);
  }

  /**
   * Accepts the server's state for the ship after it applied input `ack`. Returns how far the
   * prediction was from it, in metres.
   */
  reconcile(server: ShipState, ack: number): number {
    this.#pending = this.#pending.filter((input) => isNewer(input.sequence, ack));
    let replayed = server;
    for (const input of this.#pending) replayed = this.#step(replayed, input.controls);

    const predicted = this.#state;
    this.#state = replayed;
    if (!predicted) {
      this.#previous = replayed;
      this.#lastCorrection = 0;
      return 0;
    }
    const dx = predicted.x - replayed.x;
    const dz = predicted.z - replayed.z;
    this.#lastCorrection = Math.hypot(dx, dz);
    if (this.#lastCorrection > SNAP_DISTANCE) {
      this.#offset = { x: 0, z: 0 };
      this.#previous = replayed;
    } else {
      this.#offset = { x: this.#offset.x + dx, z: this.#offset.z + dz };
      if (this.#previous) {
        this.#previous = { ...this.#previous, x: this.#previous.x - dx, z: this.#previous.z - dz };
      }
    }
    return this.#lastCorrection;
  }

  /**
   * Where to draw the ship: between the last two predicted steps by `alpha` (0 to 1 through the
   * current tick), plus the fading correction. `dt` is the frame time in seconds.
   */
  visual(alpha: number, dt: number): ShipState | null {
    const current = this.#state;
    if (!current) return null;
    const previous = this.#previous ?? current;
    const fade = Math.exp(-CORRECTION_DECAY * dt);
    this.#offset = { x: this.#offset.x * fade, z: this.#offset.z * fade };
    const t = Math.min(1, Math.max(0, alpha));
    const turn = shortestTurn(previous.heading, current.heading);
    return {
      x: previous.x + (current.x - previous.x) * t + this.#offset.x,
      z: previous.z + (current.z - previous.z) * t + this.#offset.z,
      heading: previous.heading + turn * t,
      speed: current.speed,
    };
  }
}

function shortestTurn(from: number, to: number): number {
  const full = 2 * Math.PI;
  let turn = (to - from) % full;
  if (turn > Math.PI) turn -= full;
  if (turn <= -Math.PI) turn += full;
  return turn;
}
