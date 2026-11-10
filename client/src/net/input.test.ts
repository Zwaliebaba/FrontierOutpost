import { describe, expect, it } from "vitest";
import { INPUT_VECTORS, MAX_INPUTS as RUST_MAX_INPUTS } from "../protocol/generated/inputVectors";
import { MAX_INPUTS, encodeInput, isNewer, quantizeControl } from "./input";

const toHex = (bytes: Uint8Array) =>
  [...bytes].map((byte) => byte.toString(16).padStart(2, "0")).join("");

describe("input encoder", () => {
  it.each(INPUT_VECTORS)(
    "encodes sequence $sequence with $inputs.length inputs as Rust expects",
    ({ sequence, inputs, hex }) => {
      expect(toHex(encodeInput(sequence, inputs))).toBe(hex);
    },
  );

  it("carries as many inputs as the server accepts", () => {
    expect(MAX_INPUTS).toBe(RUST_MAX_INPUTS);
    const none: { thrust: number; turn: number; fire: boolean }[] = [];
    expect(() => encodeInput(0, none)).toThrow(RangeError);
    const many = Array.from({ length: MAX_INPUTS + 1 }, () => ({ thrust: 0, turn: 0 }));
    expect(() => encodeInput(0, many)).toThrow(RangeError);
  });

  it("wraps the sequence at 65,536", () => {
    const still = [{ thrust: 0, turn: 0 }];
    expect(toHex(encodeInput(65_536, still))).toBe(toHex(encodeInput(0, still)));
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
