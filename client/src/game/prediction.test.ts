import { describe, expect, it } from "vitest";
import wasm from "../sim/pkg/frontier_sim_wasm_bg.wasm?inline";
import { loadSimSync } from "../sim";
import type { ShipControls, ShipState } from "../sim";
import { createRng } from "../view/rng";
import type { Rng } from "../view/rng";
import { Predictor } from "./prediction";
import { asServerReads } from "./session";

// The real movement rules, compiled from Rust, as the browser runs them.
const sim = loadSimSync(
  Uint8Array.from(atob(wasm.slice(wasm.indexOf(",") + 1)), (c) => c.charCodeAt(0)),
);
const step = (state: ShipState, controls: ShipControls) => sim.stepShip(state, controls);

/** The state as a snapshot carries it: quantised as `frontier_protocol::snapshot` does. */
function asSnapshotCarries(state: ShipState): ShipState {
  const turn = 2 * Math.PI;
  const heading = ((Math.round((state.heading / turn) * 65_536) % 65_536) / 65_536) * turn;
  const vx = Math.round(Math.cos(state.heading) * state.speed * 64) / 64;
  const vz = Math.round(Math.sin(state.heading) * state.speed * 64) / 64;
  return {
    x: Math.round(state.x * 32) / 32,
    z: Math.round(state.z * 32) / 32,
    heading,
    speed: Math.hypot(vx, vz),
  };
}

interface Link {
  /** One-way delay in ticks for each message. */
  delay: (rng: Rng) => number;
  /** Chance that a datagram is lost. */
  loss: number;
}

/**
 * The server's input queue (`crates/server/src/input_queue.rs`): inputs not yet applied, one
 * applied per tick in order, the last repeated when none has arrived, and the oldest dropped
 * beyond a backlog of three.
 */
class InputQueue {
  #waiting: { sequence: number; controls: ShipControls }[] = [];
  #current: { sequence: number; controls: ShipControls } | null = null;

  receive(inputs: { sequence: number; controls: ShipControls }[]): void {
    for (const input of inputs) {
      const applied = this.#current !== null && input.sequence <= this.#current.sequence;
      const queued = this.#waiting.some((waiting) => waiting.sequence === input.sequence);
      if (!applied && !queued) this.#waiting.push(input);
    }
    this.#waiting.sort((a, b) => a.sequence - b.sequence);
  }

