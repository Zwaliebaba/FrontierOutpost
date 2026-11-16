import { Mesh, Vector3 } from "three";
import type { BufferGeometry, Object3D } from "three";
import { describe, expect, it } from "vitest";
import contract from "../../../Design/Art/contract.json";
import exteriorGlb from "../../../Design/Art/station/exterior/exterior.glb?inline";
import interiorGlb from "../../../Design/Art/station/interior/interior.glb?inline";
import { RESTING_ELEVATION } from "../game/render";
import { aimRig, createRig } from "../view/view";
import { HOME_ENVELOPE, homeViewWidth } from "./framing";
import { indexStation, parseGlb } from "./models";
import { placeStation } from "./placement";

const bytes = (url: string): ArrayBuffer =>
  Uint8Array.from(atob(url.slice(url.indexOf(",") + 1)), (c) => c.charCodeAt(0)).buffer;

const station = indexStation(
  await parseGlb(bytes(exteriorGlb)),
  await parseGlb(bytes(interiorGlb)),
);

/** The farthest any vertex of `root` reaches from the screen's centre, metres across and up. */
function reach(root: Object3D): { across: number; up: number } {
  const rig = createRig(16 / 9, 40, "ortho");
  aimRig(rig, new Vector3(0, 0, 0), RESTING_ELEVATION, 0);
  let [across, up] = [0, 0];
  const point = new Vector3();
  for (let step = 0; step < 16; step++) {
    const angle = (step / 16) * Math.PI * 2;
    const placement = placeStation({
      centreX: 0,
      centreZ: 0,
      forwardX: Math.cos(angle),
      forwardZ: Math.sin(angle),
    });
    root.position.set(placement.x, placement.y, placement.z);
    root.rotation.set(0, placement.yaw, 0);
    root.updateMatrixWorld(true);
    root.traverse((node) => {
      if (!(node instanceof Mesh)) return;
      const position = (node.geometry as BufferGeometry).getAttribute("position");
      for (let index = 0; index < position.count; index++) {
        point
          .fromBufferAttribute(position, index)
          .applyMatrix4(node.matrixWorld)
          .applyMatrix4(rig.camera.matrixWorldInverse);
        across = Math.max(across, Math.abs(point.x));
        up = Math.max(up, Math.abs(point.y));
      }
    });
  }
  return { across, up };
}

describe("the home's framing", () => {
  it("holds the exterior and the interior turned every way, with the largest tell", () => {
    for (const root of [station.exterior, station.interior]) {
      const { across, up } = reach(root);
      expect(across + contract.tell.cube_size).toBeLessThanOrEqual(HOME_ENVELOPE.across / 2);
      expect(up + contract.tell.cube_size).toBeLessThanOrEqual(HOME_ENVELOPE.up / 2);
    }
  });

  it("fits the envelope in the gap between the panels and between the bars", () => {
    // 1280×720 with the panels: the gap is the narrower limit.
    expect(homeViewWidth(1280, 720, 586)).toBeCloseTo((HOME_ENVELOPE.across * 1280) / 586, 6);
    // A short window: the height is.
    expect(homeViewWidth(1920, 600, 1226)).toBeCloseTo((HOME_ENVELOPE.up * 1920) / 450, 6);
    // The station's metres per pixel never shrink below what fits both.
    for (const [w, h, gap] of [
      [1280, 720, 586],
      [1920, 1080, 1226],
      [1024, 768, 470],
    ] as const) {
      const width = homeViewWidth(w, h, gap);
      expect((HOME_ENVELOPE.across * w) / width).toBeLessThanOrEqual(gap + 1e-9);
      expect((HOME_ENVELOPE.up * w) / width).toBeLessThanOrEqual(h - 150 + 1e-9);
    }
  });
});
