/**
 * The expedition as the player sees it: star backdrop, the scene from ExpeditionScene and the
 * tactical camera with the greybox's defaults (design §5).
 */
import {
  BufferAttribute,
  BufferGeometry,
  PerspectiveCamera,
  Points,
  PointsMaterial,
  Scene,
  WebGLRenderer,
} from "three";
import type { Vector3 } from "three";
import type { ObstacleInfo } from "../protocol";
import type { EntityPose } from "../net/interpolation";
import type { Shots } from "./shots";
import { ExpeditionScene } from "./scene";
import { linear } from "../view/readability";
import { createStarField } from "../view/sky";
import { aimRig, createRig } from "../view/view";
import type { CameraRig } from "../view/view";

export const RESTING_ELEVATION = 33;

const SKY_FOV = 50;
const STAR_SEED = 2266;

export class ExpeditionView {
  readonly canvas: HTMLCanvasElement;
  readonly world: ExpeditionScene;
  readonly #renderer: WebGLRenderer;
  readonly #sky = new Scene();
  readonly #skyCamera = new PerspectiveCamera(SKY_FOV, 1, 0.1, 10);
  #rig: CameraRig;
  #width = 1;
  #height = 1;
  #viewWidth: number;

  constructor(container: HTMLElement, ownShipId: number, viewWidth: number) {
    this.world = new ExpeditionScene(ownShipId);
    this.#viewWidth = viewWidth;
    // Silhouettes behind structures need a stencil buffer.
    this.#renderer = new WebGLRenderer({ antialias: true, stencil: true });
    this.#renderer.setPixelRatio(window.devicePixelRatio);
    this.#renderer.autoClear = false;
    this.#renderer.setClearColor(linear(0.006, 0.009, 0.02));
    this.canvas = this.#renderer.domElement;
    container.prepend(this.canvas);
    this.#rig = createRig(1, viewWidth, "ortho");
    this.#addStars();
  }

  /** The player's ship after a relaunch, drawn as their own. */
  setOwnShip(id: number): void {
    this.world.setOwnShip(id);
  }

  setObstacles(obstacles: readonly ObstacleInfo[]): void {
    this.world.setObstacles(obstacles);
  }

  get camera(): CameraRig["camera"] {
    return this.#rig.camera;
  }

  get viewport(): { width: number; height: number } {
    return { width: this.#width, height: this.#height };
  }

  resize(width: number, height: number, viewWidth = this.#viewWidth): void {
    this.#width = width;
    this.#height = height;
    this.#viewWidth = viewWidth;
    this.#renderer.setSize(width, height);
    this.#rig = createRig(width / height, viewWidth, "ortho");
    this.#skyCamera.aspect = width / height;
    this.#skyCamera.updateProjectionMatrix();
  }

  /**
   * Draws `poses`, the bracket on `lock`, and the bolts and blasts in `shots`, with the camera
   * centred on `target`, turned by `yaw` and raised to `pitch`.
   */
  render(
    poses: readonly EntityPose[],
    lock: number | null,
    target: Vector3,
    pitch: number,
    yaw: number,
    shots?: Shots,
  ): void {
    this.world.update(poses, lock, shots);
    aimRig(this.#rig, target, pitch, yaw);
    this.#renderer.clear();
    this.#skyCamera.quaternion.copy(this.#rig.camera.quaternion);
    this.#renderer.render(this.#sky, this.#skyCamera);
    this.#renderer.clearDepth();
    this.#renderer.render(this.world.scene, this.#rig.camera);
  }

  #addStars(): void {
    const layers = [
      { count: 3200, size: 1.3, dim: 0.75 },
      { count: 700, size: 2.2, dim: 1 },
      { count: 130, size: 3.4, dim: 1.5 },
    ];
    layers.forEach(({ count, size, dim }, layer) => {
      const field = createStarField(STAR_SEED + layer, count);
      this.#sky.add(
        new Points(
          new BufferGeometry()
            .setAttribute("position", new BufferAttribute(field.directions, 3))
            .setAttribute(
              "color",
              new BufferAttribute(
                field.colours.map((c) => c * dim),
                3,
              ),
            ),
          new PointsMaterial({
            size,
            sizeAttenuation: false,
            vertexColors: true,
            depthTest: false,
          }),
        ),
      );
    });
  }
}
