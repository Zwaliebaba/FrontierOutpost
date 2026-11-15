/**
 * Ships round the station's hangar (plan 9.6). The rules fly a launch out of the hangar along the
 * lane and draw a captured ship back in (frontier_sim::station); the snapshots carry them there
 * like any other ship. Behind the mouth a ship is inside the station, under the hangar's roof:
 * the departure queue waits at the hangar's start, every ship on the one spot, so drawn there
 * the crew would stack on one another and their silhouettes would show through the hull. A ship
 * is drawn once any of it is outside the mouth, so it is seen leaving and entering.
 *
 * The geometry comes from `SiteInfo.station`, so nothing here repeats the rules' numbers. It
 * needs no WebGL, so it is tested on its own.
 */
import type { StationInfo } from "../protocol";
import { SHIP_LENGTH } from "../view/hulls";

/** What these rules read of the station. */
export type HangarInfo = Pick<
  StationInfo,
  "centreX" | "centreZ" | "forwardX" | "forwardZ" | "mouthX" | "mouthZ" | "mouthWidth"
>;

/** A point in the station's frame: across its axis, and along it from the centre, metres. */
export interface StationPoint {
  across: number;
  along: number;
}

/** The unit direction out of the mouth; +x if the station sends none. */
function forward(station: HangarInfo): { x: number; z: number } {
  const length = Math.hypot(station.forwardX, station.forwardZ);
  return length > 1e-9
    ? { x: station.forwardX / length, z: station.forwardZ / length }
    : { x: 1, z: 0 };
}

/**
 * `(x, z)` in the station's frame, as frontier_sim::station's `local`: +across is the forward
 * direction turned to (−Fz, Fx), the way the exterior is drawn (outpost/placement).
 */
export function stationPoint(station: HangarInfo, x: number, z: number): StationPoint {
  const f = forward(station);
  const dx = x - station.centreX;
  const dz = z - station.centreZ;
  return { across: -dx * f.z + dz * f.x, along: dx * f.x + dz * f.z };
}

/** The world position of a point in the station's frame. */
export function worldPoint(station: HangarInfo, point: StationPoint): { x: number; z: number } {
  const f = forward(station);
  return {
    x: station.centreX - point.across * f.z + point.along * f.x,
    z: station.centreZ + point.across * f.x + point.along * f.z,
  };
}

/** How far the mouth stands along the axis from the station's centre, metres. */
export function mouthDistance(station: HangarInfo): number {
  return stationPoint(station, station.mouthX, station.mouthZ).along;
}

/** Whether `(x, z)` lies behind the mouth, in the hangar between the hub and the mouth. */
function behindMouth(station: HangarInfo, point: StationPoint): boolean {
  return (
    Math.abs(point.across) <= station.mouthWidth / 2 &&
    point.along > 0 &&
    point.along < mouthDistance(station)
  );
}

/**
 * Whether a ship centred at `(x, z)` is wholly inside the hangar, so it is not drawn: neither
 * its hull, nor its silhouette through the hull, nor anything marking it. `length` is the hull's
 * length; a ship nose or tail first through the mouth shows once its end is outside it.
 */
export function insideHangar(
  station: HangarInfo,
  x: number,
  z: number,
  length = SHIP_LENGTH,
): boolean {
  const point = stationPoint(station, x, z);
  return behindMouth(station, point) && point.along + length / 2 < mouthDistance(station);
}

/**
 * Where the camera looks to follow a ship at `(x, z)`. Inside the hangar it holds on the mouth,
 * where the ship will come out (or went in), and from there follows it along the lane without a
 * jump: the camera does not sit over a ship no one can see.
 */
export function cameraFocus(station: HangarInfo, x: number, z: number): { x: number; z: number } {
  const point = stationPoint(station, x, z);
  if (!behindMouth(station, point)) return { x, z };
  return worldPoint(station, { across: point.across, along: mouthDistance(station) });
}
