/**
 * Banking: a ship leans into its turns. Presentation only (AGENTS.md §4.5): the hull rolls about
 * its own length while its position, height and heading stay as the server and prediction have
 * them. The lean follows how fast the drawn heading is changing, so it needs no input and works
 * the same for the player's predicted ship and for interpolated ones.
 */

/** The steepest lean, radians: about 32°. */
export const MAX_BANK = 0.55;
/**
 * Radians of lean per radian per second of turn. The starter hull's full turn, 2.4 rad/s
 * (frontier_sim::ship::STARTER_HANDLING), leans it the full amount.
 */
export const BANK_PER_TURN_RATE = MAX_BANK / 2.4;
/** How quickly the lean follows the turn, per second: about a quarter second to settle. */
const RESPONSE = 8;

/** The signed difference `to - from` between two headings, in -π..π. */
export function headingDelta(from: number, to: number): number {
  const turn = Math.PI * 2;
  let delta = (to - from) % turn;
  if (delta > Math.PI) delta -= turn;
  if (delta < -Math.PI) delta += turn;
  return delta;
}

/**
 * The lean after `dt` seconds in which the heading went from `from` to `to`. Positive leans the
 * +z side down, into a turn toward +z (frontier_sim's positive turn). A heading that jumps, as a
 * reconciliation might, leans no further than [`MAX_BANK`].
 */
export function stepBank(bank: number, from: number, to: number, dt: number): number {
  if (!(dt > 0)) return bank;
  const rate = headingDelta(from, to) / dt;
  const target = Math.max(-MAX_BANK, Math.min(MAX_BANK, rate * BANK_PER_TURN_RATE));
  return bank + (target - bank) * (1 - Math.exp(-RESPONSE * dt));
}
