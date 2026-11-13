import { describe, expect, it } from "vitest";
import { SCAN_REFUSAL_TEXT } from "./scan";

describe("a refused scan", () => {
  it("says what to do about it", () => {
    expect(SCAN_REFUSAL_TEXT.moving).toContain("hold still");
    expect(SCAN_REFUSAL_TEXT.outOfRange).toContain("move closer");
    expect(SCAN_REFUSAL_TEXT.scanning).toContain("already");
    expect(SCAN_REFUSAL_TEXT.revealed).toContain("already");
  });

  it("names the scan in every reason, as the extractor's and the drive's notices do", () => {
    for (const text of Object.values(SCAN_REFUSAL_TEXT)) expect(text).toMatch(/^Scan: /);
  });
});
