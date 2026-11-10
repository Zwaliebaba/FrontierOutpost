import { describe, expect, it } from "vitest";
import { DEFAULT_CONFIG, configToSearch, parseConfig } from "./config";

describe("greybox config", () => {
  it("defaults to the current tactical view on the station scene", () => {
    expect(parseConfig("")).toEqual(DEFAULT_CONFIG);
    expect(DEFAULT_CONFIG.elevation).toBe(33);
    expect(DEFAULT_CONFIG.scene).toBe("station");
  });

  it("round-trips every field through the query string", () => {
    const config = {
      scene: "rock",
      elevation: 45,
      width: 73,
      cues: false,
      occlusion: "fade",
      heights: "varied",
      vision: "deutan",
      backdrop: "bright",
      seed: 4,
      trials: 12,
    } as const;
    expect(parseConfig(configToSearch(config))).toEqual(config);
  });

  it("falls back to defaults for unknown or out-of-range values", () => {
    const config = parseConfig("?scene=moon&elevation=90&width=1&cues=maybe&seed=-5&trials=999");
    expect(config.scene).toBe(DEFAULT_CONFIG.scene);
    expect(config.elevation).toBe(DEFAULT_CONFIG.elevation);
    expect(config.width).toBe(DEFAULT_CONFIG.width);
    expect(config.cues).toBe(DEFAULT_CONFIG.cues);
    expect(config.seed).toBe(0);
    expect(config.trials).toBe(40);
  });
});
