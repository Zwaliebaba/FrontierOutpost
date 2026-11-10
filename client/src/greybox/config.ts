import { SCENE_IDS } from "./scenarios";
import type { HeightMode, SceneId } from "./scenarios";
import { LOOK_MODES } from "../view/look";
import type { LookMode } from "../view/look";
import { ELEVATIONS, PROJECTIONS } from "../view/view";
import type { Projection } from "../view/view";

export const VIEW_WIDTHS = [40, 55, 73] as const;
export const OCCLUSION_MODES = ["none", "silhouette", "fade"] as const;
export const VISION_MODES = ["normal", "grey", "protan", "deutan"] as const;
/** `stars` is deep space with a star field; `dark` and `bright` are flat colours. */
export const BACKDROPS = ["stars", "dark", "bright"] as const;
export const HEIGHT_MODES = ["flat", "varied"] as const;

export interface Config {
  scene: SceneId;
  /** Camera elevation above the gameplay plane, in degrees. */
  elevation: number;
  projection: Projection;
  /** Whether the tester may orbit the camera away from the tactical view. */
  look: LookMode;
  /** Visible width in metres. */
  width: number;
  /** Draw plane cues: markers under ships, range rings, drop lines. */
  cues: boolean;
  occlusion: (typeof OCCLUSION_MODES)[number];
  heights: HeightMode;
  vision: (typeof VISION_MODES)[number];
  backdrop: (typeof BACKDROPS)[number];
  seed: number;
  /** Trials of each kind per run. */
  trials: number;
}

export const DEFAULT_CONFIG: Config = {
  scene: "station",
  elevation: 33,
  projection: "ortho",
  look: "hold",
  width: 40,
  cues: false,
  occlusion: "silhouette",
  heights: "flat",
  vision: "normal",
  backdrop: "stars",
  seed: 1,
  trials: 8,
};

function oneOf<T extends string | number>(
  options: readonly T[],
  raw: string | null,
  fallback: T,
): T {
  return options.find((option) => String(option) === raw) ?? fallback;
}

function integer(raw: string | null, fallback: number, min: number, max: number): number {
  const value = raw === null ? Number.NaN : Number.parseInt(raw, 10);
  return Number.isFinite(value) ? Math.min(max, Math.max(min, value)) : fallback;
}

/** Reads a config from a URL query string; anything missing or invalid falls back to default. */
export function parseConfig(search: string): Config {
  const params = new URLSearchParams(search);
  const d = DEFAULT_CONFIG;
  return {
    scene: oneOf(SCENE_IDS, params.get("scene"), d.scene),
    elevation: oneOf(ELEVATIONS, params.get("elevation"), d.elevation),
    projection: oneOf(PROJECTIONS, params.get("projection"), d.projection),
    look: oneOf(LOOK_MODES, params.get("look"), d.look),
    width: oneOf(VIEW_WIDTHS, params.get("width"), d.width),
    cues: oneOf(["1", "0"], params.get("cues"), d.cues ? "1" : "0") === "1",
    occlusion: oneOf(OCCLUSION_MODES, params.get("occlusion"), d.occlusion),
    heights: oneOf(HEIGHT_MODES, params.get("heights"), d.heights),
    vision: oneOf(VISION_MODES, params.get("vision"), d.vision),
    backdrop: oneOf(BACKDROPS, params.get("backdrop"), d.backdrop),
    seed: integer(params.get("seed"), d.seed, 0, 999_999),
    trials: integer(params.get("trials"), d.trials, 1, 40),
  };
}

export function configToSearch(config: Config): string {
  const params = new URLSearchParams();
  for (const [key, value] of Object.entries(config)) {
    params.set(key, typeof value === "boolean" ? (value ? "1" : "0") : String(value));
  }
  return `?${params.toString()}`;
}
