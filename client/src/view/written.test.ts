import { describe, expect, it } from "vitest";

import { Written } from "./written";

describe("written", () => {
  it("reports a value once, until it changes", () => {
    const written = new Written();
    expect(written.changed("hud", "hull 100%")).toBe(true);
    expect(written.changed("hud", "hull 100%")).toBe(false);
    expect(written.changed("hud", "hull 90%")).toBe(true);
    expect(written.changed("hud", "hull 100%")).toBe(true);
  });

  it("keeps each place apart", () => {
    const written = new Written();
    expect(written.changed("hud", "")).toBe(true);
    expect(written.changed("run", "")).toBe(true);
    expect(written.changed("hud", "")).toBe(false);
  });

  it("writes again to places it was told to forget", () => {
    const written = new Written();
    written.changed("nav.dock.text", "Dock · 40 m");
    written.changed("nav.oreField.text", "Ore field · 90 m");
    written.changed("hud", "x");
    written.forget("nav.");
    // New nodes for the next run's markers start empty, so the same text must be written.
    expect(written.changed("nav.dock.text", "Dock · 40 m")).toBe(true);
    expect(written.changed("nav.oreField.text", "Ore field · 90 m")).toBe(true);
    expect(written.changed("hud", "x")).toBe(false);
  });
});
