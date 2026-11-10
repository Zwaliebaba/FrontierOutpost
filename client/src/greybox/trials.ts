import { angleDifference } from "./kinematics";
import type { Rng } from "../view/rng";

export const TRIAL_KINDS = ["acquire", "heading", "range"] as const;
export type TrialKind = (typeof TRIAL_KINDS)[number];

export interface TrialRecord {
  kind: TrialKind;
  /** Milliseconds from the prompt to the answer. */
  ms: number;
  outcome: "correct" | "wrong" | "miss" | "timeout";
  /** Heading error in degrees, or the far/near range ratio of the pair, depending on kind. */
  value: number | null;
  /** Whether the prompted ship's centre was behind a structure when the prompt appeared. */
  occluded: boolean;
  /** Camera yaw away from the tactical bearing when the tester answered, in degrees. */
  cameraYawDeg: number;
  /** Camera elevation when the tester answered, in degrees. */
  cameraPitchDeg: number;
  /** Visible width when the tester answered, in metres; differs from the config after zooming. */
  viewWidth: number;
}

export interface PlanePoint {
  id: string;
  x: number;
  z: number;
}

/** Angle between a ship's true heading and the direction a tester pointed, in degrees (0–180). */
export function headingErrorDeg(
  trueHeading: number,
  shipX: number,
  shipZ: number,
  pointedX: number,
  pointedZ: number,
): number {
  const pointed = Math.atan2(pointedZ - shipZ, pointedX - shipX);
  return (Math.abs(angleDifference(pointed, trueHeading)) * 180) / Math.PI;
}

export const HEADING_TOLERANCE_DEG = 22.5;

/**
 * Picks the two candidates whose distances from `origin` differ by closest to `targetRatio`.
 * Returns them nearer first, with the far/near ratio.
 */
export function chooseRangePair(
  origin: { x: number; z: number },
  candidates: readonly PlanePoint[],
  targetRatio = 1.2,
): { near: PlanePoint; far: PlanePoint; ratio: number } | null {
  const ranged = candidates
    .map((candidate) => ({
      candidate,
      range: Math.hypot(candidate.x - origin.x, candidate.z - origin.z),
    }))
    .filter((entry) => entry.range > 0.5)
    .sort((a, b) => a.range - b.range);
  let best: { near: PlanePoint; far: PlanePoint; ratio: number } | null = null;
  for (const [index, near] of ranged.entries()) {
    for (const far of ranged.slice(index + 1)) {
      const ratio = far.range / near.range;
      if (!best || Math.abs(ratio - targetRatio) < Math.abs(best.ratio - targetRatio)) {
        best = { near: near.candidate, far: far.candidate, ratio };
      }
    }
  }
  return best;
}

/** A shuffled run with `perKind` trials of each kind. */
export function planRun(perKind: number, rng: Rng): TrialKind[] {
  const plan = TRIAL_KINDS.flatMap((kind) => Array.from({ length: perKind }, () => kind));
  for (let i = plan.length - 1; i > 0; i -= 1) {
    const j = Math.floor(rng() * (i + 1));
    const a = plan[i];
    const b = plan[j];
    if (a !== undefined && b !== undefined) {
      plan[i] = b;
      plan[j] = a;
    }
  }
  return plan;
}

export function median(values: readonly number[]): number | null {
  if (values.length === 0) return null;
  const sorted = [...values].sort((a, b) => a - b);
  const middle = Math.floor(sorted.length / 2);
  const upper = sorted[middle];
  const lower = sorted[middle - 1];
  if (upper === undefined) return null;
  return sorted.length % 2 === 1 || lower === undefined ? upper : (lower + upper) / 2;
}

export interface Summary {
  trials: number;
  /** Median time to select the designated hostile, over correct answers. */
  acquireMedianMs: number | null;
  /** Share of selection trials that ended on the wrong ship, on empty space or timed out. */
  acquireErrorRate: number | null;
  /** Share of heading trials answered within `HEADING_TOLERANCE_DEG`. */
  headingCorrectRate: number | null;
  /** Share of nearer-target trials answered correctly. */
  rangeCorrectRate: number | null;
  /** Mean far/near ratio of the pairs shown, to judge how hard the range trials were. */
  rangeMeanRatio: number | null;
}

const rate = (records: readonly TrialRecord[], pass: (record: TrialRecord) => boolean) =>
  records.length === 0 ? null : records.filter(pass).length / records.length;

export function summarise(records: readonly TrialRecord[]): Summary {
  const of = (kind: TrialKind) => records.filter((record) => record.kind === kind);
  const acquire = of("acquire");
  const range = of("range");
  const ratios = range.flatMap((record) => (record.value === null ? [] : [record.value]));
  return {
    trials: records.length,
    acquireMedianMs: median(
      acquire.filter((record) => record.outcome === "correct").map((record) => record.ms),
    ),
    acquireErrorRate: rate(acquire, (record) => record.outcome !== "correct"),
    headingCorrectRate: rate(of("heading"), (record) => record.outcome === "correct"),
    rangeCorrectRate: rate(range, (record) => record.outcome === "correct"),
    rangeMeanRatio:
      ratios.length === 0 ? null : ratios.reduce((sum, value) => sum + value, 0) / ratios.length,
  };
}

/**
 * Starting hypotheses from the Revision 3 review. The design marks gate thresholds as open, so
 * these only colour the results panel; they are not pass criteria.
 */
export const PROVISIONAL_THRESHOLDS = {
  acquireMedianMs: 1000,
  acquireErrorRate: 0.05,
  headingCorrectRate: 0.9,
  rangeCorrectRate: 0.85,
  longestHiddenSeconds: 0.5,
} as const;
