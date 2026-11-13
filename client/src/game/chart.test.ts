import { describe, expect, it } from "vitest";
import { Vector3 } from "three";
import type { SiteInfo } from "../protocol";
import { RESTING_ELEVATION } from "./render";
import { aimRig, createRig } from "../view/view";
import { worldToScreen } from "../view/picking";
import {
  chartBearing,
  chartPlaces,
  fitChart,
  jumpReadiness,
  keepInFrame,
  plot,
  scaleBar,
  sideOf,
  toChart,
  waypointFor,
} from "./chart";

const site: SiteInfo = {
  name: "Kessler Drift (compact)",
  dockX: -120,
  dockZ: 0,
  centreX: 70,
  centreZ: 0,
  radius: 125,
  dockRadius: 12,
  pockets: [
    { kind: "oreField", x: 45, z: -10, radius: 36, locked: false, material: "ore" },
    { kind: "derelict", x: 115, z: 65, radius: 24, locked: true, material: "salvage" },
  ],
};

describe("toChart", () => {
  it("draws the plane as the resting camera shows it: same right, same up", () => {
    const rig = createRig(4 / 3, 400, "ortho");
    aimRig(rig, new Vector3(0, 0, 0), RESTING_ELEVATION);
    const viewport = { width: 800, height: 600 };
    const origin = worldToScreen(new Vector3(0, 0, 0), rig.camera, viewport);
    for (const [x, z] of [
      [100, 0],
      [0, 100],
      [-60, 80],
    ] as const) {
      const screen = worldToScreen(new Vector3(x, 0, z), rig.camera, viewport);
      const chart = toChart(x, z);
      // The camera's elevation shortens the screen's up; the directions agree.
      expect(Math.sign(chart.x)).toBe(Math.sign(Math.round(screen.x - origin.x)));
      expect(Math.sign(chart.y)).toBe(Math.sign(Math.round(screen.y - origin.y)));
    }
  });

  it("keeps distances: the chart is the plane seen from straight above", () => {
    const at = toChart(30, -40);
    expect(Math.hypot(at.x, at.y)).toBeCloseTo(50);
  });
});

describe("chartBearing", () => {
  it("turns a ship's arrow the way the ship flies on the chart", () => {
    for (const heading of [0, 1, 2.5, 4]) {
      const along = toChart(Math.cos(heading), Math.sin(heading));
      const bearing = (chartBearing(heading) * Math.PI) / 180;
      expect(Math.sin(bearing)).toBeCloseTo(along.x);
      expect(-Math.cos(bearing)).toBeCloseTo(along.y);
    }
  });
});

describe("fitChart and plot", () => {
  it("shows the dock, the site and every pocket inside the padding", () => {
    const frame = fitChart(site, 800, 600, 40);
    const edges = [
      { x: site.dockX, z: site.dockZ, radius: site.dockRadius },
      { x: site.centreX, z: site.centreZ, radius: site.radius },
      ...site.pockets,
    ];
    for (const circle of edges) {
      const at = plot(frame, circle.x, circle.z);
      const reach = circle.radius * frame.scale;
      expect(at.x - reach).toBeGreaterThanOrEqual(40 - 1e-6);
      expect(at.x + reach).toBeLessThanOrEqual(760 + 1e-6);
      expect(at.y - reach).toBeGreaterThanOrEqual(40 - 1e-6);
      expect(at.y + reach).toBeLessThanOrEqual(560 + 1e-6);
    }
  });

  it("frames the site alone when asked, leaving a far dock off the chart", () => {
    // The Stage 1 site's proportions: the dock is four site radii from the site's centre.
    const far: SiteInfo = { ...site, dockX: -900, centreX: 620, radius: 360, pockets: [] };
    const whole = fitChart(far, 244, 108, 14);
    const alone = fitChart(far, 244, 108, 14, false);
    expect(alone.scale).toBeGreaterThan(whole.scale * 1.5);
    // The site's circle fills the height; the dock falls outside the frame.
    expect(plot(alone, far.centreX, far.centreZ).x).toBeCloseTo(122);
    expect(plot(alone, far.centreX, far.centreZ).y).toBeCloseTo(54);
    expect(far.radius * alone.scale).toBeCloseTo(40);
    expect(plot(alone, far.dockX, far.dockZ).x).toBeLessThan(0);
  });

  it("fills the frame along the axis that binds", () => {
    const frame = fitChart(site, 800, 600, 40);
    // The least room left at any edge is none: a larger scale would overflow the padding.
    const slack = [
      { x: site.dockX, z: site.dockZ, radius: site.dockRadius },
      { x: site.centreX, z: site.centreZ, radius: site.radius },
    ].flatMap((circle) => {
      const at = plot(frame, circle.x, circle.z);
      const reach = circle.radius * frame.scale;
      return [at.x - reach - 40, 760 - at.x - reach, at.y - reach - 40, 560 - at.y - reach];
    });
    expect(Math.min(...slack)).toBeCloseTo(0);
  });

  it("holds a ship that has left the chart at its edge", () => {
    const frame = fitChart(site, 200, 150, 10);
    expect(keepInFrame(frame, { x: -40, y: 400 }, 8)).toEqual({ x: 8, y: 142 });
    expect(keepInFrame(frame, { x: 90, y: 70 }, 8)).toEqual({ x: 90, y: 70 });
  });
});

