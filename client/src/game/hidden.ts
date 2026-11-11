/**
 * A hidden tab draws no frames, and the controls go out from the frame loop, so the last ones
 * sent would stay applied: the browser still answers the server's keep-alive, and the ship would
 * fly and fire unattended. The server lets go by itself after half a second of silence
 * (`SILENT_TICKS`); this lets the page let go at once.
 */
export interface Hideable {
  readonly hidden: boolean;
  addEventListener(type: "visibilitychange", listener: () => void): void;
}

/** Calls `release` each time `page` becomes hidden. */
export function onHidden(page: Hideable, release: () => void): void {
  page.addEventListener("visibilitychange", () => {
    if (page.hidden) release();
  });
}
