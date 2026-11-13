/**
 * A jump as the pilot sees it, in three beats. The spool: light gathers on the plane under the
 * ship, sparks spiral in faster and faster, one crisp ring closes as the countdown, and in the
 * last moment the hull draws out along its length. The departure: the hull squeezes into a line
 * of light, pops to a point, and a shockwave opens behind a trail the way it went. The arrival:
 * a point draws out into the stretched hull, which settles, a ring closes on it, and sparks
 * scatter back along the way it came. Reliable events start crew spools; snapshots locate the
 * departure and arrival when a ship moves farther than normal flight allows. The server decides
 * every jump. Kept free of the DOM and WebGL so it can be tested.
 */
import type { EntityKind } from "../protocol";
import { teleported } from "../net/interpolation";
import { SHIP_BEAM, SHIP_LENGTH } from "../view/hulls";

export interface Point {
  x: number;
  z: number;
}

/** The spool on a ship. */
export interface Charge {
  ship: number;
  /** How far through the spool, 0 to 1. */
  share: number;
  /** Where the closing ring is in its loop, 0 (far out) to 1 (at the hull). */
  pulse: number;
}

/** A flash where a ship left, or where it arrived. */
export interface Flash {
  kind: "leave" | "arrive";
  /** The ship that jumped. */
  ship: number;
  x: number;
  z: number;
  /** The way the ship jumped, radians on the plane, and how far, metres. */
  heading: number;
  distance: number;
  /** 0 when it starts, 1 when it has faded. */
  age: number;
}

/** How long each flash lasts, seconds: the departure's trail lingers longest. */
export const FLASH_SECONDS = 0.9;
/** How much longer the hull is drawn at the height of a jump: presentation only. */
export const STRETCH_MOST = 0.35;
/** The share of the spool after which the hull draws out: its last 0.3 s of six. */
const STRETCH_FROM = 0.95;
/** How long an arriving hull takes to settle from stretched, seconds. */
export const SETTLE_SECONDS = 0.25;
/** How far the view leans in as the pilot's own jump nears, as a zoom factor over 1. */
export const PUNCH_MOST = 0.06;
/** The share of the spool after which the view leans in, and how long it lets go, seconds. */
const PUNCH_FROM = 0.85;
const PUNCH_RELEASE = 0.4;
/** Sparks spiralling into a spooling hull, and how far out they start, metres. */
export const SPARKS = 32;
const SPARK_FAR = 8;
/** Where a spark meets the hull, metres from its centre. */
const SPARK_NEAR = 1.1;
/** How far round a spark turns on its way in, radians. */
const SPARK_SPIN = 1.8;
/** Sparks thrown back from an arrival, and how long they glow, seconds. */
export const SCATTER = 18;
const SCATTER_SECONDS = 0.7;
/** How long the departing hull takes to squeeze into a line and pop, seconds. */
const SQUEEZE_SECONDS = 0.14;
/** The longest trail a departure leaves, and the longest an arrival brings in, metres. */
const TRAIL_OUT = 40;
const TRAIL_IN = 14;
/** The shockwave's radius from start to end, metres. */
const SHOCK = { near: 1.5, far: 11 };
/** The closing ring loops this often at the start of a spool and at its end, per second. */
const PULSE_RATE = { start: 0.7, end: 3 };
/** Flashes kept at once: four pilots jumping together leave eight. */
const MOST_FLASHES = 16;

interface Seen extends Point {
  kind: EntityKind;
}

export class JumpEffects {
  #seen = new Map<number, Seen>();
  #flashes: (Omit<Flash, "age"> & { seconds: number })[] = [];
  #charge: { ship: number; share: number } | null = null;
  #phase = 0;
  readonly #remote = new Map<number, { left: number; total: number; phase: number }>();

