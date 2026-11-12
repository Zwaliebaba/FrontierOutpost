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
import type { Material, Object3D } from "three";
import { gearOf } from "./frigate";
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

/** Stencil settings for a hull: a visible hull clears the structure mark, so it draws normally. */
export const HULL_STENCIL = {
  stencilWrite: true,
  stencilRef: 0,
  stencilFunc: AlwaysStencilFunc,
  stencilZPass: ReplaceStencilOp,
} as const;

/** A stand-in hull's material. */
export function hullMaterial(allegiance: Allegiance): MeshStandardMaterial {
  return new MeshStandardMaterial({
    color: HULL[allegiance],
    roughness: 0.45,
    metalness: 0.5,
    ...HULL_STENCIL,
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
  hull: Object3D;
  silhouette: Object3D;
  /** The landing gear's nodes in both, for `setGear`; none on a stand-in hull. */
  gear: Object3D[];
  /** Frees what is this ship's own; geometry, and a model's materials, are shared. */
  dispose(): void;
}

/** The player-side model's two drawings, one per player-side allegiance. */
interface Templates {
  hull: Record<"self" | "ally", Object3D>;
  silhouette: Record<"self" | "ally", Object3D>;
}

let templates: Templates | null = null;

/**
 * How far an ally's armour moves toward the ally blue: shape says player side, colour says you
 * or your crew, as the stand-in hulls' tones did.
 */
const ALLY_TINT = 0.55;

function meshesOf(object: Object3D): Mesh[] {
  const meshes: Mesh[] = [];
  object.traverse((node) => {
    if (node instanceof Mesh) meshes.push(node as Mesh);
  });
  return meshes;
}

/** `model` drawn as `allegiance`'s hull: its own materials, the ally's armour tinted. */
function hullTemplate(model: Object3D, allegiance: "self" | "ally"): Object3D {
  const template = model.clone();
  const copies = new Map<Material, Material>();
  for (const mesh of meshesOf(template)) {
    const material = mesh.material;
    const own = (original: Material): Material => {
      let copy = copies.get(original);
      if (!copy) {
        copy = original.clone();
        Object.assign(copy, HULL_STENCIL);
        if (allegiance === "ally" && copy instanceof MeshStandardMaterial) {
          if (copy.name.startsWith("Armor")) copy.color.lerp(SIGNAL.ally, ALLY_TINT);
        }
        copies.set(original, copy);
      }
      return copy;
    };
    mesh.material = Array.isArray(material) ? material.map(own) : own(material);
  }
  return template;
}

/** `model` drawn as `allegiance`'s silhouette: every part in the signal colour. */
function silhouetteTemplate(model: Object3D, allegiance: "self" | "ally"): Object3D {
  const template = model.clone();
  const material = silhouetteMaterial(allegiance);
  for (const mesh of meshesOf(template)) {
    mesh.material = material;
    mesh.renderOrder = SILHOUETTE_ORDER;
  }
  return template;
}

/**
 * Draws player-side ships as `model` (the starter frigate, view/frigate) from now on, or as the
 * stand-in wedge again for `null`. Ships already drawn keep their hulls until redrawn.
 */
export function useShipModel(model: Object3D | null): void {
  templates = model && {
    hull: { self: hullTemplate(model, "self"), ally: hullTemplate(model, "ally") },
    silhouette: {
      self: silhouetteTemplate(model, "self"),
      ally: silhouetteTemplate(model, "ally"),
    },
  };
}

/** Whether player-side ships are drawn as the model rather than the stand-in. */
export function usesShipModel(): boolean {
  return templates !== null;
}

export function shipMeshes(allegiance: Allegiance, scale = 1): ShipMeshes {
  let ship: ShipMeshes;
  if (templates && allegiance !== "hostile") {
    const hull = templates.hull[allegiance].clone();
    const silhouette = templates.silhouette[allegiance].clone();
    ship = { hull, silhouette, gear: [...gearOf(hull), ...gearOf(silhouette)], dispose: () => {} };
  } else {
    // Hostiles keep the forked stand-in: there is no hostile model yet.
    const geometry = hullGeometry(allegiance);
    const hullMaterialOwn = hullMaterial(allegiance);
    const silhouetteMaterialOwn = silhouetteMaterial(allegiance);
    const silhouette = new Mesh(geometry, silhouetteMaterialOwn);
    silhouette.renderOrder = SILHOUETTE_ORDER;
    ship = {
      hull: new Mesh(geometry, hullMaterialOwn),
      silhouette,
      gear: [],
      dispose: () => {
        hullMaterialOwn.dispose();
        silhouetteMaterialOwn.dispose();
      },
    };
  }
  ship.hull.scale.setScalar(scale);
  ship.silhouette.scale.setScalar(scale);
  return ship;
}

/**
 * Places both meshes of a ship at the one render height, facing `heading` and leaning `bank`
 * radians about its own length (view/bank). The lean turns the hull about its centre, so the
 * ship stays at the one render height.
 */
export function placeShip(
  meshes: ShipMeshes,
  x: number,
  z: number,
  heading: number,
  bank = 0,
): void {
  for (const mesh of [meshes.hull, meshes.silhouette]) {
    mesh.position.set(x, RENDER_HEIGHT, z);
    // Yaw first, then roll about the ship's own +x, its length.
    mesh.rotation.set(bank, -heading, 0, "YXZ");
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
