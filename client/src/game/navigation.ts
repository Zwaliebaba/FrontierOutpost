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

/** A label's box on screen, pixels from the top left. */
export interface Box {
  x: number;
  y: number;
  width: number;
  height: number;
}

/**
 * Moves overlapping labels apart, so that each stays whole and can be clicked to choose a jump
 * target. Markers pinned to the same stretch of edge, or targets close together on screen, would
 * otherwise stack. Each overlapping pair is pushed apart along the axis where it overlaps least,
 * half each way, for a few passes; boxes stay inside `bounds`.
 */
export function spreadBoxes(
  boxes: readonly Box[],
  bounds: { width: number; height: number },
  gap = 4,
  passes = 24,
): Box[] {
  const moved = boxes.map((box) => ({ ...box }));
  const keepIn = (box: Box) => {
    box.x = Math.min(Math.max(0, box.x), Math.max(0, bounds.width - box.width));
    box.y = Math.min(Math.max(0, box.y), Math.max(0, bounds.height - box.height));
  };
  for (let pass = 0; pass < passes; pass++) {
    let clear = true;
    for (let i = 0; i < moved.length; i++) {
      for (let j = i + 1; j < moved.length; j++) {
        const a = moved[i];
        const b = moved[j];
        if (!a || !b) continue;
        const overlapX = Math.min(a.x + a.width, b.x + b.width) + gap - Math.max(a.x, b.x);
        const overlapY = Math.min(a.y + a.height, b.y + b.height) + gap - Math.max(a.y, b.y);
        if (overlapX <= 0 || overlapY <= 0) continue;
        clear = false;
        const axis = overlapX < overlapY ? "x" : "y";
        const size = axis === "x" ? "width" : "height";
        const needed = Math.min(overlapX, overlapY);
        // a goes toward the lower side, b the higher; ties, as for two boxes in one place, by
        // list order.
        const away = a[axis] + a[size] / 2 <= b[axis] + b[size] / 2 ? 1 : -1;
        // Half each way; what one cannot move, held by an edge, the other moves instead.
        const aFrom = a[axis];
        a[axis] -= (away * needed) / 2;
        keepIn(a);
        const aMoved = Math.abs(a[axis] - aFrom);
        const bFrom = b[axis];
        b[axis] += away * (needed - aMoved);
        keepIn(b);
        const short = needed - aMoved - Math.abs(b[axis] - bFrom);
        if (short > 0) {
          a[axis] -= away * short;
          keepIn(a);
        }
      }
    }
    if (clear) break;
  }
  return moved;
}

/** A distance as the markers show it: metres up to a kilometre, then kilometres. */
export function distanceText(metres: number): string {
  return metres < 1_000 ? `${String(Math.round(metres))} m` : `${(metres / 1_000).toFixed(1)} km`;
}
