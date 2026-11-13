import { describe, expect, it } from "vitest";
import type { RunSummary } from "../protocol";
import { Receipts } from "./receipts";

const pending = (run: string): RunSummary => ({
  run,
  settled: false,
  pot: { contract: 0, bounty: 0, relay: 0 },
  ending: "docked",
  ore: 12,
  salvage: 0,
  volatiles: 0,
  payout: 0,
  launchCost: 40,
  cargoLost: 0,
  modulesLost: 0,
  approachSeconds: 10,
  objectiveSeconds: 60,
  returnSeconds: 10,
});

describe("expedition receipts", () => {
  it("settles an earlier expedition without changing the newer receipt", () => {
    const receipts = new Receipts();
    receipts.add(pending("first"));
    receipts.add(pending("second"));
    receipts.settle("first", { contract: 160, bounty: 0, relay: 80 }, 120);
    expect(receipts.all[0]).toEqual(pending("second"));
    expect(receipts.all[1]).toMatchObject({ run: "first", settled: true, payout: 120, ore: 12 });
  });

  it("distinguishes a final zero share from pending and ignores unrelated settlements", () => {
    const receipts = new Receipts();
    receipts.add(pending("first"));
    expect(receipts.settle("other", { contract: 100, bounty: 0, relay: 0 }, 100)).toBeUndefined();
    receipts.settle("first", { contract: 0, bounty: 0, relay: 0 }, 0);
    expect(receipts.all[0]).toMatchObject({ settled: true, payout: 0 });
    receipts.settle("first", { contract: 0, bounty: 0, relay: 0 }, 0);
    expect(receipts.all).toHaveLength(1);
  });
});

it("restores final receipts without carrying stale pending state across reloads", () => {
  const before = new Receipts();
  before.add(pending("finished"));
  before.add(pending("still-flying"));
  before.settle("finished", { contract: 160, bounty: 0, relay: 0 }, 80);
  const after = new Receipts();
  after.restore(before.serialize());
  expect(after.all).toHaveLength(1);
  expect(after.all[0]).toMatchObject({ run: "finished", settled: true, payout: 80 });
  after.restore("broken JSON");
  expect(after.all).toHaveLength(1);
});