describe("scaleBar", () => {
  it("picks a round length that fits", () => {
    const frame = { width: 800, height: 600, scale: 0.4, centre: { x: 0, y: 0 } };
    // 120 px is 300 m: the longest of 1, 2, 5 × 10ⁿ that fits is 200 m.
    expect(scaleBar(frame, 120)).toEqual({ metres: 200, pixels: 80 });
    expect(scaleBar({ ...frame, scale: 2.2 }, 120).metres).toBe(50);
  });
});

describe("chartPlaces", () => {
  it("lists the dock first, then each pocket, numbered for the keyboard", () => {
    const places = chartPlaces(site, { x: -120, z: 0 }, null, false);
    expect(places.map((place) => [place.key, place.id, place.name])).toEqual([
      ["1", "dock", "Dock"],
      ["2", "oreField", "Ore field"],
      ["3", "derelict", "Derelict"],
    ]);
    expect(places[0]?.distance).toBe(0);
    expect(places[1]?.distance).toBeCloseTo(Math.hypot(165, 10));
  });

  it("says what each pocket yields, or what would open it", () => {
    const [, ore, derelict] = chartPlaces(site, { x: 0, z: 0 }, null, false);
    expect(ore?.status).toBe("Open · yields ore");
    expect(derelict?.locked).toBe(true);
    expect(derelict?.status).toBe("Locked · needs a sensor array");
  });

  it("marks what a scan found", () => {
    const [dock, ore, derelict] = chartPlaces(
      site,
      { x: 0, z: 0 },
      { richPocket: 1, relays: [0, 1] },
      false,
    );
    expect(dock?.tags).toEqual([]);
    expect(ore?.tags).toEqual(["relay"]);
    expect(derelict?.tags).toEqual(["rich", "relay"]);
  });

  it("warns that a laden jump home arrives short of the dock", () => {
    const empty = chartPlaces(site, { x: 0, z: 0 }, null, false)[0];
    const laden = chartPlaces(site, { x: 0, z: 0 }, null, true)[0];
    expect(empty?.arrival).toContain("docking is flown");
    expect(laden?.arrival).toContain("interceptors");
  });
});

describe("waypointFor", () => {
  it("marks the destination chosen, and nothing when none is", () => {
    const places = chartPlaces(site, { x: -120, z: 0 }, null, false);
    expect(waypointFor(places, null)).toBeUndefined();
    expect(waypointFor(places, "oreField")?.id).toBe("oreField");
    expect(waypointFor(places, "nestCore")).toBeUndefined();
  });

  it("drops the mark once the ship is inside the place", () => {
    // The ore field is 36 m across its radius; the dock, 12.
    expect(
      waypointFor(chartPlaces(site, { x: 45, z: 20 }, null, false), "oreField"),
    ).toBeUndefined();
    expect(waypointFor(chartPlaces(site, { x: 45, z: 30 }, null, false), "oreField")?.id).toBe(
      "oreField",
    );
    expect(waypointFor(chartPlaces(site, { x: -115, z: 0 }, null, false), "dock")).toBeUndefined();
  });
});

describe("jumpReadiness", () => {
  const [dock, ore, derelict] = chartPlaces(site, { x: 0, z: 0 }, null, false);

  it("asks for a destination first", () => {
    expect(jumpReadiness(null, "ready")).toEqual({ ready: false, reason: "Choose a destination" });
  });

  it("is ready for the dock or an open pocket, in the site or out", () => {
    expect(jumpReadiness(dock ?? null, "ready").ready).toBe(true);
    expect(jumpReadiness(ore ?? null, "inSite").ready).toBe(true);
    expect(jumpReadiness(ore ?? null, null).ready).toBe(true);
  });

  it("says why a locked pocket, a hostile lock or the extractor stops it", () => {
    expect(jumpReadiness(derelict ?? null, "ready")).toEqual({
      ready: false,
      reason: "Locked: needs a sensor array",
    });
    expect(jumpReadiness(ore ?? null, "hunted").reason).toContain("hostile");
    expect(jumpReadiness(ore ?? null, "extracting").reason).toContain("(X)");
  });
});

describe("sideOf", () => {
  it("tells the pilot's own ship from the crew's and from hostiles", () => {
    expect(sideOf("playerShip", 4, 4)).toBe("own");
    expect(sideOf("playerShip", 5, 4)).toBe("crew");
    expect(sideOf("drone", 6, 4)).toBe("hostile");
    expect(sideOf("core", 7, 4)).toBe("hostile");
  });
});
