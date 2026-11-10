/**
 * Encoder for input datagrams. The layout is defined by `frontier_protocol::input`; the tests
 * check this encoder against vectors written by the Rust decoder's tests.
 */

export const INPUT_KIND = 2;
export const INPUT_BYTES = 5;

/** Scales -1..1 to -127..127, clamping and treating NaN as 0, exactly as Rust does. */
export function quantizeControl(value: number): number {
  if (Number.isNaN(value)) return 0;
  return roundHalfAwayFromZero(Math.min(1, Math.max(-1, value)) * 127);
}

/** Rust's `f32::round` rounds halves away from zero; `Math.round` rounds them up. */
function roundHalfAwayFromZero(value: number): number {
  return Math.sign(value) * Math.round(Math.abs(value));
}

/** One tick of controls. `sequence` wraps at 65,536. */
export function encodeInput(sequence: number, thrust: number, turn: number): Uint8Array {
  const bytes = new Uint8Array(INPUT_BYTES);
  const view = new DataView(bytes.buffer);
  view.setUint8(0, INPUT_KIND);
  view.setUint16(1, sequence & 0xffff, true);
  view.setInt8(3, quantizeControl(thrust));
  view.setInt8(4, quantizeControl(turn));
  return bytes;
}

/** Whether `candidate` comes after `current` in the wrapping 16-bit sequence. */
export function isNewer(candidate: number, current: number): boolean {
  const difference = (candidate - current) & 0xffff;
  return difference !== 0 && difference < 0x8000;
}
