/**
 * Navigation markers for the dock and the site's pockets. The legs are long (design §10: about
 * three minutes each way), so each target is labelled where it is on screen, or at the screen
 * edge in its direction when it is off screen.
 */
import type { ScreenPoint } from "../view/picking";

export interface NavTarget {
  id: string;
  label: string;
  x: number;
  z: number;
}

export interface NavMarker {
  id: string;
  label: string;
  /** Metres from the pilot. */
  distance: number;
  /** Where to draw the label, kept inside the screen by the insets. */
  screen: ScreenPoint;
  /**
   * Which point of the label sits on `screen`, as shares of its width and height: 0.5, 1 above an
   * on-screen target; toward the screen centre for one pinned to an edge, so it stays whole.
   */
  anchor: { x: number; y: number };
  onScreen: boolean;
  /** Direction from the screen centre toward the target, radians, for an edge arrow. */
  angle: number;
}

/** Pixels kept clear at each edge: room for the HUD lines. */
export interface Insets {
  top: number;
  right: number;
  bottom: number;
  left: number;
}

/** Places a marker for each target. `project` maps a point on the plane to the screen. */
export function placeMarkers(
  targets: readonly NavTarget[],
  from: { x: number; z: number },
  project: (x: number, z: number) => ScreenPoint,
  viewport: { width: number; height: number },
  insets: Insets,
): NavMarker[] {
  // The usable area, and its centre, which the edge markers point from.
  const left = insets.left;
  const right = Math.max(left + 1, viewport.width - insets.right);
  const top = insets.top;
  const bottom = Math.max(top + 1, viewport.height - insets.bottom);
  const cx = (left + right) / 2;
  const cy = (top + bottom) / 2;
  const halfW = (right - left) / 2;
  const halfH = (bottom - top) / 2;
  return targets.map((target) => {
    const point = project(target.x, target.z);
    const dx = point.x - cx;
    const dy = point.y - cy;
    const reach = Math.max(Math.abs(dx) / halfW, Math.abs(dy) / halfH);
    const onScreen = reach <= 1;
    const screen = onScreen ? point : { x: cx + dx / reach, y: cy + dy / reach };
    const anchor = onScreen
      ? { x: 0.5, y: 1 }
      : { x: (screen.x - left) / (right - left), y: (screen.y - top) / (bottom - top) };
    return {
      id: target.id,
      label: target.label,
      distance: Math.hypot(target.x - from.x, target.z - from.z),
      screen,
      anchor,
      onScreen,
      angle: Math.atan2(dy, dx),
    };
  });
}

/** A distance as the markers show it: metres up to a kilometre, then kilometres. */
export function distanceText(metres: number): string {
  return metres < 1_000 ? `${String(Math.round(metres))} m` : `${(metres / 1_000).toFixed(1)} km`;
}
