import { describe, expect, it } from "vitest";
import type { SiteInfo } from "../protocol";
import {
  facilityPockets,
  freshPockets,
  openLine,
  openedBy,
  parseSeen,
  seenAfter,
  unlockNotice,
} from "./unlocks";

const site: Pick<SiteInfo, "pockets"> = {
  pockets: [
    { kind: "oreField", x: 0, z: 0, radius: 30, locked: false, material: "ore" },
    { kind: "derelict", x: 50, z: 0, radius: 20, locked: false, material: "salvage" },
    { kind: "volatileVeins", x: 0, z: 50, radius: 20, locked: true, material: "volatiles" },
  ],
};

describe("unlocks", () => {
  it("names the facility that opens each pocket that needs one", () => {
    expect(openedBy("derelict")).toBe("sensor array");
    expect(openedBy("volatileVeins")).toBe("refinery");
    expect(openedBy("nestCore")).toBe("fabrication bay");
    expect(openedBy("oreField")).toBeNull();
  });

  it("counts only open pockets a facility opened", () => {
    expect(facilityPockets(site)).toEqual(["derelict"]);
  });

  it("calls a pocket new until a run has flown with it open", () => {
    expect(freshPockets(site, null)).toEqual(["derelict"]);
    expect(freshPockets(site, [])).toEqual(["derelict"]);
    const after = parseSeen(seenAfter(site, null));
    expect(after).toEqual(["derelict"]);
    expect(freshPockets(site, after)).toEqual([]);
  });

  it("keeps what was seen before when a later run adds more", () => {
    const refinery = {
      pockets: site.pockets.map((pocket) => ({ ...pocket, locked: false })),
    };
    expect(freshPockets(refinery, ["derelict"])).toEqual(["volatileVeins"]);
    expect(parseSeen(seenAfter(refinery, ["derelict"]))).toEqual(["derelict", "volatileVeins"]);
  });

  it("reads a damaged store as nothing stored", () => {
    expect(parseSeen(null)).toBeNull();
    expect(parseSeen("not json")).toBeNull();
    expect(parseSeen('{"a":1}')).toBeNull();
    expect(parseSeen('["derelict","bogus"]')).toEqual(["derelict"]);
  });

  it("says in the briefing what opened a pocket", () => {
    expect(openLine("derelict")).toBe("Derelict: open · your sensor array opened it");
    expect(openLine("oreField")).toBe("Ore field: open");
  });

  it("announces new pockets as the run begins, and stays quiet with none", () => {
    expect(unlockNotice(site, ["derelict"])).toBe(
      "New pocket open: Derelict (sensor array, yields salvage) · on the map (M)",
    );
    expect(unlockNotice(site, [])).toBeNull();
  });
});
