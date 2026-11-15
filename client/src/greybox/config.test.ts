import { describe, expect, it } from "vitest";
import { DEFAULT_CONFIG, configToSearch, parseConfig, resultFileName } from "./config";

describe("greybox config", () => {
  it("defaults to the current tactical view on the station scene", () => {
    expect(parseConfig("")).toEqual(DEFAULT_CONFIG);
    expect(DEFAULT_CONFIG.elevation).toBe(33);
    expect(DEFAULT_CONFIG.scene).toBe("station");
    expect(DEFAULT_CONFIG.projection).toBe("ortho");
    expect(DEFAULT_CONFIG.backdrop).toBe("stars");
    expect(DEFAULT_CONFIG.cues).toBe(false);
    expect(parseConfig("?cues=1").cues).toBe(true);
    expect(parseConfig("?backdrop=dark").backdrop).toBe("dark");
  });

  it("lets the tester look around by default without moving the resting view", () => {
    expect(DEFAULT_CONFIG.look).toBe("hold");
    expect(parseConfig("?look=fixed").look).toBe("fixed");
    expect(parseConfig("?look=sideways&projection=fisheye")).toEqual(DEFAULT_CONFIG);
  });

  it("round-trips every field through the query string", () => {
    const config = {
      scene: "rock",
      scale: 3,
      facing: "away",
      elevation: 45,
      projection: "wide",
      look: "free",
      width: 73,
      cues: true,
      occlusion: "fade",
      heights: "varied",
      vision: "deutan",
      backdrop: "bright",
      seed: 4,
      trials: 12,
    } as const;
    expect(parseConfig(configToSearch(config))).toEqual(config);
  });

  it("puts the M9 ring at its authored size at the dock, mouth toward the site", () => {
    expect(DEFAULT_CONFIG.scale).toBe(2.2);
    expect(DEFAULT_CONFIG.facing).toBe("site");
    expect(parseConfig("?scale=1").scale).toBe(1);
    expect(parseConfig("?scale=3&facing=camera")).toMatchObject({ scale: 3, facing: "camera" });
    expect(parseConfig("?scale=2&facing=up")).toMatchObject({ scale: 2.2, facing: "site" });
  });

  it("names each download after its scene and, in the station scene, its station", () => {
    const named = (search: string) => resultFileName(parseConfig(search));
    expect(named("")).toBe("greybox-station-2.2x-site-33deg.json");
    expect(named("?scale=3&facing=away&elevation=40")).toBe("greybox-station-3x-away-40deg.json");
    // The Stage 1 station has no mouth to face.
    expect(named("?scale=1&facing=side")).toBe("greybox-station-1x-33deg.json");
    expect(named("?scene=rock&scale=3")).toBe("greybox-rock-33deg.json");
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
