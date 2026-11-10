import { describe, expect, it } from "vitest";
import { Selection } from "./selection";
import type { Candidate } from "./selection";

const drone: Candidate = { id: 10, kind: "drone", x: 5, z: 0 };
const raider: Candidate = { id: 11, kind: "raider", x: 20, z: 0 };
const warden: Candidate = { id: 12, kind: "warden", x: 50, z: 0 };
const ally: Candidate = { id: 2, kind: "playerShip", x: 3, z: 0 };
const here = { x: 0, z: 0 };

function selection() {
  const sent: (number | null)[] = [];
  return { selection: new Selection((target) => sent.push(target)), sent };
}

describe("selection", () => {
  it("locks and centres on a hostile that is clicked", () => {
    const { selection: s, sent } = selection();
    s.click(raider);
    expect([s.focus, s.lock]).toEqual([11, 11]);
    expect(sent).toEqual([11]);
  });

  it("centres on an ally without touching the lock", () => {
    const { selection: s, sent } = selection();
    s.click(drone);
    s.click(ally);
    expect([s.focus, s.lock]).toEqual([2, 10]);
    expect(sent).toEqual([10]);
  });

  it("returns the view on empty space but keeps the lock", () => {
    const { selection: s } = selection();
    s.click(drone);
    s.click(null);
    expect([s.focus, s.lock]).toEqual([null, 10]);
  });

  it("cycles from the nearest hostile outward and wraps round", () => {
    const { selection: s, sent } = selection();
    const all = [warden, ally, raider, drone];
    s.cycle(here, all);
    s.cycle(here, all);
    s.cycle(here, all);
    s.cycle(here, all);
    expect(sent).toEqual([10, 11, 12, 10]);
    expect(s.focus).toBe(10);
  });

  it("does nothing when cycling with no hostile near", () => {
    const { selection: s, sent } = selection();
    s.cycle(here, [ally]);
    expect(s.lock).toBeNull();
    expect(sent).toEqual([]);
  });

  it("releases the lock and the view", () => {
    const { selection: s, sent } = selection();
    s.click(drone);
    s.release();
    expect([s.focus, s.lock]).toEqual([null, null]);
    expect(sent).toEqual([10, null]);
  });

  it("drops the view when steering, keeping the lock", () => {
    const { selection: s } = selection();
    s.click(drone);
    s.steer();
    expect([s.focus, s.lock]).toEqual([null, 10]);
  });

  it("forgets a ship that has gone, telling the server only about the lock", () => {
    const { selection: s, sent } = selection();
    s.click(drone);
    s.prune(new Set([11, 12]));
    expect([s.focus, s.lock]).toEqual([null, null]);
    expect(sent).toEqual([10, null]);
  });
});
