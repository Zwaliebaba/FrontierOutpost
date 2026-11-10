import { NPC_HANDLING, PLAYER_HANDLING, seek, stepShip } from "./kinematics";
import type { ShipInput, ShipState } from "./kinematics";
import type { Occluder } from "./occlusion";
import { between, createRng } from "../view/rng";
import { LARGE_ASTEROID_RADIUS, stationOccluders } from "./stationProxy";

export const SCENE_IDS = ["crossing", "rock", "station", "headon"] as const;
export type SceneId = (typeof SCENE_IDS)[number];
export type { Allegiance } from "../view/readability";
import type { Allegiance } from "../view/readability";
export type HeightMode = "flat" | "varied";

export const SCENE_LABELS: Record<SceneId, string> = {
  crossing: "Open space, crossing traffic",
  rock: "Fight around a large asteroid",
  station: "Fight around the home station",
  headon: "Ships heading toward and away from the camera",
};

type Waypoint = readonly [x: number, z: number];

export interface Ship {
  id: string;
  allegiance: Allegiance;
  state: ShipState;
  /** Presentation height of the hull centre above the gameplay plane. */
  height: number;
  route: readonly Waypoint[];
  routeIndex: number;
  cooldown: number;
}

export interface Bolt {
  x: number;
  y: number;
  z: number;
  heading: number;
  allegiance: Allegiance;
  targetId: string;
  age: number;
}

export interface World {
  ships: Ship[];
  bolts: Bolt[];
  occluders: Occluder[];
  time: number;
}

/** Provisional projectile cap from the Revision 3 review (technical feasibility lens). */
export const MAX_BOLTS = 64;
const HOSTILES = 8;
const ALLIES = 3;
const WAYPOINT_RADIUS = 2.5;
const FIRE_RANGE = 22;
const FIRE_INTERVAL = 1.4;
const BOLT_SPEED = 28;
const BOLT_LIFETIME = 2.5;

/** Horizontal direction from the scene toward the tactical camera, in (x, z). */
const TOWARD_CAMERA: Waypoint = [0.43 / Math.hypot(0.43, 0.72), 0.72 / Math.hypot(0.43, 0.72)];

function orbit(radius: number, startAngle: number, clockwise: boolean): Waypoint[] {
  return Array.from({ length: 8 }, (_, step): Waypoint => {
    const angle = startAngle + ((clockwise ? 1 : -1) * step * Math.PI) / 4;
    return [Math.cos(angle) * radius, Math.sin(angle) * radius];
  });
}

function shuttle(angle: number, radius: number, skew: number): Waypoint[] {
  return [
    [Math.cos(angle) * radius, Math.sin(angle) * radius],
    [-Math.cos(angle + skew) * radius, -Math.sin(angle + skew) * radius],
  ];
}

function lane(offset: number, length: number, towardFirst: boolean): Waypoint[] {
  const [tx, tz] = TOWARD_CAMERA;
  const near: Waypoint = [tx * length - tz * offset, tz * length + tx * offset];
  const far: Waypoint = [-tx * length - tz * offset, -tz * length + tx * offset];
  return towardFirst ? [near, far] : [far, near];
}

