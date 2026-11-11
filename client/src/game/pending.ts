/**
 * Snapshot events waiting for the next frame to draw them. They arrive with the datagrams and
 * are drawn by the frame loop, where each ship is drawn. A hidden tab draws no frames but keeps
 * receiving, so an unbounded queue grew for as long as the tab stayed hidden and replayed every
 * shot of that time in the first frame back. Events are visuals of the moment: one older than
 * `MAX_AGE_TICKS` is no longer worth drawing, and is dropped as newer ones arrive.
 */

/** A second of server ticks at 20 Hz. */
export const MAX_AGE_TICKS = 20;

export class PendingEvents<Item> {
  #waiting: { tick: number; event: Item }[] = [];

  /** Queues the events of the snapshot at `tick`, and drops those now too old to draw. */
  push(tick: number, events: readonly Item[]): void {
    for (const event of events) this.#waiting.push({ tick, event });
    const oldest = tick - MAX_AGE_TICKS;
    if ((this.#waiting[0]?.tick ?? tick) < oldest) {
      this.#waiting = this.#waiting.filter((entry) => entry.tick >= oldest);
    }
  }

  /** Takes every event still waiting, oldest first. */
  take(): Item[] {
    return this.#waiting.splice(0).map((entry) => entry.event);
  }

  get size(): number {
    return this.#waiting.length;
  }
}
