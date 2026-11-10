/**
 * Frame cost as the player's machine sees it (plan work item 2.6): how long the browser spends
 * in each frame's callback, and how many frames it draws, summed over a window.
 */
export interface FrameSummary {
  /** Frames drawn per second over the window. */
  fps: number;
  /** Mean time spent in a frame's callback, milliseconds. */
  meanMs: number;
  /** The longest frame callback in the window, milliseconds. */
  worstMs: number;
}

export class FrameTimer {
  readonly #windowMs: number;
  #windowStart: number | null = null;
  #frames = 0;
  #total = 0;
  #worst = 0;

  constructor(windowMs = 1_000) {
    this.#windowMs = windowMs;
  }

  /** Records a frame callback that ran from `start` to `end`. Returns a summary once per window. */
  record(start: number, end: number): FrameSummary | null {
    this.#windowStart ??= start;
    const work = end - start;
    this.#frames += 1;
    this.#total += work;
    this.#worst = Math.max(this.#worst, work);
    const elapsed = end - this.#windowStart;
    if (elapsed < this.#windowMs) return null;
    const summary = {
      fps: (this.#frames * 1_000) / elapsed,
      meanMs: this.#total / this.#frames,
      worstMs: this.#worst,
    };
    this.#windowStart = end;
    this.#frames = 0;
    this.#total = 0;
    this.#worst = 0;
    return summary;
  }
}
