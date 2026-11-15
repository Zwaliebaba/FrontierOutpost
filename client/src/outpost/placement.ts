/**
 * Where the station stands, so the outpost and the expedition are one place (design §11): at the
 * site's dock, its hangar mouth facing the site's centre, the way ships launch. The rules own that
 * geometry (frontier_sim::station) and send it in `SiteInfo.station`; the client only turns the
 * models to match. The outpost view shows the station turned the same way, so launching only
 * moves the camera from the station to the ship.
 */
import type { StationInfo } from "../protocol";
import { SHIP_HEIGHT } from "../view/hulls";

/**
 * How high the station's origin stands (design §5: one plane). Hulls are drawn centred on the
 * plane (view/hulls), so the hangar deck and the mouth's sill, at the models' z = 0, lie half a
 * hull below it: a ship flies through the mouth at deck level, its underside on the deck.
 */
export const STATION_HEIGHT = -SHIP_HEIGHT / 2;

export interface Placement {
  x: number;
  z: number;
  /** Rotation about Y, radians, as three.js applies it. */
  yaw: number;
  /** Height of the station's origin: `STATION_HEIGHT`. */
  y: number;
  /** The heading out of the hangar mouth, in the ships' convention: (cos h, sin h) in (x, z). */
  heading: number;
}

/** The part of `StationInfo` that places the models: where it stands and which way it faces. */
export type StationPose = Pick<StationInfo, "centreX" | "centreZ" | "forwardX" | "forwardZ">;

/**
 * Before any contract names a site: the station at the origin facing +x, the rules' own fallback
 * (`Station::new` with no facing). The first contract turns it to its site's dock at once.
 */
export const HOME_STATION: StationPose = { centreX: 0, centreZ: 0, forwardX: 1, forwardZ: 0 };

/**
 * The models placed as the rules stand the station. The station's frame (across, along) is the
 * models' Blender (X, Y), which glTF carries as (x, −z); world (x, z) is centre + across·(−Fz, Fx)
 * + along·(Fx, Fz). Turning by ψ = atan2(−Fx, −Fz) about +Y takes glTF −z onto F and +x onto
 * (−Fz, Fx), so the model is not mirrored.
 */
export function placeStation(station: StationPose): Placement {
  const length = Math.hypot(station.forwardX, station.forwardZ);
  const [fx, fz] = length > 1e-9 ? [station.forwardX / length, station.forwardZ / length] : [1, 0];
  return {
    x: station.centreX,
    z: station.centreZ,
    yaw: Math.atan2(-fx, -fz),
    y: STATION_HEIGHT,
    heading: Math.atan2(fz, fx),
  };
}
