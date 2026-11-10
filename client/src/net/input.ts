/**
 * Encoder for input datagrams. The layout is defined by `frontier_protocol::input`; the tests
 * check this encoder against vectors written by the Rust decoder's tests.
 */

export const INPUT_KIND = 2;

/** Scales -1..1 to -127..127, clamping and treating NaN as 0, exactly as Rust does. */
export function quantizeControl(value: number): number {
  if (Number.isNaN(value)) return 0;
  return roundHalfAwayFromZero(Math.min(1, Math.max(-1, value)) * 127);
}

/** Rust's `f32::round` rounds halves away from zero; `Math.round` rounds them up. */
function roundHalfAwayFromZero(value: number): number {
  return Math.sign(value) * Math.round(Math.abs(value));
}

/** Inputs per datagram: the newest and three before it, so a lost datagram is recovered. */
export const MAX_INPUTS = 4;

/**
 * Encodes the input numbered `sequence` (wrapping at 65,536) and the ones before it: `inputs`
 * is newest first, 1 to `MAX_INPUTS` entries.
 */
export function encodeInput(
  sequence: number,
  inputs: readonly { thrust: number; turn: number }[],
): Uint8Array {
  if (inputs.length < 1 || inputs.length > MAX_INPUTS) {
    throw new RangeError(`an input datagram carries 1 to ${String(MAX_INPUTS)} inputs`);
  }
  const bytes = new Uint8Array(4 + 2 * inputs.length);
  const view = new DataView(bytes.buffer);
  view.setUint8(0, INPUT_KIND);
  view.setUint16(1, sequence & 0xffff, true);
  view.setUint8(3, inputs.length);
  inputs.forEach((input, index) => {
    view.setInt8(4 + 2 * index, quantizeControl(input.thrust));
    view.setInt8(5 + 2 * index, quantizeControl(input.turn));
  });
  return bytes;
}

/** Whether `candidate` comes after `current` in the wrapping 16-bit sequence. */
export function isNewer(candidate: number, current: number): boolean {
  const difference = (candidate - current) & 0xffff;
  return difference !== 0 && difference < 0x8000;
}