export function createWorld(scene: SceneId, seed: number, heights: HeightMode): World {
  const rng = createRng(seed);
  const ships: Ship[] = [];

  const add = (id: string, allegiance: Allegiance, route: Waypoint[]) => {
    const [startX, startZ] = route[0] ?? [0, 0];
    const [nextX, nextZ] = route[1] ?? [0, 0];
    ships.push({
      id,
      allegiance,
      state: {
        x: startX,
        z: startZ,
        heading: Math.atan2(nextZ - startZ, nextX - startX),
        speed: allegiance === "self" ? 0 : NPC_HANDLING.maxSpeed,
      },
      height: heights === "varied" && allegiance !== "self" ? between(rng, -2, 2.2) : 0,
      route,
      routeIndex: 1,
      cooldown: between(rng, 0, FIRE_INTERVAL),
    });
  };

  // The tester starts on the camera side of the scene, facing into it.
  const [tx, tz] = TOWARD_CAMERA;
  add("self", "self", [
    [tx * 13, tz * 13],
    [0, 0],
  ]);

  for (let i = 0; i < HOSTILES + ALLIES; i += 1) {
    const hostile = i < HOSTILES;
    const id = hostile ? `H${String(i + 1)}` : `A${String(i - HOSTILES + 1)}`;
    const allegiance: Allegiance = hostile ? "hostile" : "ally";
    const jitter = between(rng, -0.25, 0.25);
    switch (scene) {
      case "crossing":
        add(
          id,
          allegiance,
          shuttle((i * Math.PI) / 5.5 + jitter, hostile ? 15 : 11, between(rng, -0.5, 0.5)),
        );
        break;
      case "rock":
      case "station":
        add(
          id,
          allegiance,
          orbit(
            (scene === "rock" ? 7.5 : 9.5) + (i % 4) * 1.6,
            (i * 2 * Math.PI) / (HOSTILES + ALLIES) + jitter,
            i % 2 === 0,
          ),
        );
        break;
      case "headon":
        add(id, allegiance, lane((i - 5) * 2.6 + jitter, 15, i % 2 === 0));
        break;
    }
  }

  const occluders: Occluder[] =
    scene === "rock"
      ? [{ kind: "sphere", x: 0, y: 0, z: 0, radius: LARGE_ASTEROID_RADIUS }]
      : scene === "station"
        ? stationOccluders()
        : [];
  return { ships, bolts: [], occluders, time: 0 };
}

function nearestOpponent(ship: Ship, ships: readonly Ship[]): Ship | undefined {
  let best: Ship | undefined;
  let bestDistance = FIRE_RANGE;
  for (const other of ships) {
    if ((other.allegiance === "hostile") === (ship.allegiance === "hostile")) continue;
    const distance = Math.hypot(other.state.x - ship.state.x, other.state.z - ship.state.z);
    if (distance < bestDistance) {
      best = other;
      bestDistance = distance;
    }
  }
  return best;
}

/** Advances the world by one fixed step. Deterministic: same world and inputs, same result. */
export function stepWorld(world: World, playerInput: ShipInput, dt: number): void {
  world.time += dt;
  for (const ship of world.ships) {
    if (ship.allegiance === "self") {
      ship.state = stepShip(ship.state, playerInput, PLAYER_HANDLING, dt);
    } else {
      const [targetX, targetZ] = ship.route[ship.routeIndex] ?? [0, 0];
      if (Math.hypot(targetX - ship.state.x, targetZ - ship.state.z) < WAYPOINT_RADIUS) {
        ship.routeIndex = (ship.routeIndex + 1) % ship.route.length;
      }
      ship.state = stepShip(ship.state, seek(ship.state, targetX, targetZ), NPC_HANDLING, dt);
    }

    ship.cooldown -= dt;
    if (ship.cooldown <= 0) {
      const target = nearestOpponent(ship, world.ships);
      if (target && world.bolts.length < MAX_BOLTS) {
        world.bolts.push({
          x: ship.state.x,
          y: ship.height,
          z: ship.state.z,
          heading: ship.state.heading,
          allegiance: ship.allegiance,
          targetId: target.id,
          age: 0,
        });
      }
      ship.cooldown = FIRE_INTERVAL;
    }
  }

  world.bolts = world.bolts.filter((bolt) => {
    const target = world.ships.find((ship) => ship.id === bolt.targetId);
    if (!target) return false;
    const dx = target.state.x - bolt.x;
    const dy = target.height - bolt.y;
    const dz = target.state.z - bolt.z;
    const distance = Math.hypot(dx, dy, dz);
    const travel = BOLT_SPEED * dt;
    bolt.age += dt;
    if (distance <= travel || bolt.age > BOLT_LIFETIME) return false;
    bolt.x += (dx / distance) * travel;
    bolt.y += (dy / distance) * travel;
    bolt.z += (dz / distance) * travel;
    bolt.heading = Math.atan2(dz, dx);
    return true;
  });
}
