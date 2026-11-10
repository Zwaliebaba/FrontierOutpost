import { describe, expect, it } from "vitest";
import { INPUT_VECTORS } from "../protocol/generated/inputVectors";
import { encodeInput, isNewer, quantizeControl } from "./input";

const toHex = (bytes: Uint8Array) =>
  [...bytes].map((byte) => byte.toString(16).padStart(2, "0")).join("");

describe("input encoder", () => {
  it.each(INPUT_VECTORS)(
    "encodes sequence $sequence, thrust $thrust, turn $turn as Rust expects",
    ({ sequence, thrust, turn, hex }) => {
      expect(toHex(encodeInput(sequence, thrust, turn))).toBe(hex);
    },
  );

  it("wraps the sequence at 65,536", () => {
    expect(toHex(encodeInput(65_536, 0, 0))).toBe(toHex(encodeInput(0, 0, 0)));
  });

  it("rounds halves away from zero, as Rust does", () => {
    // 0.5 / 127 * 127 = 0.5 exactly in the scaled range.
    expect(quantizeControl(0.5 / 127)).toBe(1);
    expect(quantizeControl(-0.5 / 127)).toBe(-1);
    expect(quantizeControl(Number.NaN)).toBe(0);
  });

  it("compares sequences across the wrap", () => {
    expect(isNewer(1, 0)).toBe(true);
    expect(isNewer(0, 0)).toBe(false);
    expect(isNewer(0, 65_535)).toBe(true);
    expect(isNewer(65_530, 10)).toBe(false);
  });
});
