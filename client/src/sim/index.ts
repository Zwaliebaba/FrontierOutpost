/**
 * The simulation rules compiled from Rust (`crates/sim-wasm`). `npm run build:sim` writes the
 * WebAssembly module and its bindings to `./pkg`; it runs before `npm run dev` and `npm run build`.
 */
import init, { ticksForMillis, ticksPerSecond } from "./pkg/frontier_sim_wasm";

export interface Sim {
  ticksPerSecond(): number;
  ticksForMillis(millis: number): number;
}

let loading: Promise<Sim> | null = null;

/** Loads and instantiates the module once; later calls share the same instance. */
export function loadSim(): Promise<Sim> {
  loading ??= init().then(() => ({ ticksPerSecond, ticksForMillis }));
  return loading;
}
