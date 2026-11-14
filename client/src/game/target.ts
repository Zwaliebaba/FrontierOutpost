/**
 * The target readout: what is locked, how far it is, whether the weapons reach it, and its hull.
 * It sits under the top bar at the middle of the screen, the nearest fixed place to the lock
 * bracket, which the view centres. Kept free of the DOM so it can be tested.
 */
import type { EntityKind } from "../protocol";

const KIND_NAME: Readonly<Record<EntityKind, string>> = {
  playerShip: "Pilot",
  wingman: "Wingman",
  drone: "Drone",
  raider: "Raider",
  warden: "Warden",
  core: "Nest core",
};

export interface TargetReadout {
  name: string;
  distance: string;
  inRange: boolean;
  /** Said in words as well as shown by colour. */
  range: string;
  /** 0 to 1. */
  hullShare: number;
  hull: string;
}

/** The readout for a locked `kind` with `id`, `metres` away. `hull` is 0 to 255. */
export function targetReadout(
  kind: EntityKind,
  id: number,
  metres: number,
  weaponRange: number,
  hull: number,
): TargetReadout {
  const inRange = metres <= weaponRange;
  const hullShare = Math.min(1, Math.max(0, hull / 255));
  return {
    name: `${KIND_NAME[kind]} ${String(id)}`,
    distance: `${metres.toFixed(0)} m`,
    inRange,
    range: inRange ? "In range" : `Out of range · fires within ${weaponRange.toFixed(0)} m`,
    hullShare,
    hull: `hull ${String(Math.round(hullShare * 100))}%`,
  };
}
