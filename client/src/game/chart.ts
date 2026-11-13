/**
 * The system map as data: where the dock, the pockets and the ships sit on a chart of the site,
 * what each destination is, and whether the jump drive can take the ship there. The map is where
 * a pilot chooses a jump destination (design §2), so the scene itself carries no labels.
 * Kept free of the DOM so it can be tested.
 */
import { tacticalViewDirection } from "../camera";
import type { Drive, EntityKind, PocketKind, RunPhase, SiteInfo } from "../protocol";
import { POCKET_NAME, POCKET_NEEDS } from "./contract";
import type { Scanned } from "./contract";
import { DOCK_MARKER } from "./jump";

export interface ChartPoint {
  x: number;
  y: number;
}

// Up on the chart is up on the screen with the camera at rest: along the plane, away from the
// camera. A pilot then steers by the chart as they steer by the scene.
const toCamera = tacticalViewDirection();
const planar = Math.hypot(toCamera.x, toCamera.z);
const UP = { x: -toCamera.x / planar, z: -toCamera.z / planar };
const RIGHT = { x: -UP.z, z: UP.x };

/** A point on the plane in chart axes, metres: x to the right, y down. */
export function toChart(x: number, z: number): ChartPoint {
  return { x: x * RIGHT.x + z * RIGHT.z, y: -(x * UP.x + z * UP.z) };
}

/** A heading on the plane as degrees clockwise from straight up the chart. */
export function chartBearing(heading: number): number {
  const along = toChart(Math.cos(heading), Math.sin(heading));
  return (Math.atan2(along.x, -along.y) * 180) / Math.PI;
}

/** How a chart of `width` by `height` pixels shows the plane. */
export interface ChartFrame {
  width: number;
  height: number;
  /** Pixels per metre. */
  scale: number;
  /** The point, in chart axes, drawn at the middle of the frame. */
  centre: ChartPoint;
}

/**
 * The frame that shows the whole site, dock to farthest pocket, with `padding` pixels clear.
 * Without the dock it frames the site alone: the legs are long, and a small drawing that showed
 * the dock too would crowd the pockets into a corner.
 */
export function fitChart(
  site: SiteInfo,
  width: number,
  height: number,
  padding: number,
  withDock = true,
): ChartFrame {
  const circles = [
    ...(withDock ? [{ x: site.dockX, z: site.dockZ, radius: site.dockRadius }] : []),
    { x: site.centreX, z: site.centreZ, radius: site.radius },
    ...site.pockets,
  ];
  let left = Infinity;
  let right = -Infinity;
  let top = Infinity;
  let bottom = -Infinity;
  for (const circle of circles) {
    const at = toChart(circle.x, circle.z);
    left = Math.min(left, at.x - circle.radius);
    right = Math.max(right, at.x + circle.radius);
    top = Math.min(top, at.y - circle.radius);
    bottom = Math.max(bottom, at.y + circle.radius);
  }
  const scale = Math.min(
    (width - 2 * padding) / Math.max(1, right - left),
    (height - 2 * padding) / Math.max(1, bottom - top),
  );
  return { width, height, scale, centre: { x: (left + right) / 2, y: (top + bottom) / 2 } };
}

/** Where a point on the plane is drawn in `frame`, pixels from its top left. */
export function plot(frame: ChartFrame, x: number, z: number): ChartPoint {
  const at = toChart(x, z);
  return {
    x: frame.width / 2 + (at.x - frame.centre.x) * frame.scale,
    y: frame.height / 2 + (at.y - frame.centre.y) * frame.scale,
  };
}

/** `point` held at least `margin` pixels inside `frame`: a ship off the chart shows at its edge. */
export function keepInFrame(frame: ChartFrame, point: ChartPoint, margin: number): ChartPoint {
  return {
    x: Math.min(Math.max(margin, point.x), frame.width - margin),
    y: Math.min(Math.max(margin, point.y), frame.height - margin),
  };
}

/** A round length for a scale bar no longer than `maxPixels`: 1, 2 or 5 times a power of ten. */
export function scaleBar(frame: ChartFrame, maxPixels: number): { metres: number; pixels: number } {
  const most = maxPixels / frame.scale;
  const power = 10 ** Math.floor(Math.log10(most));
  const metres = ([5, 2, 1].find((step) => step * power <= most) ?? 1) * power;
  return { metres, pixels: metres * frame.scale };
}

export type PlaceKind = "dock" | PocketKind;

