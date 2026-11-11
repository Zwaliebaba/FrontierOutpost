/**
 * The readability rules both renderers draw with (design §5): every combat ship at one height,
 * allegiance shown by shape and colour together, and ships behind structures shown as
 * silhouettes. The greybox measures these rules; the expedition view uses them unchanged.
 */
import {
  AlwaysStencilFunc,
  BufferGeometry,
  Color,
  DoubleSide,
  EqualStencilFunc,
  GreaterDepth,
  Group,
  LinearSRGBColorSpace,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  PlaneGeometry,
  ReplaceStencilOp,
} from "three";
import { SHIP_BEAM, SHIP_HEIGHT, SHIP_LENGTH, forkedHull, wedgeHull } from "./hulls";

export type Allegiance = "self" | "ally" | "hostile";

export const linear = (r: number, g: number, b: number): Color =>
  new Color().setRGB(r, g, b, LinearSRGBColorSpace);

/** The plane height every combat ship's centre is drawn at (design §5). */
export const RENDER_HEIGHT = 0;

/** Hull tones from the style guide's material baselines; the player's own ship is lightest. */
export const HULL: Readonly<Record<Allegiance, Color>> = {
  self: linear(0.53, 0.57, 0.61),
  ally: linear(0.29, 0.35, 0.43),
  hostile: linear(0.3, 0.11, 0.07),
};

/** Signal colours for silhouettes and markers; colour always pairs with a shape. */
export const SIGNAL: Readonly<Record<Allegiance, Color>> = {
  self: linear(1, 1, 1),
  ally: linear(0.04, 0.65, 1),
  hostile: linear(1, 0.4, 0.055),
};

/** Bolt edges: player-side shots cyan, hostile shots a deeper orange than the hostile signal. */
export const BOLT: Readonly<Record<"pilot" | "hostile", Color>> = {
  pilot: SIGNAL.ally,
  hostile: linear(1, 0.25, 0.05),
};

/** Stencil value structures write, so silhouettes show behind them and nowhere else. */
export const STRUCTURE = 1;

/** Stencil settings for a structure: it marks every pixel where it is the nearest surface. */
export const STRUCTURE_STENCIL = {
  stencilWrite: true,
  stencilRef: STRUCTURE,
  stencilFunc: AlwaysStencilFunc,
  stencilZPass: ReplaceStencilOp,
} as const;

/** Player-side ships are a wedge, hostiles are forked: the shape half of allegiance. */
export function hullShape(allegiance: Allegiance): "wedge" | "forked" {
  return allegiance === "hostile" ? "forked" : "wedge";
}

const SHAPES = { wedge: wedgeHull(), forked: forkedHull() };

/** The shared geometry for an allegiance's hull. Callers scale meshes, never the geometry. */
export function hullGeometry(allegiance: Allegiance): BufferGeometry {
  return SHAPES[hullShape(allegiance)];
}

/** A hull material. A visible hull clears the structure mark, so it is drawn normally. */
export function hullMaterial(allegiance: Allegiance): MeshStandardMaterial {
  return new MeshStandardMaterial({
    color: HULL[allegiance],
    roughness: 0.45,
    metalness: 0.5,
    stencilWrite: true,
    stencilRef: 0,
    stencilFunc: AlwaysStencilFunc,
    stencilZPass: ReplaceStencilOp,
  });
}

/**
 * A silhouette material: drawn only where a structure is the nearest surface and the hull lies
 * behind it. Meshes using it must render after structures and hulls.
 */
export function silhouetteMaterial(allegiance: Allegiance): MeshBasicMaterial {
  return new MeshBasicMaterial({
    color: SIGNAL[allegiance],
    depthFunc: GreaterDepth,
    depthWrite: false,
    transparent: true,
    opacity: 0.9,
    stencilWrite: true,
    stencilRef: STRUCTURE,
    stencilFunc: EqualStencilFunc,
  });
}

/** Render order for silhouettes: after every opaque surface has written depth and stencil. */
export const SILHOUETTE_ORDER = 10;

/** A ship as drawn: its hull, and its silhouette for when a structure hides it. */
export interface ShipMeshes {
  hull: Mesh<BufferGeometry, MeshStandardMaterial>;
  silhouette: Mesh<BufferGeometry, MeshBasicMaterial>;
}

export function shipMeshes(allegiance: Allegiance, scale = 1): ShipMeshes {
  const geometry = hullGeometry(allegiance);
  const hull = new Mesh(geometry, hullMaterial(allegiance));
  const silhouette = new Mesh(geometry, silhouetteMaterial(allegiance));
  silhouette.renderOrder = SILHOUETTE_ORDER;
  hull.scale.setScalar(scale);
  silhouette.scale.setScalar(scale);
  return { hull, silhouette };
}

/** Places both meshes of a ship at the one render height, facing `heading`. */
export function placeShip(meshes: ShipMeshes, x: number, z: number, heading: number): void {
  for (const mesh of [meshes.hull, meshes.silhouette]) {
    mesh.position.set(x, RENDER_HEIGHT, z);
    mesh.rotation.set(0, -heading, 0);
  }
}

/** Render order for the lock bracket: on top of everything, structures included. */
export const BRACKET_ORDER = 20;

/**
 * Four corner marks around a locked ship, drawn over everything so a lock is never lost behind a
 * structure. Sized for a starter hull; scale the group for larger ships.
 */
export function lockBracket(): Group {
  const material = new MeshBasicMaterial({
    color: SIGNAL.hostile,
    depthTest: false,
    depthWrite: false,
    transparent: true,
    side: DoubleSide,
    // Flat strips with no depth: one pass draws both faces. Without this three.js draws a
    // transparent double-sided material twice, back faces then front, and marks the material
    // changed before each, so every strip rebuilt its program parameters twice a frame.
    forceSinglePass: true,
  });
  const group = new Group();
  const half = Math.max(SHIP_LENGTH, SHIP_BEAM) * 0.75;
  const arm = half * 0.45;
  const thick = 0.12;
  for (const sx of [-1, 1]) {
    for (const sz of [-1, 1]) {
      const along = new Mesh(stripGeometry(arm, thick), material);
      along.position.set(sx * (half - arm / 2), SHIP_HEIGHT / 2, sz * half);
      const across = new Mesh(stripGeometry(thick, arm), material);
      across.position.set(sx * half, SHIP_HEIGHT / 2, sz * (half - arm / 2));
      group.add(along, across);
    }
  }
  group.traverse((node) => {
    node.renderOrder = BRACKET_ORDER;
  });
  return group;
}

/** A flat strip on the plane, `x` by `z` metres. */
function stripGeometry(x: number, z: number): BufferGeometry {
  return new PlaneGeometry(x, z).rotateX(-Math.PI / 2);
}
