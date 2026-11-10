import { describe, expect, it } from "vitest";
import type { EntityState } from "../protocol";
import {
  INVALID_SNAPSHOTS,
  MAX_DATAGRAM_BYTES,
  VALID_SNAPSHOTS,
} from "../protocol/generated/snapshotVectors";
import {
  SnapshotError,
  decodeSnapshot,
  headingRadians,
  positionMetres,
  velocityMetresPerSecond,
} from "./snapshot";

const fromHex = (hex: string) =>
  Uint8Array.from(hex.match(/../g) ?? [], (pair) => Number.parseInt(pair, 16));

describe("snapshot decoder", () => {
  it.each(VALID_SNAPSHOTS)("decodes the Rust encoder's '$name' exactly", ({ hex, snapshot }) => {
    expect(decodeSnapshot(fromHex(hex))).toEqual(snapshot);
  });

  it("fits the full-expedition vector in one datagram", () => {
    const full = VALID_SNAPSHOTS.find((vector) => vector.name === "full expedition");
    expect(full?.snapshot.entities).toHaveLength(28);
    expect(fromHex(full?.hex ?? "").length).toBeLessThanOrEqual(MAX_DATAGRAM_BYTES);
  });

  it.each(INVALID_SNAPSHOTS)("rejects '$name'", ({ hex }) => {
    expect(() => decodeSnapshot(fromHex(hex))).toThrow(SnapshotError);
  });

  it("decodes from a view into a larger buffer", () => {
    const vector = VALID_SNAPSHOTS.find((v) => v.name === "extremes");
    const bytes = fromHex(vector?.hex ?? "");
    const padded = new Uint8Array(bytes.length + 7);
    padded.set(bytes, 3);
    expect(decodeSnapshot(padded.subarray(3, 3 + bytes.length))).toEqual(vector?.snapshot);
  });
});

describe("snapshot units", () => {
  const entity: EntityState = {
    id: 1,
    kind: "drone",
    x: -16,
    z: 32,
    heading: 16_384,
    vx: 576,
    vz: -64,
    hull: 255,
  };

  it("converts to metres, metres per second and radians", () => {
    expect(positionMetres(entity)).toEqual({ x: -0.5, z: 1 });
    expect(velocityMetresPerSecond(entity)).toEqual({ x: 9, z: -1 });
    expect(headingRadians(entity)).toBeCloseTo(Math.PI / 2, 12);
  });
});
