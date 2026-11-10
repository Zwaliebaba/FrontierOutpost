/**
 * A pilot's part of an expedition: joins, sends controls every tick and keeps the snapshots
 * the server sends back.
 */
import type { ServerMessage, Snapshot } from "../protocol";
import type { Connection } from "../net/connection";
import { MAX_INPUTS, encodeInput, quantizeControl } from "../net/input";
import { SnapshotBuffer } from "../net/interpolation";
import { decodeSnapshot } from "../net/snapshot";

export interface Controls {
  thrust: number;
  turn: number;
}

export class ExpeditionFullError extends Error {
  override name = "ExpeditionFullError";
}

/** Waits for the server to place this connection in the expedition. */
export function waitForJoin(connection: Connection): Promise<{ shipId: number; tickRate: number }> {
  return new Promise((resolve, reject) => {
    let settled = false;
    connection.onMessage((message: ServerMessage) => {
      if (settled) return;
      if (message.type === "joined") {
        settled = true;
        resolve({ shipId: message.shipId, tickRate: message.tickRate });
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
    this.shipId = shipId;
    this.tickRate = tickRate;
    this.snapshots = new SnapshotBuffer(tickRate);
    connection.onDatagram((bytes) => {
      let snapshot: Snapshot;
      try {
        snapshot = decodeSnapshot(bytes);
      } catch {
        // A malformed datagram is treated as lost.
        return;
      }
      const before = this.snapshots.newest?.tick;
      this.snapshots.push(snapshot, now());
      if (before === undefined || snapshot.tick > before) onSnapshot(snapshot);
    });
  }

  /**
   * Sends this tick's controls. Returns the sequence number they were sent with and the
   * controls as the server will read them, after quantisation, for prediction to use.
   */
  sendControls(controls: Controls): { sequence: number; sent: Controls } {
    this.#sequence = (this.#sequence + 1) & 0xffff;
    this.#recent = [controls, ...this.#recent].slice(0, MAX_INPUTS);
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
  };
}
