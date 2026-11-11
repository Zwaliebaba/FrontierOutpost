/**
 * Where the station stands in a run, so the outpost and the expedition are one place: berth 07
 * sits on the site's dock, where pilots launch and dock, and faces the site, so a launching ship
 * flies straight off the deck and away from the station. The outpost view shows the station
 * turned the same way, so launching only moves the camera from the station to the ship.
 */
import manifest from "../../../Design/FrontierOutpost/scene_manifest.json";
import { SHIP_HEIGHT } from "../view/hulls";

interface Bounds {
  min: number[];
  max: number[];
}

const landed: Bounds = manifest.docking_validation.ship_bounds_station_space;

/**
 * The starter frigate landed on berth 07, in station space (glTF: Blender -Y is +Z), from the
 * generator's docking check: the middle of the deck, and the height of its landing feet.
 */
export const BERTH = {
  x: ((landed.min[0] ?? 0) + (landed.max[0] ?? 0)) / 2,
  z: -((landed.min[1] ?? 0) + (landed.max[1] ?? 0)) / 2,
  /** The height of the deck, where the landing feet rest. */
  deck: landed.min[2] ?? 0,
};

/**
 * How far below the gameplay plane the station's origin sits (design §5: one plane). Hulls are
 * drawn centred on the plane (view/hulls), so the deck lies half a hull below it, where a ship's
 * underside rests: otherwise the deck would hide the lower half and draw it as a silhouette.
 */
export const STATION_DROP = BERTH.deck + SHIP_HEIGHT / 2;

export interface Placement {
  x: number;
  z: number;
  /** Rotation about Y, radians, as three.js applies it. */
  yaw: number;
  /** Height of the station's origin: `-STATION_DROP`. */
  y: number;
  /** The heading a ship on the berth faces to fly away from the station. */
  heading: number;
}

/**
 * The station placed with its berth on `dock` and facing `toward` (the site's centre). The
 * berth's launch direction is station-space +Z, away from the hub.
 */
export function placeStation(
  dock: { x: number; z: number },
  toward: { x: number; z: number },
): Placement {
  const heading = Math.atan2(toward.z - dock.z, toward.x - dock.x);
  // Rotating about Y by `yaw` turns station-space +Z to (sin yaw, cos yaw) on the plane.
  const yaw = Math.PI / 2 - heading;
  const [sin, cos] = [Math.sin(yaw), Math.cos(yaw)];
  // The berth's world position is the station's plus the rotated berth offset.
  const bx = BERTH.x * cos + BERTH.z * sin;
  const bz = -BERTH.x * sin + BERTH.z * cos;
  return { x: dock.x - bx, z: dock.z - bz, yaw, y: -STATION_DROP, heading };
}
