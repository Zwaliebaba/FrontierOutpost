/**
 * The waypoint: the one bearing cue on the scene. The legs are long (design §10: about three
 * minutes each way), so the destination chosen on the system map is marked where it is on
 * screen, or at the screen edge in its direction when it is off screen.
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

/** A label's box on screen, pixels from the top left. */
export interface Box {
  x: number;
  y: number;
  width: number;
  height: number;
}

/**
 * Moves the waypoint off the HUD's panels: it is the one bearing cue on the scene, so it must
 * never be drawn under the gauges or the minimap. Of the places just clear of a panel's edge, it
 * takes the nearest that is whole inside `bounds` and on no panel; with none, it stays put.
 */
export function clearOf(box: Box, blocked: readonly Box[], bounds: Box, gap = 6): Box {
  const overlaps = (a: Box, b: Box) =>
    a.x < b.x + b.width + gap &&
    b.x < a.x + a.width + gap &&
    a.y < b.y + b.height + gap &&
    b.y < a.y + a.height + gap;
  const free = (at: Box) =>
    at.x >= bounds.x &&
    at.y >= bounds.y &&
    at.x + at.width <= bounds.x + bounds.width &&
    at.y + at.height <= bounds.y + bounds.height &&
    !blocked.some((panel) => overlaps(at, panel));
  if (!blocked.some((panel) => overlaps(box, panel))) return box;
  const moved = (at: Box) => Math.abs(at.x - box.x) + Math.abs(at.y - box.y);
  // Above and below each panel, then beside it: a tie goes to the earlier, so the waypoint
  // keeps its place along a top or bottom edge when it can.
  const places = blocked
    .flatMap((panel) => [
      { ...box, y: panel.y - gap - box.height },
      { ...box, y: panel.y + panel.height + gap },
      { ...box, x: panel.x - gap - box.width },
      { ...box, x: panel.x + panel.width + gap },
    ])
    .filter(free)
    .sort((a, b) => moved(a) - moved(b));
  return places[0] ?? box;
}

/** A distance as the markers show it: metres up to a kilometre, then kilometres. */
export function distanceText(metres: number): string {
  return metres < 1_000 ? `${String(Math.round(metres))} m` : `${(metres / 1_000).toFixed(1)} km`;
}
