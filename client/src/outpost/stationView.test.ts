import { describe, expect, it } from "vitest";
import {
  HOME_VIEW,
  STATION_FACTS,
  leaveInterior,
  openInterior,
  openTab,
  placeLine,
  viewControl,
} from "./stationView";

describe("the station's two views", () => {
  it("opens on the exterior and the overview", () => {
    expect(HOME_VIEW).toEqual({ view: "exterior", tab: "overview" });
  });

  it("opens the interior with the facilities service, which builds there", () => {
    expect(openInterior()).toEqual({ view: "interior", tab: "facilities" });
    expect(openTab(HOME_VIEW, "facilities")).toEqual({ view: "interior", tab: "facilities" });
  });

  it("leaves the other services in whichever view is drawn", () => {
    expect(openTab(HOME_VIEW, "market")).toEqual({ view: "exterior", tab: "market" });
    expect(openTab(openInterior(), "processor")).toEqual({ view: "interior", tab: "processor" });
  });

  it("closes the facilities service with the interior, so building happens only inside", () => {
    expect(leaveInterior(openInterior())).toEqual({ view: "exterior", tab: "overview" });
    expect(leaveInterior({ view: "interior", tab: "hangar" })).toEqual({
      view: "exterior",
      tab: "hangar",
    });
  });

  it("says which view is drawn and how to reach the other", () => {
    expect(viewControl("exterior")).toEqual({
      caption: "STATION / EXTERIOR",
      action: "interior",
      label: "Open interior",
      hint: "Select the station to open it",
    });
    expect(viewControl("interior")).toMatchObject({ action: "exterior", label: "Exterior" });
    expect(viewControl("interior").hint).toContain("Esc");
  });
});

describe("the station's text without berths", () => {
  it("counts the crew at home, not berths, and names the run in flight", () => {
    expect(placeLine(false, 1)).toBe("HOME OUTPOST / HANGAR");
    expect(placeLine(false, 3)).toBe("HOME OUTPOST / CREW 3 OF 4");
    expect(placeLine(true, 3)).toBe("EXPEDITION / ACTIVE");
    for (const text of [placeLine(false, 4), STATION_FACTS]) expect(text).not.toMatch(/berth/i);
  });
});
