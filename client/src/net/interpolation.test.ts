import { describe, expect, it } from "vitest";
import type { EntityState, Snapshot } from "../protocol";
import { SnapshotBuffer } from "./interpolation";
import type { OwnStatus } from "../protocol";

const IDLE: OwnStatus = {
  phase: "atDock",
  cargo: 0,
  capacity: 0,
  heat: 0,
  extractor: "off",
  extractorSeconds: 0,
  scan: null,
  alert: 0,
  elapsedSeconds: 0,
  drive: "ready",
  shield: 0,
  relay: 0,
};

const TICK_RATE = 20;

/** A snapshot with one ship at `x` metres, moving +x at `vx` metres per second. */
function snapshot(tick: number, x: number, vx = 0, heading = 0): Snapshot {
  const ship: EntityState = {
    id: 1,
    kind: "playerShip",
    x: Math.round(x * 32),
    z: 0,
    heading,
    vx: Math.round(vx * 64),
    vz: 0,
    hull: 255,
  };
  return { tick, inputAck: 0, own: IDLE, deposits: [], entities: [ship], events: [] };
}

/** Pushes ticks 100.. with the ship at 0, 1, 2, ... m, each arriving `latency` ms late. */
function steady(count: number, latency = 30) {
  const buffer = new SnapshotBuffer(TICK_RATE);
  for (let i = 0; i < count; i += 1)
    buffer.push(snapshot(100 + i, i, 20), (100 + i) * 50 + latency);
  return buffer;
}

const xAt = (buffer: SnapshotBuffer, now: number) => buffer.sample(now)[0]?.x;

describe("snapshot interpolation", () => {
  it("draws entities 100 ms behind the newest server time", () => {
    const buffer = steady(10, 30);
    // At local time of tick 109's arrival, server time is tick 109; draw tick 107.
    expect(xAt(buffer, 109 * 50 + 30)).toBeCloseTo(7, 5);
  });

  it("interpolates between two snapshots", () => {
    const buffer = steady(10, 30);
    expect(xAt(buffer, 107 * 50 + 30 + 25 + 100)).toBeCloseTo(7.5, 5);
  });

  it("ignores how late snapshots arrive once the fastest one is known", () => {
    const buffer = new SnapshotBuffer(TICK_RATE);
    const delays = [30, 90, 45, 30, 120, 60, 30, 75, 30, 30];
    delays.forEach((delay, i) => {
      buffer.push(snapshot(100 + i, i, 20), (100 + i) * 50 + delay);
    });
    expect(xAt(buffer, 109 * 50 + 30)).toBeCloseTo(7, 5);
  });

  it("bridges a lost snapshot", () => {
    const buffer = new SnapshotBuffer(TICK_RATE);
    for (const i of [0, 1, 2, 4, 5, 6]) buffer.push(snapshot(100 + i, i, 20), (100 + i) * 50 + 30);
    expect(xAt(buffer, 103 * 50 + 30 + 100)).toBeCloseTo(3, 5);
  });

  it("extrapolates briefly past the newest snapshot, then holds", () => {
    const buffer = steady(3, 30);
    // Newest is tick 102 at x = 2 moving 20 m/s; 50 ms past it is x = 3.
    expect(xAt(buffer, 103 * 50 + 30 + 100)).toBeCloseTo(3, 1);
    // Far past it, extrapolation stops after 150 ms.
    expect(xAt(buffer, 120 * 50 + 30 + 100)).toBeCloseTo(5, 1);
  });

  it("turns through the shorter way across zero", () => {
    const buffer = new SnapshotBuffer(TICK_RATE);
    buffer.push(snapshot(100, 0, 0, 65_000), 100 * 50);
    buffer.push(snapshot(101, 0, 0, 500), 101 * 50);
    const heading = buffer.sample(100 * 50 + 25 + 100)[0]?.heading ?? Number.NaN;
    const turn = 2 * Math.PI;
    const expected = ((65_000 + (500 + 65_536 - 65_000) / 2) / 65_536) * turn;
    expect(((heading % turn) + turn) % turn).toBeCloseTo(expected % turn, 5);
  });

  it("ignores snapshots older than the newest", () => {
    const buffer = steady(5, 30);
    buffer.push(snapshot(101, 50), 105 * 50);
    expect(buffer.newest?.tick).toBe(104);
  });

  it("draws nothing before the first snapshot", () => {
    expect(new SnapshotBuffer(TICK_RATE).sample(0)).toEqual([]);
  });
});
