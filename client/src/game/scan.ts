/**
 * The site scan as the HUD needs it: whether the scanner reaches the site from here, for the
 * prompt, and what to say when the server refuses a scan. The server decides whether a scan
 * starts (`Run::scan` in frontier-sim); this only mirrors its reach, so that the prompt does not
 * offer a key that would do nothing. Kept free of the DOM so it can be tested.
 */
import type { RunPhase, ScanRefusal } from "../protocol";

/** The fitting whose scanner reaches the site from the approach leg. */
export const DEEP_SCAN = "deep_scan";

export const SCAN_REFUSAL_TEXT: Readonly<Record<ScanRefusal, string>> = {
  notFlying: "Scan: not while docked",
  revealed: "Scan: the site is already scanned",
  scanning: "Scan: already scanning · hold still",
  outOfRange: "Scan: out of range · move closer to the site's centre",
  moving: "Scan: hold still first",
};

export interface ScanReach {
  /** Whether a scan started here would reach the site. */
  inReach: boolean;
  /** Metres to close before it would; 0 where distance is not what holds it back. */
  short: number;
}

export interface ScanReachInput {
  phase: RunPhase;
  /** The ship, and the site's centre, on the gameplay plane. */
  x: number;
  z: number;
  centreX: number;
  centreZ: number;
  /** How far from the site's centre a scan reaches, as the server sent it on joining. */
  range: number;
  /** The fittings flown, by identifier. */
  fitted: readonly string[];
}

/** The scanner's reach from where the ship is, or null once the pilot's run is over. */
export function scanReach(input: ScanReachInput): ScanReach | null {
  const { phase } = input;
  if (phase === "docked" || phase === "destroyed" || phase === "abandoned") return null;
  // The deep-scan array reaches from anywhere once the ship has left the dock.
  if (input.fitted.includes(DEEP_SCAN)) return { inReach: phase !== "atDock", short: 0 };
  const short = Math.hypot(input.x - input.centreX, input.z - input.centreZ) - input.range;
  return short <= 0 ? { inReach: true, short: 0 } : { inReach: false, short };
}
