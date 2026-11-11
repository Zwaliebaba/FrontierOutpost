import { describe, expect, it } from "vitest";
import { AlwaysStencilFunc, EqualStencilFunc, GreaterDepth, Mesh, ReplaceStencilOp } from "three";
import type { MeshBasicMaterial } from "three";
import {
  HULL,
  RENDER_HEIGHT,
  SIGNAL,
  STRUCTURE,
  STRUCTURE_STENCIL,
  hullGeometry,
  hullMaterial,
  hullShape,
  lockBracket,
  placeShip,
  shipMeshes,
  silhouetteMaterial,
} from "./readability";
import type { Allegiance } from "./readability";

const ALLEGIANCES: Allegiance[] = ["self", "ally", "hostile"];
const luminance = ({ r, g, b }: { r: number; g: number; b: number }) =>
  0.2126 * r + 0.7152 * g + 0.0722 * b;

describe("allegiance", () => {
  it("separates hostiles from the player's side by shape, not only colour", () => {
    expect(hullShape("hostile")).not.toBe(hullShape("ally"));
    expect(hullShape("self")).toBe(hullShape("ally"));
    expect(hullGeometry("hostile")).not.toBe(hullGeometry("ally"));
  });

  it("gives each allegiance its own hull tone and signal colour", () => {
    for (const a of ALLEGIANCES) {
      for (const b of ALLEGIANCES) {
        if (a === b) continue;
        expect(HULL[a].equals(HULL[b])).toBe(false);
        expect(SIGNAL[a].equals(SIGNAL[b])).toBe(false);
      }
    }
  });

  it("keeps hull tones apart in greyscale, own ship lightest", () => {
    const [self, ally, hostile] = ALLEGIANCES.map((a) => luminance(HULL[a]));
    expect(self).toBeGreaterThan(ally ?? Infinity);
    expect(ally).toBeGreaterThan((hostile ?? Infinity) * 1.5);
  });
});

describe("silhouettes", () => {
  it("lets structures mark the pixels they own", () => {
    expect(STRUCTURE_STENCIL).toMatchObject({
      stencilWrite: true,
      stencilRef: STRUCTURE,
      stencilFunc: AlwaysStencilFunc,
      stencilZPass: ReplaceStencilOp,
    });
  });

  it("clears the mark where a hull is the nearest surface", () => {
    const material = hullMaterial("hostile");
    expect(material.stencilWrite).toBe(true);
    expect(material.stencilRef).toBe(0);
    expect(material.stencilZPass).toBe(ReplaceStencilOp);
  });

  it("draws only behind a structure, in the allegiance's signal colour", () => {
    const material = silhouetteMaterial("ally");
    expect(material.depthFunc).toBe(GreaterDepth);
    expect(material.depthWrite).toBe(false);
    expect(material.stencilFunc).toBe(EqualStencilFunc);
    expect(material.stencilRef).toBe(STRUCTURE);
    expect(material.color.equals(SIGNAL.ally)).toBe(true);
  });

  it("renders after hulls and structures, sharing the hull's shape and scale", () => {
    const { hull, silhouette } = shipMeshes("hostile", 1.7);
    expect(silhouette.renderOrder).toBeGreaterThan(hull.renderOrder);
    expect(silhouette.geometry).toBe(hull.geometry);
    expect(silhouette.scale.x).toBe(1.7);
  });
});

describe("placement", () => {
  it("puts hull and silhouette at the one render height, facing the heading", () => {
    const meshes = shipMeshes("ally");
    placeShip(meshes, 12, -4, Math.PI / 2);
    for (const mesh of [meshes.hull, meshes.silhouette]) {
      expect(mesh.position.toArray()).toEqual([12, RENDER_HEIGHT, -4]);
      expect(mesh.rotation.y).toBeCloseTo(-Math.PI / 2);
    }
  });
});

describe("lock bracket", () => {
  it("draws over structures and is drawn last", () => {
    const bracket = lockBracket();
    expect(bracket.children).toHaveLength(8);
    bracket.traverse((node) => {
      expect(node.renderOrder).toBeGreaterThan(shipMeshes("ally").silhouette.renderOrder);
    });
  });

  it("is drawn in one pass, with the transparent objects it must cover", () => {
    const [strip] = lockBracket().children;
    if (!(strip instanceof Mesh)) throw new Error("the bracket is made of meshes");
    const material = strip.material as MeshBasicMaterial;
    // Transparent, so it sorts after every opaque object; double-sided, so it shows from
    // either side of the plane; and one pass, or three.js draws and re-keys it twice a frame.
    expect(material.transparent).toBe(true);
    expect(material.depthTest).toBe(false);
    expect(material.forceSinglePass).toBe(true);
  });
});
