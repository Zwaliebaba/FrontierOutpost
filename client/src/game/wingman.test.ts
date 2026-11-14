import { describe, expect, it } from "vitest";
import type { AccountInfo, Catalog, WingmanInfo } from "../protocol";
import { wingmanChoices, wingmanFinished, wingmanText } from "./wingman";

const catalog = {
  fittings: [
    {
      id: "extraction_rig",
      name: "Extraction rig",
      slot: "rig",
      needs: "Extraction rig",
      price: 60,
      effect: "hold",
    },
  ],
} as unknown as Catalog;
const account = {
  outpost: { sockets: [{ facility: "crew_quarters", run: 2 }], opens: ["Extraction rig"] },
  hangar: ["extraction_rig"],
  launch: { flown: ["extraction_rig"] },
} as unknown as AccountInfo;

describe("wingman preparation and readings", () => {
  it("offers only spare stock rigs and requires powered quarters", () => {
    expect(wingmanChoices(account, catalog, null)).toEqual({ tier: 2, canHire: true, rigs: [] });
    const spare = { ...account, hangar: [...account.hangar, "extraction_rig"] };
    expect(wingmanChoices(spare, catalog, null).rigs).toHaveLength(1);
    expect(
      wingmanChoices({ ...account, outpost: { ...account.outpost, sockets: [] } }, catalog, null)
        .canHire,
    ).toBe(false);
  });

  it("shows authoritative order, activity and cargo, including a loss", () => {
    const info: WingmanInfo = {
      shipId: 2,
      order: { type: "hold", x: 20, z: 20 },
      state: "holding",
      cargo: 3,
      capacity: 24,
    };
    expect(wingmanText(info)).toBe("Wingman · hold · holding · cargo 3/24");
    expect(wingmanFinished(info)).toBe(false);
    expect(wingmanFinished({ ...info, state: "destroyed" })).toBe(true);
  });
});
