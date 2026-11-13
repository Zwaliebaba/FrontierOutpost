/**
 * The processor as the home panel shows it (design §10): one batch at a time, each taking a fixed
 * time, whether or not the pilot is flying or signed in. The server times the batches and says how
 * far along the one in hand was when it wrote the account; between account messages the page
 * counts on by its own clock, for the progress bar only. The server finishes the batch and sends
 * the account again. Kept free of the DOM so it can be tested.
 */
import type { AccountInfo } from "../protocol";

export interface ProcessorView {
  /** The recipe of the batch in hand; absent while the processor is idle. */
  recipe?: string;
  /** Batches at the processor, the one in hand included. */
  batches: number;
  /** How much of the batch in hand is done, 0 to 1. */
  share: number;
  /** Seconds until the batch in hand finishes. */
  left: number;
  /** Seconds until every batch at the processor has finished. */
  allLeft: number;
}

/** The processor `since` seconds after `account` arrived. */
export function processorView(account: AccountInfo, since: number): ProcessorView {
  const batches = account.processing.reduce((sum, queued) => sum + queued.batches, 0);
  const each = account.outpost.batchSeconds;
  const { working } = account;
  if (!working || each <= 0) return { batches, share: 0, left: 0, allLeft: 0 };
  const elapsed = Math.min(each, working.elapsedSeconds + Math.max(0, since));
  const left = each - elapsed;
  return {
    recipe: working.recipe,
    batches,
    share: elapsed / each,
    left,
    allLeft: left + Math.max(0, batches - 1) * each,
  };
}

/** A time left, as a clock: "2:41", or "1:02:41" from an hour. */
export function countdown(seconds: number): string {
  const whole = Math.max(0, Math.ceil(seconds));
  const hours = Math.floor(whole / 3600);
  const minutes = Math.floor((whole % 3600) / 60);
  const rest = String(whole % 60).padStart(2, "0");
  return hours > 0
    ? `${String(hours)}:${String(minutes).padStart(2, "0")}:${rest}`
    : `${String(minutes)}:${rest}`;
}

/** How long a batch takes, in words: "4 min", "1 min 36 s", "48 s". */
export function pace(seconds: number): string {
  const minutes = Math.floor(seconds / 60);
  const rest = seconds % 60;
  return [
    ...(minutes > 0 ? [`${String(minutes)} min`] : []),
    ...(rest > 0 || minutes === 0 ? [`${String(rest)} s`] : []),
  ].join(" ");
}

/** The line under the progress bar. */
export function progressLine(view: ProcessorView): string {
  if (view.recipe === undefined) return "";
  if (view.left <= 0) return "Finishing…";
  const batch = `${countdown(view.left)} left on this batch`;
  return view.batches > 1
    ? `${batch} · all ${String(view.batches)} done in ${countdown(view.allLeft)}`
    : batch;
}