  /**
   * Notes where each ship is drawn this frame, `seconds` after the last. A pilot's ship that
   * moved farther than flight allows jumped: it gets a flash at each end. Returns the ships
   * that jumped.
   */
  observe(
    poses: readonly { id: number; kind: EntityKind; x: number; z: number }[],
    seconds: number,
  ): number[] {
    const jumped: number[] = [];
    const now = new Map<number, Seen>();
    for (const pose of poses) {
      now.set(pose.id, { kind: pose.kind, x: pose.x, z: pose.z });
      const before = this.#seen.get(pose.id);
      // Only pilots have a jump drive. A ship first seen has no place it came from.
      if (pose.kind !== "playerShip" || before?.kind !== "playerShip") continue;
      const dx = pose.x - before.x;
      const dz = pose.z - before.z;
      const distance = Math.hypot(dx, dz);
      if (!teleported(distance, seconds)) continue;
      const heading = Math.atan2(dz, dx);
      this.#flashes.push(
        { kind: "leave", ship: pose.id, x: before.x, z: before.z, heading, distance, seconds: 0 },
        { kind: "arrive", ship: pose.id, x: pose.x, z: pose.z, heading, distance, seconds: 0 },
      );
      jumped.push(pose.id);
    }
    this.#seen = now;
    if (this.#flashes.length > MOST_FLASHES) {
      this.#flashes.splice(0, this.#flashes.length - MOST_FLASHES);
    }
    return jumped;
  }

  /** The spool on `ship`, `share` of the way through; null when no drive is spooling. */
  setCharge(ship: number | null, share = 0): void {
    if (ship === null) {
      this.#charge = null;
      this.#phase = 0;
      return;
    }
    this.#charge = { ship, share: Math.min(1, Math.max(0, share)) };
  }

  /** Reliable crew spool events; a missing end event expires after the advertised duration. */
  remoteCharge(ship: number, seconds: number): void {
    if (seconds <= 0) {
      this.#remote.delete(ship);
      return;
    }
    if (this.#remote.size < 4 || this.#remote.has(ship))
      this.#remote.set(ship, { left: seconds, total: seconds, phase: 0 });
  }

  get charges(): Charge[] {
    return [
      ...(this.charge ? [this.charge] : []),
      ...[...this.#remote].map(([ship, charge]) => ({
        ship,
        share: 1 - charge.left / charge.total,
        pulse: charge.phase,
      })),
    ];
  }

  /** Ages the flashes and turns the spool's closing ring, `dt` seconds on. */
  advance(dt: number, elapsed = dt): void {
    for (const [ship, charge] of this.#remote) {
      charge.left -= elapsed;
      charge.phase =
        (charge.phase +
          dt *
            (PULSE_RATE.start +
              (PULSE_RATE.end - PULSE_RATE.start) * (1 - charge.left / charge.total))) %
        1;
      if (charge.left <= 0) this.#remote.delete(ship);
    }
    for (const flash of this.#flashes) flash.seconds += dt;
    this.#flashes = this.#flashes.filter((flash) => flash.seconds < FLASH_SECONDS);
    if (this.#charge) {
      // The ring closes faster as the jump nears, so the last second reads as the last.
      const rate = PULSE_RATE.start + (PULSE_RATE.end - PULSE_RATE.start) * this.#charge.share;
      this.#phase = (this.#phase + dt * rate) % 1;
    }
  }

  get charge(): Charge | null {
    return this.#charge ? { ...this.#charge, pulse: this.#phase } : null;
  }

  /**
   * How much longer than itself `ship`'s hull is drawn: it draws out over the spool's last
   * moment and arrives drawn out, then settles. 1 for a ship not jumping.
   */
  stretch(ship: number): number {
    const arrival = this.#arrival(ship, SETTLE_SECONDS);
    if (arrival) return 1 + STRETCH_MOST * (1 - arrival.seconds / SETTLE_SECONDS) ** 2;
    const charge = this.charges.find((c) => c.ship === ship);
    if (!charge) return 1;
    return 1 + STRETCH_MOST * smooth((charge.share - STRETCH_FROM) / (1 - STRETCH_FROM));
  }

  /**
   * The zoom on the view for the pilot flying `ship`: it leans in as their own jump nears and
   * lets go once they have arrived, so the cut lands on a beat. 1 at rest.
   */
  punch(ship: number): number {
    const arrival = this.#arrival(ship, PUNCH_RELEASE);
    if (arrival) return 1 + PUNCH_MOST * (1 - smooth(arrival.seconds / PUNCH_RELEASE));
    const charge = this.#charge?.ship === ship ? this.#charge : null;
    if (!charge) return 1;
    return 1 + PUNCH_MOST * smooth((charge.share - PUNCH_FROM) / (1 - PUNCH_FROM));
  }

  /** The newest arrival of `ship` younger than `seconds`. */
  #arrival(ship: number, seconds: number) {
    return this.#flashes.findLast(
      (flash) => flash.ship === ship && flash.kind === "arrive" && flash.seconds < seconds,
    );
  }

  get flashes(): Flash[] {
    return this.#flashes.map(({ seconds, ...flash }) => ({
      ...flash,
      age: seconds / FLASH_SECONDS,
    }));
  }

  /** A new run, or none: nothing carries over. */
  reset(): void {
    this.#seen.clear();
    this.#remote.clear();
    this.#flashes = [];
    this.#charge = null;
    this.#phase = 0;
  }
}

const smooth = (t: number): number => {
  const x = Math.min(1, Math.max(0, t));
  return x * x * (3 - 2 * x);
};
const easeOut = (t: number): number => 1 - (1 - Math.min(1, Math.max(0, t))) ** 3;
/** A fixed scatter of numbers in [0, 1) for spark `index`, so each keeps its own path. */
const seeded = (index: number, salt: number): number => {
  const x = Math.sin(index * 127.1 + salt * 311.7) * 43_758.545;
  return x - Math.floor(x);
};

