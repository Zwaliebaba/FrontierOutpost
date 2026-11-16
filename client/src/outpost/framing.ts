/**
 * How wide the home view is, so the station fits between the HUD's panels (style guide §10).
 * One envelope holds the exterior turned any way the rules can stand it, every tell at its
 * largest, and the interior's cutaway, so neither a purchase, an upgrade nor opening the interior
 * changes the station's apparent scale.
 */

/**
 * The envelope from the resting camera (33° elevation), metres across and up the screen, round
 * the station's centre: the 32 m ring is about 34 m across from every side and 24 m up the
 * screen, its command tower above the plane and its core below; then the largest tell all round.
 */
export const HOME_ENVELOPE = { across: 37, up: 28 } as const;

/** Rows the HUD keeps above and below the view: the command bar, the footer and a margin. */
export const HOME_ROWS_PX = 150;

/**
 * The view width, metres across the whole window, that fits the envelope in the `gap` pixels
 * between the panels and in the window's height less the bars.
 */
export function homeViewWidth(width: number, height: number, gap: number): number {
  return Math.max(
    (HOME_ENVELOPE.across * width) / Math.max(1, gap),
    (HOME_ENVELOPE.up * width) / Math.max(240, height - HOME_ROWS_PX),
  );
}