/** Somewhere the jump drive can go, as the map lists it. */
export interface ChartPlace {
  /** The jump drive's name for it: `dock`, or the pocket's kind. */
  id: string;
  /** The digit that chooses it while the map is open. */
  key: string;
  kind: PlaceKind;
  name: string;
  x: number;
  z: number;
  radius: number;
  /** Whether the crew lacks the facility it needs. */
  locked: boolean;
  /** What it is, in a line. */
  status: string;
  /** What a scan found there, `rich` or `relay`, and `new` for a pocket opened since the last run. */
  tags: string[];
  /** Where a jump there arrives (design §2), so the pilot knows before committing. */
  arrival: string;
  /** Metres from the pilot. */
  distance: number;
}

/**
 * The dock and the site's pockets, in the order the map lists them. `laden` is whether the ship
 * carries cargo: a jump home then arrives short of the dock.
 */
export function chartPlaces(
  site: SiteInfo,
  from: { x: number; z: number },
  reveal: Pick<Scanned, "richPocket" | "relays"> | null,
  laden: boolean,
  fresh: readonly string[] = [],
): ChartPlace[] {
  const distance = (x: number, z: number) => Math.hypot(x - from.x, z - from.z);
  return [
    {
      id: DOCK_MARKER,
      key: "1",
      kind: "dock",
      name: "Dock",
      x: site.dockX,
      z: site.dockZ,
      radius: site.dockRadius,
      locked: false,
      status: "Home berth · banks the hold",
      tags: [],
      arrival: laden
        ? "Short of the dock, outside the site: the interceptors stand between"
        : "Just off the berth: docking is flown",
      distance: distance(site.dockX, site.dockZ),
    },
    ...site.pockets.map((pocket, index): ChartPlace => {
      const needs = POCKET_NEEDS[pocket.kind] ?? "a facility";
      return {
        id: pocket.kind,
        key: String(index + 2),
        kind: pocket.kind,
        name: POCKET_NAME[pocket.kind],
        x: pocket.x,
        z: pocket.z,
        radius: pocket.radius,
        locked: pocket.locked,
        status: pocket.locked
          ? `Locked · needs ${needs}`
          : pocket.material
            ? `Open · yields ${pocket.material}`
            : "Open",
        tags: [
          ...(!pocket.locked && fresh.includes(pocket.kind) ? ["new"] : []),
          ...(reveal?.richPocket === index ? ["rich"] : []),
          ...(reveal?.relays.includes(index) ? ["relay"] : []),
        ],
        arrival: "Outside its edge, facing it",
        distance: distance(pocket.x, pocket.z),
      };
    }),
  ];
}

/**
 * The place the scene's waypoint marks: the destination chosen, until the ship is inside it.
 * There the place is all around the ship, and a label for it would only cover the fight.
 */
export function waypointFor(
  places: readonly ChartPlace[],
  selected: string | null,
): ChartPlace | undefined {
  const chosen = places.find((place) => place.id === selected);
  return chosen && chosen.distance > chosen.radius ? chosen : undefined;
}

/** Whether the jump drive would spool for a destination, and the reason when it would not. */
export interface JumpReadiness {
  ready: boolean;
  /** Why not, or empty. */
  reason: string;
}

/**
 * What the pilot can tell before asking: the server judges again (frontier_sim::jump), and its
 * refusal still shows. `drive` is the cruise drive's state, which the same hostile lock and
 * working extractor stop. `phase` is the pilot's: a jump is made from open space, so not until
 * the ship has flown clear of the dock.
 */
export function jumpReadiness(
  place: ChartPlace | null,
  drive: Drive | null,
  phase: RunPhase | null = null,
): JumpReadiness {
  if (!place) return { ready: false, reason: "Choose a destination" };
  if (place.locked) return { ready: false, reason: place.status.replace(" · ", ": ") };
  if (phase === "atDock" && place.kind !== "dock") {
    return { ready: false, reason: "Fly clear of the dock first" };
  }
  if (drive === "hunted") {
    return { ready: false, reason: "Drive jammed: a hostile has you locked" };
  }
  if (drive === "extracting") return { ready: false, reason: "Stop the extractor first (X)" };
  return { ready: true, reason: "" };
}

/** Whose a ship on the chart is. Shown by shape as well as colour (design §5). */
export type ContactSide = "own" | "crew" | "hostile";

export function sideOf(kind: EntityKind, id: number, ownId: number): ContactSide {
  if (id === ownId) return "own";
  return kind === "playerShip" ? "crew" : "hostile";
}
