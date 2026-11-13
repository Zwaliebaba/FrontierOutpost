/**
 * The expedition as the player sees it: star backdrop, the scene from ExpeditionScene and the
 * tactical camera with the greybox's defaults (design §5).
 */
import {
  AgXToneMapping,
  BufferAttribute,
  BufferGeometry,
  PerspectiveCamera,
  Points,
  PointsMaterial,
  Scene,
  WebGLRenderer,
} from "three";
import type { Vector3 } from "three";
import type { DepositInfo, ObstacleInfo, SiteInfo } from "../protocol";
import type { EntityPose } from "../net/interpolation";
import type { JumpEffects } from "./jumpEffects";
import type { Shots } from "./shots";
import { ExpeditionScene } from "./scene";
import { createDust, dustOpacity, placeDust } from "../view/dust";
import { linear } from "../view/readability";
import { createStarField } from "../view/sky";
import { softPoints, softPointsMaterial } from "../view/softPoints";
import { aimRig, createRig } from "../view/view";
import type { CameraRig } from "../view/view";

export const RESTING_ELEVATION = 33;

const SKY_FOV = 50;
const STAR_SEED = 2266;
const DUST_SEED = 4417;
/** A camera step longer than this in one frame is a jump's cut, not flight, metres. */
const DUST_CUT = 20;
/** How quickly the dust follows a change of speed, per second. */
const DUST_EASE = 10;

export class ExpeditionView {
  readonly canvas: HTMLCanvasElement;
  readonly world: ExpeditionScene;
  readonly #renderer: WebGLRenderer;
  readonly #sky = new Scene();
  readonly #skyCamera = new PerspectiveCamera(SKY_FOV, 1, 0.1, 10);
  /** Drawn in flight only, after the scene, so it shares the scene's depth (view/dust). */
  readonly #dust = new Scene();
  readonly #dustField = createDust(DUST_SEED);
  readonly #dustPositions = new BufferAttribute(new Float32Array(this.#dustField.homes.length), 3);
  readonly #dustUniforms = { pixelRatio: { value: 1 }, opacity: { value: dustOpacity(0) } };
  #dustAt: { x: number; z: number } | null = null;
  #dustVelocity = { x: 0, z: 0 };
  #rig: CameraRig;
  #width = 1;
  #height = 1;
  #viewWidth: number;
  /** The jump's lean in: a zoom over the view width the player chose (jumpEffects). */
  #punch = 1;

  constructor(container: HTMLElement, ownShipId: number, viewWidth: number) {
    this.world = new ExpeditionScene(ownShipId);
    this.#viewWidth = viewWidth;
    // Silhouettes behind structures need a stencil buffer.
    this.#renderer = new WebGLRenderer({ antialias: true, stencil: true });
    this.#renderer.toneMapping = AgXToneMapping;
    this.#renderer.toneMappingExposure = 1;
    this.#renderer.setPixelRatio(window.devicePixelRatio);
    this.#renderer.autoClear = false;
    this.#renderer.setClearColor(linear(0.006, 0.009, 0.02));
    this.canvas = this.#renderer.domElement;
    container.prepend(this.canvas);
    this.#rig = createRig(1, viewWidth, "ortho");
    this.#addStars();
    this.#addDust();
    this.world.setPixelRatio(this.#renderer.getPixelRatio());
  }

  /** Leans the view in by `zoom` over the chosen width, for the pilot's own jump; 1 at rest. */
  setPunch(zoom: number): void {
    this.#punch = zoom;
  }

  /** The player's ship after a relaunch, drawn as their own. */
  setOwnShip(id: number): void {
    this.world.setOwnShip(id);
  }

  /** Draws a new run's site, dropping the last run's ships. */
  beginRun(
    ownShipId: number,
    site: SiteInfo,
    obstacles: readonly ObstacleInfo[],
    deposits: readonly DepositInfo[],
  ): void {
    this.world.reset(ownShipId);
    this.world.setSite(site, obstacles, deposits);
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
    this.#dustUniforms.pixelRatio.value = this.#renderer.getPixelRatio();
    this.#rig = createRig(width / height, viewWidth, "ortho");
    this.#skyCamera.aspect = width / height;
    this.#skyCamera.updateProjectionMatrix();
  }

  /**
   * Draws `poses`, the bracket on `lock`, the bolts and blasts in `shots` and the jumps in
   * `jumps`, with the camera centred on `target`, turned by `yaw` and raised to `pitch`. `dt`,
   * seconds since the last frame, paces the landing gear.
   */
  render(
    poses: readonly EntityPose[],
    lock: number | null,
    target: Vector3,
    pitch: number,
    yaw: number,
    shots?: Shots,
    dt = 0,
    jumps?: JumpEffects,
  ): void {
    this.world.update(poses, lock, shots, dt, jumps);
    if (this.#rig.camera.zoom !== this.#punch) {
      this.#rig.camera.zoom = this.#punch;
      this.#rig.camera.updateProjectionMatrix();
    }
    aimRig(this.#rig, target, pitch, yaw);
    this.#renderer.clear();
    this.#skyCamera.quaternion.copy(this.#rig.camera.quaternion);
    this.#renderer.render(this.#sky, this.#skyCamera);
    this.#renderer.clearDepth();
    this.#renderer.render(this.world.scene, this.#rig.camera);
    this.#drawDust(target, dt);
  }

  /** Draws another scene, the outpost, with the same camera rules and star backdrop. */
  renderScene(scene: Scene, target: Vector3, pitch: number, yaw: number): void {
    // Home has no jumps: a lean left over from a run's last frame must not stay.
    if (this.#rig.camera.zoom !== 1) {
      this.#rig.camera.zoom = 1;
      this.#rig.camera.updateProjectionMatrix();
    }
    aimRig(this.#rig, target, pitch, yaw);
    this.#renderer.clear();
    this.#skyCamera.quaternion.copy(this.#rig.camera.quaternion);
    this.#renderer.render(this.#sky, this.#skyCamera);
    this.#renderer.clearDepth();
    this.#renderer.render(scene, this.#rig.camera);
  }

  /** The dust round the camera, shown as much as the ship it follows is moving. */
  #drawDust(target: Vector3, dt: number): void {
    const last = this.#dustAt;
    this.#dustAt = { x: target.x, z: target.z };
    if (last && dt > 0) {
      const dx = target.x - last.x;
      const dz = target.z - last.z;
      const cut = Math.hypot(dx, dz) > DUST_CUT;
      const ease = Math.min(1, dt * DUST_EASE);
      const v = this.#dustVelocity;
      v.x = cut ? 0 : v.x + (dx / dt - v.x) * ease;
      v.z = cut ? 0 : v.z + (dz / dt - v.z) * ease;
    }
    placeDust(this.#dustField, target.x, target.z, this.#dustPositions.array as Float32Array);
    this.#dustPositions.needsUpdate = true;
    this.#dustUniforms.opacity.value = dustOpacity(
      Math.hypot(this.#dustVelocity.x, this.#dustVelocity.z),
    );
    this.#renderer.render(this.#dust, this.#rig.camera);
  }

  #addDust(): void {
    const { colours, sizes } = this.#dustField;
    this.#dustUniforms.pixelRatio.value = this.#renderer.getPixelRatio();
    const specks = softPoints(sizes.length, softPointsMaterial(this.#dustUniforms));
    specks.geometry
      .setAttribute("position", this.#dustPositions)
      .setAttribute("tint", new BufferAttribute(colours, 3))
      .setAttribute("size", new BufferAttribute(sizes, 1));
    this.#dust.add(specks);
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
