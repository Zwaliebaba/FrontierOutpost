/**
 * The notices: short messages that answer something the pilot did or warn of something coming.
 * A refusal, a warning and a payout do not look alike, and one does not wipe out another: the
 * newest few are shown together, each for long enough to read. Kept free of the DOM so it can be
 * tested.
 */

/** What a notice is: news, something refused or to watch, or something gained. */
export type NoticeLevel = "info" | "warn" | "good";

export interface Notice {
  /** Counts up, so the page can tell a new notice from one it already shows. */
  id: number;
  text: string;
  level: NoticeLevel;
  /** When it goes, on the page's clock, milliseconds. */
  until: number;
}

/** How many notices show at once: more would be a log, not a notice. */
export const MOST_NOTICES = 3;
const SHORTEST_MS = 4_000;
const LONGEST_MS = 9_000;
/** Reading time for each character past a short line. */
const MS_PER_CHARACTER = 55;
const SHORT_LINE = 40;

/** How long a notice stays: longer for a longer one, within limits. */
export function noticeMs(text: string): number {
  const extra = Math.max(0, text.length - SHORT_LINE) * MS_PER_CHARACTER;
  return Math.min(LONGEST_MS, SHORTEST_MS + extra);
}

export class Notices {
  #shown: Notice[] = [];
  #next = 1;

  /**
   * Adds a notice at `now`. One that says what a notice on show already says takes its place,
   * so a key pressed five times does not fill the screen with five refusals.
   */
  push(text: string, level: NoticeLevel, now: number): void {
    this.#shown = this.#shown.filter((notice) => notice.text !== text);
    this.#shown.push({ id: this.#next, text, level, until: now + noticeMs(text) });
    this.#next += 1;
    this.#shown = this.#shown.slice(-MOST_NOTICES);
  }

  /** The notices on show at `now`, oldest first. */
  visible(now: number): readonly Notice[] {
    if (this.#shown.some((notice) => notice.until <= now)) {
      this.#shown = this.#shown.filter((notice) => notice.until > now);
    }
    return this.#shown;
  }
}
