/** Smallest signed angle from `from` to `to`, in (-PI, PI]. */
export function angleDifference(to: number, from: number): number {
  let diff = (to - from) % (2 * Math.PI);
  if (diff > Math.PI) diff -= 2 * Math.PI;
  if (diff <= -Math.PI) diff += 2 * Math.PI;
  return diff;
}