  takeForTick(): { sequence: number; controls: ShipControls } | null {
    if (this.#waiting.length > 3) this.#waiting.splice(0, this.#waiting.length - 3);
    this.#current = this.#waiting.shift() ?? this.#current;
    return this.#current;
  }
}

/**
 * Runs a pilot against a simulated server for `ticks` ticks, as the expedition does: each tick
 * the server applies the next queued input, steps, and sends a snapshot with its ack. Each
 * input datagram also carries the three inputs before it. Returns every correction the
 * predictor made, in metres.
 */
function fly(ticks: number, link: Link, controlsAt: (tick: number) => ShipControls): number[] {
  const rng = createRng(7);
  const predictor = new Predictor(step);
  const queue = new InputQueue();
  let server: ShipState = { x: 6, z: 0, heading: 0, speed: 0 };
  let serverControls: ShipControls = { thrust: 0, turn: 0 };
  let applied = 0;
  const sentInputs: ShipControls[] = [];
  const toServer: { at: number; inputs: { sequence: number; controls: ShipControls }[] }[] = [];
  const toClient: { at: number; state: ShipState; ack: number }[] = [];
  const corrections: number[] = [];

  for (let tick = 1; tick <= ticks; tick += 1) {
    for (const datagram of toServer.filter((message) => message.at === tick)) {
      queue.receive(datagram.inputs);
    }
    const next = queue.takeForTick();
    if (next) {
      applied = next.sequence;
      serverControls = next.controls;
    }
    server = step(server, serverControls);
    if (rng() >= link.loss) {
      toClient.push({ at: tick + link.delay(rng), state: asSnapshotCarries(server), ack: applied });
    }

    // Client: reconcile against whatever arrived, then send and predict this tick's input.
    for (const snapshot of toClient.filter((message) => message.at === tick)) {
      corrections.push(predictor.reconcile(snapshot.state, snapshot.ack));
    }
    const sent = asServerReads({ ...controlsAt(tick), fire: false, cruise: false });
    sentInputs.push(sent);
    const inputs = [];
    for (let back = 0; back < 4 && tick - back >= 1; back += 1) {
      inputs.push({ sequence: tick - back, controls: sentInputs[tick - back - 1] ?? sent });
    }
    if (rng() >= link.loss) toServer.push({ at: tick + link.delay(rng), inputs });
    predictor.advance(tick, sent);
  }
  return corrections;
}

/** Thrust most of the time, weaving left and right: a pilot dodging. */
const weaving = (tick: number): ShipControls & { fire: boolean } => ({
  fire: false,
  thrust: tick % 60 < 45 ? 1 : 0,
  turn: Math.floor(tick / 15) % 3 === 0 ? 0 : Math.floor(tick / 15) % 2 === 0 ? 1 : -1,
});

const max = (values: number[]) => Math.max(0, ...values);

const p95 = (values: number[]) =>
  [...values].sort((a, b) => a - b)[Math.floor(values.length * 0.95)] ?? Number.NaN;

/** One-way delays in ticks: 2 or 3 ticks each way is about 250 ms round trip with jitter. */
const target: Link = { delay: (rng) => 2 + Math.floor(rng() * 2), loss: 0 };

describe("prediction against the real movement rules", () => {
  it("needs no correction beyond snapshot rounding on a steady 200 ms link", () => {
    const corrections = fly(400, { delay: () => 2, loss: 0 }, weaving);
    expect(corrections.length).toBeGreaterThan(300);
    expect(max(corrections)).toBeLessThan(0.05);
  });

  it("absorbs jitter at the 250 ms target without correcting", () => {
    expect(max(fly(800, target, weaving))).toBeLessThan(0.05);
  });

  it("corrects only rarely and slightly with 5% loss each way", () => {
    const corrections = fly(800, { ...target, loss: 0.05 }, weaving);
    expect(p95(corrections)).toBeLessThan(0.05);
    expect(max(corrections)).toBeLessThan(1);
  });

  it("stays bounded on a bad link with 150 ms of jitter", () => {
    const corrections = fly(
      800,
      { delay: (rng) => 2 + Math.floor(rng() * 4), loss: 0.05 },
      weaving,
    );
    expect(max(corrections)).toBeLessThan(2.5);
  });

  it("settles to no correction once the pilot stops steering", () => {
    const corrections = fly(400, { ...target, loss: 0.05 }, (tick) =>
      tick < 200 ? weaving(tick) : { thrust: 0, turn: 0, fire: false },
    );
    expect(max(corrections.slice(-50))).toBeLessThan(0.05);
  });
});

describe("predictor", () => {
  const still: ShipState = { x: 0, z: 0, heading: 0, speed: 0 };
  const thrust = { thrust: 1, turn: 0 };

  it("waits for the first snapshot, then replays the inputs the server has not applied", () => {
    const predictor = new Predictor(step);
    predictor.advance(1, thrust);
    predictor.advance(2, thrust);
    expect(predictor.state).toBeNull();
    expect(predictor.reconcile(still, 0)).toBe(0);
    expect(predictor.state).toEqual(step(step(still, thrust), thrust));
  });

  it("glides a small correction away instead of jumping", () => {
    const predictor = new Predictor(step);
    predictor.reconcile(still, 0);
    predictor.reconcile({ ...still, x: 0.5 }, 0);
    const first = predictor.visual(1, 0.016)?.x ?? Number.NaN;
    expect(first).toBeLessThan(0.1);
    let last = first;
    for (let i = 0; i < 60; i += 1) last = predictor.visual(1, 0.016)?.x ?? Number.NaN;
    expect(last).toBeCloseTo(0.5, 2);
  });

  it("jumps straight to a large correction", () => {
    const predictor = new Predictor(step);
    predictor.reconcile(still, 0);
    predictor.reconcile({ ...still, x: 10 }, 0);
    expect(predictor.visual(1, 0.016)?.x).toBeCloseTo(10, 5);
  });
});
