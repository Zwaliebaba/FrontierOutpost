/**
 * The site scan as the HUD needs it: what to say when the server refuses one. The server
 * decides whether a scan starts (`Run::scan` in frontier-sim). Kept free of the DOM so it can
 * be tested.
 */
import type { ScanRefusal } from "../protocol";

export const SCAN_REFUSAL_TEXT: Readonly<Record<ScanRefusal, string>> = {
  notFlying: "Scan: not while docked",
  revealed: "Scan: the site is already scanned",
  scanning: "Scan: already scanning · hold still",
  outOfRange: "Scan: out of range · move closer to the site's centre",
  moving: "Scan: hold still first",
};
