import { angleDifference } from "../view/angles";

export { angleDifference };

/**
 * Direct-steering ship motion on the gameplay plane (XZ), for the greybox only.
 *
 * This is a stand-in so a tester can fly while judging readability. The real movement rules
 * belong in `frontier-sim`; nothing here is a design value.
 */
export interface ShipState {
  x: number;
  z: number;
  /** Radians on the plane; forward is (cos heading, sin heading) in (x, z). */
  heading: number;
  speed: number;
}

export interface ShipInput {
  /** -1 (brake) to 1 (full thrust). */
  thrust: number;
  /** -1 (left) to 1 (right, clockwise seen from above). */
  turn: number;
}

export interface ShipHandling {
  maxSpeed: number;
  acceleration: number;
  turnRate: number;
  drag: number;
}

export const PLAYER_HANDLING: ShipHandling = {
  maxSpeed: 9,
  acceleration: 7,
  turnRate: 2.4,
  drag: 0.5,
};
export const NPC_HANDLING: ShipHandling = {
  maxSpeed: 6,
  acceleration: 5,
  turnRate: 1.8,
  drag: 0.5,
};

const clamp = (value: number, min: number, max: number) => Math.min(max, Math.max(min, value));

export function stepShip(
  state: ShipState,
  input: ShipInput,
  handling: ShipHandling,
  dt: number,
): ShipState {
  const heading = state.heading + clamp(input.turn, -1, 1) * handling.turnRate * dt;
  const accelerated =
    state.speed +
    clamp(input.thrust, -1, 1) * handling.acceleration * dt -
    handling.drag * state.speed * dt;
  const speed = clamp(accelerated, 0, handling.maxSpeed);
  return {
    x: state.x + Math.cos(heading) * speed * dt,
    z: state.z + Math.sin(heading) * speed * dt,
    heading,
    speed,
  };
}

/** Input that steers `state` toward a point on the plane. */
export function seek(state: ShipState, targetX: number, targetZ: number): ShipInput {
  const desired = Math.atan2(targetZ - state.z, targetX - state.x);
  const diff = angleDifference(desired, state.heading);
  return { turn: clamp(diff * 2, -1, 1), thrust: Math.abs(diff) < 1 ? 1 : 0.3 };
}
