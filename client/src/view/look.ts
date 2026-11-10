import { angleDifference } from "./angles";

/**
 * How far the tester may move the camera away from the tactical view.
 * - `fixed`: not at all (the design's current rule).
 * - `hold`: orbit while a button is held; the view springs back on release.
 * - `free`: orbit, and the view stays where it is left.
 */
export const LOOK_MODES = ["fixed", "hold", "free"] as const;
export type LookMode = (typeof LOOK_MODES)[number];

export interface LookState {
  /** Radians away from the tactical bearing; positive orbits clockwise seen from above. */
  yaw: number;
  /** Camera elevation above the gameplay plane, in degrees. */
  pitch: number;
}

/** Never level with the plane (ships would stack) and never past vertical. */
export const PITCH_MIN = 8;
export const PITCH_MAX = 88;
const YAW_PER_PIXEL = 0.006;
const PITCH_PER_PIXEL = 0.25;
const SETTLED_YAW = 0.002;
const SETTLED_PITCH = 0.05;

const clamp = (value: number, min: number, max: number) => Math.min(max, Math.max(min, value));

/** Orbit by a pointer drag: right drags the scene right, down raises the camera. */
export function dragLook(state: LookState, dxPx: number, dyPx: number): LookState {
  return {
    yaw: state.yaw + dxPx * YAW_PER_PIXEL,
    pitch: clamp(state.pitch + dyPx * PITCH_PER_PIXEL, PITCH_MIN, PITCH_MAX),
  };
}

export function turnLook(state: LookState, radians: number): LookState {
  return { yaw: state.yaw + radians, pitch: state.pitch };
}

/** One step of the spring back to `home`, by the shortest way round; lands exactly on it. */
export function settleLook(state: LookState, home: LookState, dt: number, rate = 9): LookState {
  const yawError = angleDifference(home.yaw, state.yaw);
  const pitchError = home.pitch - state.pitch;
  if (Math.abs(yawError) < SETTLED_YAW && Math.abs(pitchError) < SETTLED_PITCH) return { ...home };
  const blend = 1 - Math.exp(-rate * dt);
  return { yaw: state.yaw + yawError * blend, pitch: state.pitch + pitchError * blend };
}

/** Narrowest and widest view the wheel can reach, in metres across. */
export const ZOOM_MIN = 12;
export const ZOOM_MAX = 140;
const ZOOM_PER_PIXEL = 0.0015;
const PIXELS_PER_LINE = 16;
const PIXELS_PER_PAGE = 400;

/**
 * View width after a wheel event. Scrolling up (negative delta) zooms in. The change is
 * proportional, so each notch feels the same at any zoom. `deltaMode` is the WheelEvent's unit:
 * 0 pixels, 1 lines, 2 pages.
 */
export function zoomWidth(width: number, deltaY: number, deltaMode = 0): number {
  const pixels =
    deltaY * (deltaMode === 1 ? PIXELS_PER_LINE : deltaMode === 2 ? PIXELS_PER_PAGE : 1);
  return clamp(width * Math.exp(pixels * ZOOM_PER_PIXEL), ZOOM_MIN, ZOOM_MAX);
}

export function isHome(state: LookState, home: LookState): boolean {
  return state.yaw === home.yaw && state.pitch === home.pitch;
}
