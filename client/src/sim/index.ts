/**
 * The simulation rules compiled from Rust (`crates/sim-wasm`). `npm run build:sim` writes the
 * WebAssembly module and its bindings to `./pkg`; it runs before `npm run dev` and `npm run build`.
 */
import init, {
  initSync,
  setObstacles,
  stepShip,
  ticksForMillis,
  ticksPerSecond,
} from "./pkg/frontier_sim_wasm";
import type { SyncInitInput } from "./pkg/frontier_sim_wasm";

/** A ship on the gameplay plane, as `frontier_sim::ship::ShipState`. */
export interface ShipState {
  x: number;
  z: number;
  /** Radians, 0 to 2π; 0 faces +x and the angle grows toward +z. */
  heading: number;
  /** Metres per second along the heading. */
  speed: number;
}

/** Controls for one step, each -1 to 1. */
export interface ShipControls {
  thrust: number;
  turn: number;
  /** The cruise drive is engaged (frontier_sim::ship::CRUISE_FACTOR times speed). */
  cruise?: boolean;
}

export interface Sim {
  ticksPerSecond(): number;
  ticksForMillis(millis: number): number;
  /** One step of the starter hull, computed by the same Rust code the server runs. */
  stepShip(state: ShipState, controls: ShipControls): ShipState;
  /** Sets the fixed obstacles that `stepShip` collides with. */
  setObstacles(obstacles: readonly { x: number; z: number; radius: number }[]): void;
}

const sim: Sim = {
  ticksPerSecond,
  ticksForMillis,
  stepShip(state, controls) {
    const [x = state.x, z = state.z, heading = state.heading, speed = state.speed] = stepShip(
      state.x,
      state.z,
      state.heading,
      state.speed,
      controls.thrust,
      controls.turn,
      controls.cruise === true,
    );
    return { x, z, heading, speed };
  },
  setObstacles(obstacles) {
    setObstacles(Float32Array.from(obstacles.flatMap((o) => [o.x, o.z, o.radius])));
  },
};

let loading: Promise<Sim> | null = null;

/** Loads and instantiates the module once; later calls share the same instance. */
export function loadSim(): Promise<Sim> {
  loading ??= init().then(() => sim);
  return loading;
}

/** Instantiates the module from its bytes, for tests that run outside a browser. */
export function loadSimSync(module: SyncInitInput): Sim {
  initSync({ module });
  loading = Promise.resolve(sim);
  return sim;
}
