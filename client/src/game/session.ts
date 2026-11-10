/**
 * A pilot's part of an expedition: joins, sends controls every tick and keeps the snapshots
 * the server sends back.
 */
import type { ServerMessage } from "../protocol";
import type { Connection } from "../net/connection";
import { encodeInput } from "../net/input";
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

  constructor(connection: Connection, shipId: number, tickRate: number, now: () => number) {
    this.#connection = connection;
    this.shipId = shipId;
    this.tickRate = tickRate;
    this.snapshots = new SnapshotBuffer(tickRate);
    connection.onDatagram((bytes) => {
      try {
        this.snapshots.push(decodeSnapshot(bytes), now());
      } catch {
        // A malformed datagram is treated as lost.
      }
    });
  }

  /** Sends this tick's controls and returns the sequence number they were sent with. */
  sendControls(controls: Controls): number {
    this.#sequence = (this.#sequence + 1) & 0xffff;
    const datagram = encodeInput(this.#sequence, controls.thrust, controls.turn);
    // A send can fail while the session closes; the closed promise reports that.
    this.#connection.sendDatagram(datagram).catch(() => undefined);
    return this.#sequence;
  }
}
