import { describe, expect, it } from "vitest";
import type { AccountInfo } from "../protocol";
import { countdown, pace, processorView, progressLine } from "./processor";

/** Only what the processor reads of an account. */
function home(
  batchSeconds: number,
  processing: AccountInfo["processing"],
  working?: AccountInfo["working"],
): AccountInfo {
  return {
    outpost: { batchSeconds },
    processing,
    ...(working ? { working } : {}),
  } as AccountInfo;
}

describe("processorView", () => {
  it("is idle with nothing in hand", () => {
    const view = processorView(home(240, []), 30);
    expect(view).toEqual({ batches: 0, share: 0, left: 0, allLeft: 0 });
    expect(progressLine(view)).toBe("");
  });

  it("counts on from where the server said the batch in hand was", () => {
    const account = home(
      240,
      [
        { recipe: "alloy", batches: 2 },
        { recipe: "components", batches: 1 },
      ],
      { recipe: "alloy", elapsedSeconds: 60 },
    );
    const view = processorView(account, 19);
    expect(view.recipe).toBe("alloy");
    expect(view.batches).toBe(3);
    expect(view.share).toBeCloseTo(79 / 240);
    expect(view.left).toBe(161);
    expect(view.allLeft).toBe(161 + 2 * 240);
    expect(progressLine(view)).toBe("2:41 left on this batch · all 3 done in 10:41");
  });

  it("stops at the batch's end and waits for the server to finish it", () => {
    const account = home(96, [{ recipe: "alloy", batches: 1 }], {
      recipe: "alloy",
      elapsedSeconds: 90,
    });
    expect(progressLine(processorView(account, 2))).toBe("0:04 left on this batch");
    const over = processorView(account, 500);
    expect(over.share).toBe(1);
    expect(over.left).toBe(0);
    expect(progressLine(over)).toBe("Finishing…");
  });
});

describe("times", () => {
  it("shows time left as a clock, rounded up so it never reads 0:00 early", () => {
    expect(countdown(160.2)).toBe("2:41");
    expect(countdown(0.4)).toBe("0:01");
    expect(countdown(0)).toBe("0:00");
    expect(countdown(3_761)).toBe("1:02:41");
  });

  it("says a batch's time in words", () => {
    expect(pace(240)).toBe("4 min");
    expect(pace(96)).toBe("1 min 36 s");
    expect(pace(48)).toBe("48 s");
    expect(pace(0)).toBe("0 s");
  });
});
