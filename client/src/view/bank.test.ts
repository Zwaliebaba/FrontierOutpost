import { describe, expect, it } from "vitest";
import { Object3D, Vector3 } from "three";
import { MAX_BANK, headingDelta, stepBank } from "./bank";
import { placeShip } from "./readability";
import type { ShipMeshes } from "./readability";

const settle = (rate: number) => {
  let bank = 0;
  let heading = 0;
  for (let i = 0; i < 120; i++) {
    const next = heading + rate / 60;
    bank = stepBank(bank, heading, next, 1 / 60);
    heading = next;
  }
  return bank;
};

describe("headingDelta", () => {
  it("takes the short way round", () => {
    expect(headingDelta(0.1, 6.2)).toBeCloseTo(6.2 - 0.1 - Math.PI * 2);
    expect(headingDelta(6.2, 0.1)).toBeCloseTo(0.1 - 6.2 + Math.PI * 2);
    expect(headingDelta(1, 1.5)).toBeCloseTo(0.5);
  });
});

describe("stepBank", () => {
  it("leans in proportion to the turn, up to the limit, both ways", () => {
    expect(settle(2.4)).toBeCloseTo(MAX_BANK, 3);
    expect(settle(-1.2)).toBeCloseTo(-MAX_BANK / 2, 3);
    expect(settle(20)).toBeCloseTo(MAX_BANK, 3);
  });

  it("levels out when the turn stops", () => {
    let bank = settle(2.4);
    for (let i = 0; i < 120; i++) bank = stepBank(bank, 1, 1, 1 / 60);
    expect(Math.abs(bank)).toBeLessThan(1e-3);
  });

  it("eases in rather than snapping, and ignores a frame with no time", () => {
    const first = stepBank(0, 0, 0.04, 1 / 60);
    expect(first).toBeGreaterThan(0);
    expect(first).toBeLessThan(MAX_BANK / 2);
    expect(stepBank(0.2, 0, 1, 0)).toBe(0.2);
  });

  it("does not lean across the heading's wrap", () => {
    expect(Math.abs(stepBank(0, Math.PI * 2 - 0.01, 0.01, 1 / 60))).toBeLessThan(MAX_BANK);
    expect(stepBank(0, Math.PI * 2 - 0.01, 0.01, 1 / 60)).toBeGreaterThan(0);
  });
});

describe("placeShip with a lean", () => {
  const meshes = (): ShipMeshes => ({
    hull: new Object3D(),
    silhouette: new Object3D(),
    gear: [],
    dispose: () => {},
  });

  it("dips the side toward the turn and keeps the nose on the heading", () => {
    // Facing +x, a positive turn heads toward +z: the +z side is inside the turn.
    const ship = meshes();
    placeShip(ship, 0, 0, 0, 0.4);
    ship.hull.updateMatrixWorld();
    const inside = new Vector3(0, 0, 1).applyMatrix4(ship.hull.matrixWorld);
    const nose = new Vector3(1, 0, 0).applyMatrix4(ship.hull.matrixWorld);
    const centre = ship.hull.position.y;
    expect(inside.y).toBeLessThan(centre);
    expect(nose.y).toBeCloseTo(centre);
    expect(nose.x).toBeGreaterThan(0);
  });

  it("rolls about the ship's own length whatever its heading", () => {
    const ship = meshes();
    placeShip(ship, 0, 0, Math.PI / 2, 0.4);
    ship.silhouette.updateMatrixWorld();
    // Facing +z, a positive turn heads toward -x.
    const nose = new Vector3(1, 0, 0).applyMatrix4(ship.silhouette.matrixWorld);
    const inside = new Vector3(0, 0, 1).applyMatrix4(ship.silhouette.matrixWorld);
    expect(nose.z).toBeCloseTo(1 + ship.silhouette.position.z);
    expect(nose.y).toBeCloseTo(ship.silhouette.position.y);
    expect(inside.x).toBeLessThan(0);
    expect(inside.y).toBeLessThan(ship.silhouette.position.y);
  });
});
