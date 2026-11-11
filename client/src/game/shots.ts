/**
 * Shots and blasts as the player sees them. The server decides every hit; the client only
 * draws a bolt from shooter to target at the projectile's speed, and a flash where a ship is
 * destroyed. Lost snapshots lose these visuals, never the outcome.
 */

export interface Point {
  x: number;
  z: number;
}

export interface Bolt {
  /** Who fired it, to colour it by allegiance. */
  shooter: number;
  x: number;
  z: number;
  /** Direction of travel, radians on the plane. */
  heading: number;
}

export interface Blast {
  x: number;
  z: number;
  /** 0 when it starts, 1 when it has faded. */
  age: number;
}

interface Flying {
  shooter: number;
  target: number;
  x: number;
  z: number;
  speed: number;
  heading: number;
  /** Seconds left before it is dropped, whatever happens. */
  life: number;
}

const BLAST_SECONDS = 0.6;
const MAX_LIFE_SECONDS = 3;
/**
 * The most bolts drawn at once: as many shots as the server keeps in flight in one instance
 * (`frontier_sim::combat::MAX_PROJECTILES`). The scene keeps a mesh for the most it has ever
 * drawn, so this also bounds what a burst of late events can leave behind.
 */
export const MAX_BOLTS = 64;

export class Shots {
  #flying: Flying[] = [];
  #blasts: (Point & { seconds: number })[] = [];

  /** A shot from `shooter`, now at `from`, homing on `target` at `speed` m/s. */
  fire(shooter: number, target: number, from: Point, speed: number): void {
    this.#flying.push({ shooter, target, ...from, speed, heading: 0, life: MAX_LIFE_SECONDS });
    // Beyond what can be in flight, the oldest are leftovers: drop them.
    if (this.#flying.length > MAX_BOLTS) this.#flying.splice(0, this.#flying.length - MAX_BOLTS);
  }

  /** A ship destroyed at `at`. */
  blast(at: Point): void {
    this.#blasts.push({ ...at, seconds: 0 });
  }

  /**
   * Point defence on `target` destroyed a shot from `shooter`: the nearest such bolt to the
   * target goes, with a flash where it was. Returns whether a bolt was found.
   */
  intercept(shooter: number, target: number, at: Point): boolean {
    let nearest = -1;
    let best = Infinity;
    this.#flying.forEach((shot, index) => {
      if (shot.shooter !== shooter || shot.target !== target) return;
      const distance = Math.hypot(shot.x - at.x, shot.z - at.z);
      if (distance < best) {
        best = distance;
        nearest = index;
      }
    });
    const [shot] = nearest >= 0 ? this.#flying.splice(nearest, 1) : [];
    if (shot) this.#blasts.push({ x: shot.x, z: shot.z, seconds: BLAST_SECONDS / 2 });
    return shot !== undefined;
  }

  /**
   * Moves every bolt `dt` seconds toward where its target is drawn now, and ages the blasts.
   * A bolt whose target is gone, or that arrives, disappears.
   */
  advance(dt: number, positionOf: (id: number) => Point | undefined): void {
    this.#flying = this.#flying.filter((shot) => {
      const target = positionOf(shot.target);
      shot.life -= dt;
      if (!target || shot.life <= 0) return false;
      const dx = target.x - shot.x;
      const dz = target.z - shot.z;
      const distance = Math.hypot(dx, dz);
      const travel = shot.speed * dt;
      if (distance <= travel) return false;
      shot.heading = Math.atan2(dz, dx);
      shot.x += (dx / distance) * travel;
      shot.z += (dz / distance) * travel;
      return true;
    });
    for (const blast of this.#blasts) blast.seconds += dt;
    this.#blasts = this.#blasts.filter((blast) => blast.seconds < BLAST_SECONDS);
  }

  get bolts(): Bolt[] {
    return this.#flying.map(({ shooter, x, z, heading }) => ({ shooter, x, z, heading }));
  }

  get blasts(): Blast[] {
    return this.#blasts.map(({ x, z, seconds }) => ({ x, z, age: seconds / BLAST_SECONDS }));
  }
}
