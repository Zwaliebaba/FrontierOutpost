/**
 * Draws other entities slightly in the past, between two snapshots, so that motion stays
 * smooth when datagrams arrive unevenly or are lost.
 */
import type { EntityKind, Snapshot } from "../protocol";
import { angleDifference } from "../view/angles";
import { headingRadians, positionMetres, velocityMetresPerSecond } from "./snapshot";

export interface EntityPose {
  id: number;
  kind: EntityKind;
  x: number;
  z: number;
  heading: number;
  hull: number;
}

/** How far behind the newest server time entities are drawn (plan, work item 1.3). */
export const INTERPOLATION_DELAY_MS = 100;
/** How far past the newest snapshot an entity is extrapolated before it stops. */
const MAX_EXTRAPOLATION_MS = 150;
const KEEP_MS = 1_000;

interface Received {
  snapshot: Snapshot;
  /** Server time of the snapshot, in local milliseconds. */
  at: number;
}

export class SnapshotBuffer {
  readonly #tickMs: number;
  readonly #delayMs: number;
  #received: Received[] = [];
  /**
   * Local time minus server time, from the fastest snapshot seen. Delay only ever adds to it,
   * so the smallest observed value is the closest to the true offset.
   */
  #offset = Number.POSITIVE_INFINITY;

  constructor(tickRate: number, delayMs = INTERPOLATION_DELAY_MS) {
    this.#tickMs = 1_000 / tickRate;
    this.#delayMs = delayMs;
  }

  /** Records a snapshot that arrived at local time `arrivedMs`. */
  push(snapshot: Snapshot, arrivedMs: number): void {
    const serverMs = snapshot.tick * this.#tickMs;
    this.#offset = Math.min(this.#offset, arrivedMs - serverMs);
    const newest = this.#received.at(-1);
    // Datagrams can arrive out of order; an older snapshot adds nothing.
    if (newest && snapshot.tick <= newest.snapshot.tick) return;
    this.#received.push({ snapshot, at: serverMs });
    const oldest = serverMs - KEEP_MS;
    while ((this.#received[1]?.at ?? Number.POSITIVE_INFINITY) < oldest) this.#received.shift();
  }

  /** The newest snapshot received, if any. */
  get newest(): Snapshot | undefined {
    return this.#received.at(-1)?.snapshot;
  }

  /** Every entity as it should be drawn at local time `nowMs`. */
  sample(nowMs: number): EntityPose[] {
    const first = this.#received[0];
    if (!first) return [];
    const renderAt = nowMs - this.#offset - this.#delayMs;

    const afterIndex = this.#received.findIndex((entry) => entry.at > renderAt);
    if (afterIndex === -1) {
      // No snapshot is new enough: extrapolate a little from the newest.
      const newest = this.#received.at(-1) ?? first;
      const ahead = Math.min(renderAt - newest.at, MAX_EXTRAPOLATION_MS) / 1_000;
      return newest.snapshot.entities.map((entity) => {
        const pose = poseOf(entity);
        const velocity = velocityMetresPerSecond(entity);
        return { ...pose, x: pose.x + velocity.x * ahead, z: pose.z + velocity.z * ahead };
      });
    }
    const after = this.#received[afterIndex] ?? first;
    const before = this.#received[afterIndex - 1];
    if (!before) return after.snapshot.entities.map(poseOf);

    const t = (renderAt - before.at) / (after.at - before.at);
    const earlier = new Map(before.snapshot.entities.map((entity) => [entity.id, entity]));
    // Entities that left before `after` are gone; ones that joined appear where they joined.
    return after.snapshot.entities.map((entity) => {
      const end = poseOf(entity);
      const previous = earlier.get(entity.id);
      if (!previous) return end;
      const start = poseOf(previous);
      return {
        ...end,
        x: start.x + (end.x - start.x) * t,
        z: start.z + (end.z - start.z) * t,
        heading: start.heading + angleDifference(end.heading, start.heading) * t,
      };
    });
  }
}

function poseOf(entity: Snapshot["entities"][number]): EntityPose {
  const { x, z } = positionMetres(entity);
  return {
    id: entity.id,
    kind: entity.kind,
    x,
    z,
    heading: headingRadians(entity),
    hull: entity.hull,
  };
}
