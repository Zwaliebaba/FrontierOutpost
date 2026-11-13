import { describe, expect, it } from "vitest";
import { MOST_NOTICES, Notices, noticeMs } from "./notices";

describe("noticeMs", () => {
  it("gives a long notice longer, within limits", () => {
    expect(noticeMs("Jump cancelled")).toBe(4_000);
    expect(noticeMs("x".repeat(60))).toBe(4_000 + 20 * 55);
    expect(noticeMs("x".repeat(400))).toBe(9_000);
  });
});

describe("Notices", () => {
  it("shows a notice until its time is up", () => {
    const notices = new Notices();
    notices.push("Jump cancelled", "info", 1_000);
    expect(notices.visible(1_000).map((notice) => notice.text)).toEqual(["Jump cancelled"]);
    expect(notices.visible(4_999)).toHaveLength(1);
    expect(notices.visible(5_000)).toHaveLength(0);
  });

  it("keeps an earlier notice beside a later one, with their levels", () => {
    const notices = new Notices();
    notices.push("The crew's pot is paid", "good", 0);
    notices.push("Extractor: the hold is full", "warn", 1_000);
    expect(notices.visible(1_500).map(({ text, level }) => [text, level])).toEqual([
      ["The crew's pot is paid", "good"],
      ["Extractor: the hold is full", "warn"],
    ]);
    // The first goes when its own time is up; the second stays.
    expect(notices.visible(4_500).map((notice) => notice.text)).toEqual([
      "Extractor: the hold is full",
    ]);
  });

  it("replaces a notice said again instead of repeating it", () => {
    const notices = new Notices();
    notices.push("No hostile within lock range", "warn", 0);
    notices.push("Jump cancelled", "info", 500);
    notices.push("No hostile within lock range", "warn", 3_000);
    const shown = notices.visible(3_000);
    expect(shown.map((notice) => notice.text)).toEqual([
      "Jump cancelled",
      "No hostile within lock range",
    ]);
    // Said again, it is a new notice with a new time.
    expect(shown[1]?.until).toBe(7_000);
    expect(shown[1]?.id).toBeGreaterThan(shown[0]?.id ?? 0);
  });

  it("shows only the newest few", () => {
    const notices = new Notices();
    for (const text of ["one", "two", "three", "four"]) notices.push(text, "info", 0);
    const shown = notices.visible(0).map((notice) => notice.text);
    expect(shown).toHaveLength(MOST_NOTICES);
    expect(shown).toEqual(["two", "three", "four"]);
  });
});
