import { describe, expect, it } from "vitest";
import type { EntityState, Snapshot } from "../protocol";
import { SnapshotBuffer, teleported } from "./interpolation";
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

  /**
   * A server at 20 Hz that stalls once at tick `at` and skips `skipped` ticks without making
   * them up, as its ticker does. The ship moves 1 m a tick; velocity is reported as zero, so
   * a pose drawn past the newest snapshot stands still and shows.
   */
  function afterSkip(skipped: number, at = 200, until = 600) {
    const buffer = new SnapshotBuffer(TICK_RATE);
    const arrival = (tick: number) => tick * 50 + 30 + (tick >= at ? skipped * 50 : 0);
    const drawn: number[] = [];
    for (let tick = 100; tick < until; tick += 1) {
      buffer.push(snapshot(tick, tick, 0), arrival(tick));
      // Three frames a tick, as a 60 Hz display draws them.
      for (const frame of [0, 17, 33]) drawn.push(xAt(buffer, arrival(tick) + frame) ?? Number.NaN);
    }
    return { buffer, arrival, drawn };
  }

  it("draws 100 ms behind again after the server skips ticks", () => {
    for (const skipped of [1, 2, 4, 10]) {
      const { buffer, arrival } = afterSkip(skipped);
      // Twenty seconds on: server time at the newest arrival is tick 599, so draw tick 597.
      expect(xAt(buffer, arrival(599)), `${String(skipped)} skipped`).toBeCloseTo(597, 5);
    }
  });

  it("never draws time running backwards, and is smooth again once it has recovered", () => {
    const { drawn } = afterSkip(4);
    const steps = drawn.slice(1).map((x, i) => x - (drawn[i] ?? x));
    expect(Math.min(...steps)).toBeGreaterThanOrEqual(0);
    // Until the window has passed and the offset has risen, the ship is drawn at the newest
    // snapshot and steps a tick at a time: 5 s, then 2 s to give back 200 ms. From tick 360 on
    // each frame moves a third of a tick, 0.33 m here.
    const recovered = steps.slice(3 * (360 - 100));
    expect(Math.max(...recovered)).toBeLessThan(0.5);
    expect(Math.min(...recovered)).toBeGreaterThan(0.2);
  });

  it("follows a client clock that runs fast", () => {
    // 200 ppm fast for ten minutes: local time gains 120 ms on the server's.
    const buffer = new SnapshotBuffer(TICK_RATE);
    const arrival = (tick: number) => (tick * 50 + 30) * 1.0002;
    for (let tick = 0; tick < 12_000; tick += 1)
      buffer.push(snapshot(tick, tick, 0), arrival(tick));
    expect(xAt(buffer, arrival(11_999))).toBeCloseTo(11_997, 1);
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

describe("teleported", () => {
  it("tells a jump from flight, however long the gap", () => {
    // The cruise drive at its fastest covers 1.35 m a tick; a jump arrives 40 m off at least.
    expect(teleported(1.35, 0.05)).toBe(false);
    expect(teleported(19, 0.05)).toBe(false);
    expect(teleported(120, 0.05)).toBe(true);
    // Ten seconds unseen at cruise speed is 270 m of flight, not a jump.
    expect(teleported(270, 10)).toBe(false);
    expect(teleported(700, 10)).toBe(true);
  });
});

describe("a ship that jumps between two snapshots", () => {
  it("is drawn where it was, then where it is, never in between", () => {
    const buffer = new SnapshotBuffer(TICK_RATE);
    buffer.push(snapshot(100, 0), 100 * 50);
    buffer.push(snapshot(101, 0), 101 * 50);
    buffer.push(snapshot(102, 400), 102 * 50);
    buffer.push(snapshot(103, 400), 103 * 50);
    // Drawn 100 ms behind: these sample server times 5,060 to 5,095, across the jump.
    const drawn = [5_160, 5_170, 5_180, 5_190, 5_195].map((now) => xAt(buffer, now));
    for (const x of drawn) expect(x === 0 || x === 400).toBe(true);
    expect(drawn[0]).toBe(0);
    expect(drawn.at(-1)).toBe(400);
  });
});
