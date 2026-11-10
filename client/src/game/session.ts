/**
 * A pilot's part of an expedition: joins, sends controls every tick and keeps the snapshots
 * the server sends back.
 */
import type { ObstacleInfo, ServerMessage, Snapshot } from "../protocol";
import type { Connection } from "../net/connection";
import { MAX_INPUTS, encodeInput, quantizeControl } from "../net/input";
import { SnapshotBuffer } from "../net/interpolation";
import { decodeSnapshot } from "../net/snapshot";

export interface Controls {
  thrust: number;
  turn: number;
  /** The trigger is held: fire at the locked target when ready and in range. */
  fire: boolean;
}

export class ExpeditionFullError extends Error {
  override name = "ExpeditionFullError";
}

/** Waits for the server to place this connection in the expedition. */
export function waitForJoin(
  connection: Connection,
): Promise<{ shipId: number; tickRate: number; obstacles: ObstacleInfo[]; weaponRange: number }> {
  return new Promise((resolve, reject) => {
    let settled = false;
    connection.onMessage((message: ServerMessage) => {
      if (settled) return;
      if (message.type === "joined") {
        settled = true;
        resolve({
          shipId: message.shipId,
          tickRate: message.tickRate,
          obstacles: message.obstacles,
          weaponRange: message.weaponRange,
        });
      } else if (message.type === "expeditionFull") {
        settled = true;
        reject(new ExpeditionFullError("The expedition already has four pilots."));
      }
    });
    void connection.closed.then((reason) => {
      if (!settled) reject(new Error(`disconnected before joining: ${reason}`));
    });
  });
}

export class Session {
  readonly shipId: number;
  readonly tickRate: number;
  readonly snapshots: SnapshotBuffer;
  readonly #connection: Connection;
  #sequence = 0;
  /** The controls most recently sent, newest first, resent with each datagram. */
  #recent: Controls[] = [];
  /** When each recent input was sent, by sequence, to measure the round trip. */
  #sentAt = new Map<number, number>();
  #lastAck: number | null = null;
  #roundTripMs: number | null = null;
  readonly #now: () => number;
  /** Set when this ship's flight ends; its snapshots and controls are then ignored. */
  #retired = false;

  /**
   * `onSnapshot` is called for each snapshot newer than any before it, after it is buffered.
   */
  constructor(
    connection: Connection,
    shipId: number,
    tickRate: number,
    now: () => number,
    onSnapshot: (snapshot: Snapshot) => void = () => undefined,
  ) {
    this.#connection = connection;
    this.#now = now;
    this.shipId = shipId;
    this.tickRate = tickRate;
    this.snapshots = new SnapshotBuffer(tickRate);
    connection.onDatagram((bytes) => {
      if (this.#retired) return;
      let snapshot: Snapshot;
      try {
        snapshot = decodeSnapshot(bytes);
      } catch {
        // A malformed datagram is treated as lost.
        return;
      }
      const before = this.snapshots.newest?.tick;
      this.snapshots.push(snapshot, now());
      this.#measure(snapshot.inputAck);
      if (before === undefined || snapshot.tick > before) onSnapshot(snapshot);
    });
  }

  /** Ends this ship's flight: later snapshots and controls belong to the next one. */
  retire(): void {
    this.#retired = true;
  }

  /**
   * Time from sending an input to a snapshot that shows it applied, smoothed, in milliseconds.
   * It includes up to one tick waiting for the server's step. Null until measured.
   */
  get roundTripMs(): number | null {
    return this.#roundTripMs;
  }

  #measure(ack: number): void {
    if (ack === this.#lastAck) return;
    this.#lastAck = ack;
    const sentAt = this.#sentAt.get(ack);
    if (sentAt === undefined) return;
    const sample = this.#now() - sentAt;
    this.#roundTripMs =
      this.#roundTripMs === null ? sample : this.#roundTripMs + (sample - this.#roundTripMs) * 0.1;
  }

  /**
   * Sends this tick's controls. Returns the sequence number they were sent with and the
   * controls as the server will read them, after quantisation, for prediction to use.
   */
  sendControls(controls: Controls): { sequence: number; sent: Controls } {
    this.#sequence = (this.#sequence + 1) & 0xffff;
    this.#recent = [controls, ...this.#recent].slice(0, MAX_INPUTS);
    this.#sentAt.set(this.#sequence, this.#now());
    this.#sentAt.delete((this.#sequence - 64) & 0xffff);
    const datagram = encodeInput(this.#sequence, this.#recent);
    // A send can fail while the session closes; the closed promise reports that.
    this.#connection.sendDatagram(datagram).catch(() => undefined);
    return { sequence: this.#sequence, sent: asServerReads(controls) };
  }
}

/** Controls as the server decodes them from an input datagram. */
export function asServerReads(controls: Controls): Controls {
  return {
    thrust: Math.max(-1, quantizeControl(controls.thrust) / 127),
    turn: Math.max(-1, quantizeControl(controls.turn) / 127),
    fire: controls.fire,
  };
}
