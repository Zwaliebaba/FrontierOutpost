/**
 * What a facility opened, shown where the pilot will see it. A playtester built a sensor array
 * and saw nothing change: the derelict pocket it opened was listed as "open" in the briefing
 * like any other, and nothing in the run said it was new. Now a pocket a facility opens names
 * the facility, and one opened since the pilot's last run is called new in the briefing, tagged
 * on the system map and announced as the run begins. What the pilot has seen is a convenience
 * kept in the browser: lost, the pockets read as new once more. Kept free of the DOM.
 */
import type { PocketKind, SiteInfo } from "../protocol";
import { POCKET_NAME, POCKET_NEEDS } from "./contract";

const KINDS: readonly PocketKind[] = ["oreField", "derelict", "volatileVeins", "nestCore"];

/** The facility that opens `kind`, such as "sensor array", or null for a pocket always open. */
export function openedBy(kind: PocketKind): string | null {
  return POCKET_NEEDS[kind]?.replace(/^an? /, "") ?? null;
}

/** The open pockets of `site` that a facility opened. */
export function facilityPockets(site: Pick<SiteInfo, "pockets">): PocketKind[] {
  return site.pockets
    .filter((pocket) => !pocket.locked && openedBy(pocket.kind) !== null)
    .map((pocket) => pocket.kind);
}

/** Those of them the pilot had not flown with before; `seen` is null when nothing is stored. */
export function freshPockets(
  site: Pick<SiteInfo, "pockets">,
  seen: readonly PocketKind[] | null,
): PocketKind[] {
  return facilityPockets(site).filter((kind) => !(seen ?? []).includes(kind));
}

/** The stored list, or null when there is none or it is not one. */
export function parseSeen(stored: string | null): PocketKind[] | null {
  if (stored === null) return null;
  try {
    const parsed: unknown = JSON.parse(stored);
    if (!Array.isArray(parsed)) return null;
    return KINDS.filter((kind) => parsed.includes(kind));
  } catch {
    return null;
  }
}

/** What to store once a run on `site` is over: everything seen so far and what it opened. */
export function seenAfter(
  site: Pick<SiteInfo, "pockets">,
  seen: readonly PocketKind[] | null,
): string {
  const all = new Set([...(seen ?? []), ...facilityPockets(site)]);
  return JSON.stringify(KINDS.filter((kind) => all.has(kind)));
}

/** The briefing's line for an open pocket: which facility opened it, if one did. */
export function openLine(kind: PocketKind): string {
  const facility = openedBy(kind);
  return `${POCKET_NAME[kind]}: open${facility ? ` · your ${facility} opened it` : ""}`;
}

/** The notice as a run begins with `fresh` pockets, or null with none. */
export function unlockNotice(
  site: Pick<SiteInfo, "pockets">,
  fresh: readonly PocketKind[],
): string | null {
  const parts = site.pockets
    .filter((pocket) => fresh.includes(pocket.kind))
    .map((pocket) => {
      const yields = pocket.material ? `, yields ${pocket.material}` : "";
      return `${POCKET_NAME[pocket.kind]} (${openedBy(pocket.kind) ?? "a facility"}${yields})`;
    });
  return parts.length ? `New pocket open: ${parts.join(" and ")} · on the map (M)` : null;
}
