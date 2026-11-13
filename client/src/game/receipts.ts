import { PROTOCOL_VERSION } from "../protocol";
import type { PotInfo, RunSummary } from "../protocol";

/** A late crew settlement belongs to its expedition, even after another launch. */
export class Receipts {
  readonly #runs = new Map<string, RunSummary>();

  add(summary: RunSummary): void {
    this.#runs.set(summary.run, summary);
    if (this.#runs.size > 32) {
      const oldest = this.#runs.keys().next().value;
      if (oldest !== undefined) this.#runs.delete(oldest);
    }
  }

  settle(run: string, pot: PotInfo, share: number): RunSummary | undefined {
    const before = this.#runs.get(run);
    if (!before) return undefined;
    const summary = { ...before, settled: true, pot, payout: share };
    this.#runs.set(run, summary);
    return summary;
  }

  /** Persist only final receipts: an offline pending payment must never masquerade as current. */
  serialize(): string {
    return JSON.stringify({
      version: PROTOCOL_VERSION,
      receipts: this.all.filter((receipt) => receipt.settled),
    });
  }

  restore(text: string | null): void {
    if (!text) return;
    try {
      const data: unknown = JSON.parse(text);
      if (
        typeof data !== "object" ||
        data === null ||
        !("version" in data) ||
        data.version !== PROTOCOL_VERSION ||
        !("receipts" in data) ||
        !Array.isArray(data.receipts)
      )
        return;
      const saved: unknown[] = data.receipts;
      for (const receipt of [...saved].reverse().slice(-32)) {
        if (finalReceipt(receipt)) this.add(receipt);
      }
    } catch {
      /* Browser storage is optional; invalid cache never affects gameplay. */
    }
  }

  get all(): RunSummary[] {
    return [...this.#runs.values()].reverse();
  }
}

function finalReceipt(value: unknown): value is RunSummary {
  if (typeof value !== "object" || value === null) return false;
  const row = value as Record<string, unknown>;
  const number = (n: unknown) =>
    typeof n === "number" && Number.isInteger(n) && n >= 0 && n <= 0xffffffff;
  const pot = row.pot;
  return (
    typeof row.run === "string" &&
    row.run.length > 0 &&
    row.run.length < 128 &&
    row.settled === true &&
    ["docked", "destroyed", "abandoned"].includes(String(row.ending)) &&
    [
      "ore",
      "salvage",
      "volatiles",
      "payout",
      "launchCost",
      "cargoLost",
      "modulesLost",
      "approachSeconds",
      "objectiveSeconds",
      "returnSeconds",
    ].every((key) => number(row[key])) &&
    typeof pot === "object" &&
    pot !== null &&
    "contract" in pot &&
    "bounty" in pot &&
    "relay" in pot &&
    number(pot.contract) &&
    number(pot.bounty) &&
    number(pot.relay)
  );
}
