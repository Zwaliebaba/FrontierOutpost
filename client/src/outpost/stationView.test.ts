import { describe, expect, it } from "vitest";
import { HOME_VIEW, leaveInterior, openInterior, openTab, viewControl } from "./stationView";

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
