import { describe, expect, it } from "vitest";
import { MAX_BOLTS, Shots } from "./shots";

describe("shots", () => {
  const positions = new Map([
    [1, { x: 0, z: 0 }],
    [2, { x: 10, z: 0 }],
  ]);
  const at = (id: number) => positions.get(id);

  it("flies a bolt toward its target at its speed", () => {
    const shots = new Shots();
    shots.fire(1, 2, { x: 0, z: 0 }, 20);
    shots.advance(0.25, at);
    expect(shots.bolts).toEqual([{ shooter: 1, x: 5, z: 0, heading: 0 }]);
  });

  it("follows a target that moves", () => {
    const shots = new Shots();
    shots.fire(1, 2, { x: 0, z: 0 }, 10);
    shots.advance(0.1, () => ({ x: 0, z: 10 }));
    const [bolt] = shots.bolts;
    expect(bolt?.x).toBeCloseTo(0);
    expect(bolt?.z).toBeCloseTo(1);
  });

  it("draws no more bolts than the server keeps in flight, and those the newest", () => {
    const shots = new Shots();
    for (let shooter = 0; shooter < 500; shooter += 1) shots.fire(shooter, 2, { x: 0, z: 0 }, 20);
    expect(shots.bolts.length).toBe(MAX_BOLTS);
    expect(shots.bolts[0]?.shooter).toBe(500 - MAX_BOLTS);
    expect(shots.bolts.at(-1)?.shooter).toBe(499);
  });

  it("disappears on arrival or when its target is gone", () => {
    const shots = new Shots();
    shots.fire(1, 2, { x: 0, z: 0 }, 20);
    shots.fire(1, 3, { x: 0, z: 0 }, 20);
    shots.advance(0.6, at);
    expect(shots.bolts).toEqual([]);
  });

  it("gives up after three seconds whatever happens", () => {
    const shots = new Shots();
    shots.fire(1, 2, { x: 0, z: 0 }, 0.1);
    for (let i = 0; i < 31; i += 1) shots.advance(0.1, at);
    expect(shots.bolts).toEqual([]);
  });

  it("ages a blast from 0 and drops it once faded", () => {
    const shots = new Shots();
    shots.blast({ x: 3, z: 4 });
    shots.advance(0.3, at);
    expect(shots.blasts[0]?.age).toBeCloseTo(0.5);
    shots.advance(0.3, at);
    expect(shots.blasts).toEqual([]);
  });
  it("drops a bolt that point defence intercepts, with a flash where it was", () => {
    const shots = new Shots();
    shots.fire(3, 7, { x: 0, z: 0 }, 20);
    shots.fire(4, 7, { x: 0, z: 0 }, 20);
    shots.advance(0.1, () => ({ x: 10, z: 0 }));
    expect(shots.intercept(3, 7, { x: 10, z: 0 })).toBe(true);
    expect(shots.bolts.map((bolt) => bolt.shooter)).toEqual([4]);
    expect(shots.blasts).toHaveLength(1);
    expect(shots.intercept(3, 7, { x: 10, z: 0 }), "already gone").toBe(false);
  });
});