/** A spark, metres from the hull it belongs to, and how bright it is, 0 to 1. */
export interface Spark {
  x: number;
  z: number;
  glow: number;
}

/**
 * Spark `index` of `count` on a spooling hull, at the spool's `pulse` (Charge.pulse): it starts
 * far out, spirals in and fades into the hull. The pulse loops faster as the jump nears, and so
 * do the sparks.
 */
export function sparkAt(index: number, count: number, pulse: number): Spark {
  const raw = pulse + index / count;
  const along = raw - Math.floor(raw);
  const start = SPARK_FAR * (0.6 + 0.4 * seeded(index, 1));
  const radius = SPARK_NEAR + (start - SPARK_NEAR) * (1 - along) ** 1.6;
  const angle = seeded(index, 2) * 2 * Math.PI + along * SPARK_SPIN;
  return {
    x: Math.cos(angle) * radius,
    z: Math.sin(angle) * radius,
    glow: Math.min(1, along / 0.2, (1 - along) / 0.1),
  };
}

/**
 * Spark `index` of `count` thrown back from an arrival, `seconds` after it, for a ship that
 * jumped along `heading`: out behind it in a fan, slowing, fading.
 */
export function scatterAt(index: number, count: number, seconds: number, heading: number): Spark {
  const fan = (index / Math.max(1, count - 1) - 0.5) * 1.8 + (seeded(index, 3) - 0.5) * 0.3;
  const speed = 7 + 9 * seeded(index, 4);
  const drag = 3;
  const travelled = (speed * (1 - Math.exp(-seconds * drag))) / drag;
  const angle = heading + Math.PI + fan;
  return {
    x: Math.cos(angle) * travelled,
    z: Math.sin(angle) * travelled,
    glow: Math.max(0, 1 - seconds / SCATTER_SECONDS) ** 1.5,
  };
}

/**
 * The shapes of one end of a jump at its age, metres along the way the ship went. Where it left:
 * the hull's squeezed line, then a point, a shockwave opening and a trail shooting ahead. Where it
 * arrives: a point, a ring closing on the hull and a trail drawn in behind it.
 */
export interface FlashShape {
  /** The departing hull squeezed into a line of light; zero length when gone. */
  line: { length: number; width: number };
  /** The point of light the ship pops into or out of: across, metres, and opacity. */
  point: { size: number; opacity: number };
  ring: { radius: number; opacity: number };
  /** Along the jump: ahead of a departure, behind an arrival. */
  trail: { length: number; opacity: number };
}

export function flashShape(flash: Flash): FlashShape {
  const seconds = flash.age * FLASH_SECONDS;
  if (flash.kind === "leave") {
    const squeeze = seconds / SQUEEZE_SECONDS;
    const popped = Math.max(0, seconds - SQUEEZE_SECONDS) / 0.35;
    const shock = seconds / 0.6;
    return {
      line:
        squeeze < 1
          ? {
              length: SHIP_LENGTH * (1 + STRETCH_MOST) * (1 + 1.2 * squeeze),
              width: SHIP_BEAM * 0.6 * (1 - squeeze) ** 1.5,
            }
          : { length: 0, width: 0 },
      point: {
        size: 4 * (squeeze < 1 ? squeeze : Math.max(0, 1 - popped)),
        opacity: squeeze < 1 ? squeeze : Math.max(0, 1 - popped) ** 2,
      },
      ring: {
        radius: SHOCK.near + (SHOCK.far - SHOCK.near) * easeOut(shock),
        opacity: 0.9 * Math.max(0, 1 - shock) ** 1.2,
      },
      trail: {
        length: Math.min(TRAIL_OUT, flash.distance) * easeOut(seconds / 0.25),
        opacity: 0.7 * (1 - flash.age) ** 1.3,
      },
    };
  }
  const closing = seconds / 0.35;
  return {
    line: { length: 0, width: 0 },
    point: { size: 3.5 * Math.max(0, 1 - seconds / 0.2), opacity: Math.max(0, 1 - seconds / 0.2) },
    ring: {
      radius: SPARK_NEAR + (SHOCK.far * 0.75 - SPARK_NEAR) * (1 - easeOut(closing)),
      opacity: Math.min(1, seconds / 0.05) * Math.max(0, 1 - seconds / 0.45),
    },
    trail: {
      length: Math.min(TRAIL_IN, flash.distance) * (1 - easeOut(seconds / 0.3)),
      opacity: 0.6 * (1 - flash.age),
    },
  };
}
