/**
 * The expedition as the player sees it: star backdrop, landmarks, ships and the tactical
 * camera with the greybox's defaults (design §5).
 */
import {
  BufferAttribute,
  BufferGeometry,
  Color,
  DirectionalLight,
  HemisphereLight,
  IcosahedronGeometry,
  LinearSRGBColorSpace,
  Mesh,
  MeshStandardMaterial,
  PerspectiveCamera,
  Points,
  PointsMaterial,
  Scene,
  Vector3,
  WebGLRenderer,
} from "three";
import type { EntityKind, ObstacleInfo } from "../protocol";
import type { EntityPose } from "../net/interpolation";
import { forkedHull, wedgeHull } from "../view/hulls";
import { between, createRng } from "../view/rng";
import { createStarField } from "../view/sky";
import { aimRig, createRig } from "../view/view";
import type { CameraRig } from "../view/view";

const linear = (r: number, g: number, b: number) =>
  new Color().setRGB(r, g, b, LinearSRGBColorSpace);

/** Hull tones from the style guide's material baselines; the player's own ship is lightest. */
const HULL = {
  own: linear(0.53, 0.57, 0.61),
  pilot: linear(0.29, 0.35, 0.43),
  hostile: linear(0.3, 0.11, 0.07),
};

export const RESTING_ELEVATION = 33;

/**
 * Each kind's collision radius over the starter hull's (frontier_sim::profile), so a drawn
 * hull is as large as the ship the server collides.
 */
const SIZE: Record<EntityKind, number> = {
  playerShip: 1,
  drone: 0.9 / 1.3,
  raider: 1,
  warden: 2.2 / 1.3,
};
const SKY_FOV = 50;
const LANDMARK_SEED = 2266;

export class ExpeditionView {
  readonly canvas: HTMLCanvasElement;
  readonly #renderer: WebGLRenderer;
  readonly #scene = new Scene();
  readonly #sky = new Scene();
  readonly #skyCamera = new PerspectiveCamera(SKY_FOV, 1, 0.1, 10);
  readonly #ships = new Map<number, Mesh>();
  readonly #ownShipId: number;
  #rig: CameraRig;
  #width = 1;
  #height = 1;
  #viewWidth: number;

  constructor(container: HTMLElement, ownShipId: number, viewWidth: number) {
    this.#ownShipId = ownShipId;
    this.#viewWidth = viewWidth;
    this.#renderer = new WebGLRenderer({ antialias: true });
    this.#renderer.setPixelRatio(window.devicePixelRatio);
    this.#renderer.autoClear = false;
    this.#renderer.setClearColor(linear(0.006, 0.009, 0.02));
    this.canvas = this.#renderer.domElement;
    container.prepend(this.canvas);
    this.#rig = createRig(1, viewWidth, "ortho");

    this.#scene.add(new HemisphereLight(linear(0.3, 0.48, 1), linear(0.025, 0.038, 0.055), 0.6));
    const starlight = new DirectionalLight(linear(0.67, 0.77, 1), 2.4);
    starlight.position.set(-4, 10, 6);
    this.#scene.add(starlight);
    this.#addStars();
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

  /** Draws `poses` with the camera centred on `target`, turned by `yaw` and raised to `pitch`. */
  render(poses: readonly EntityPose[], target: Vector3, pitch: number, yaw: number): void {
    const seen = new Set<number>();
    for (const pose of poses) {
      seen.add(pose.id);
      let mesh = this.#ships.get(pose.id);
      if (!mesh) {
        mesh = this.#shipMesh(pose.kind, pose.id === this.#ownShipId);
        this.#ships.set(pose.id, mesh);
        this.#scene.add(mesh);
      }
      // Every ship renders at one height (design §5); the hull model is centred on it.
      mesh.position.set(pose.x, 0, pose.z);
      mesh.rotation.set(0, -pose.heading, 0);
    }
    for (const [id, mesh] of this.#ships) {
      if (seen.has(id)) continue;
      this.#scene.remove(mesh);
      mesh.geometry.dispose();
      this.#ships.delete(id);
    }

    aimRig(this.#rig, target, pitch, yaw);
    this.#renderer.clear();
    this.#skyCamera.quaternion.copy(this.#rig.camera.quaternion);
    this.#renderer.render(this.#sky, this.#skyCamera);
    this.#renderer.clearDepth();
    this.#renderer.render(this.#scene, this.#rig.camera);
  }

  #shipMesh(kind: EntityKind, own: boolean): Mesh {
    const hostile = kind !== "playerShip";
    const color = hostile ? HULL.hostile : own ? HULL.own : HULL.pilot;
    const mesh = new Mesh(
      hostile ? forkedHull() : wedgeHull(),
      new MeshStandardMaterial({ color, roughness: 0.45, metalness: 0.5 }),
    );
    mesh.scale.setScalar(SIZE[kind]);
    return mesh;
  }

  #addStars(): void {
    const layers = [
      { count: 3200, size: 1.3, dim: 0.75 },
      { count: 700, size: 2.2, dim: 1 },
      { count: 130, size: 3.4, dim: 1.5 },
    ];
    layers.forEach(({ count, size, dim }, layer) => {
      const field = createStarField(LANDMARK_SEED + layer, count);
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

  /**
   * Draws the expedition's obstacles. They come from the server, which collides ships with them;
   * only their tilt and depth below the plane are decoration.
   */
  setObstacles(obstacles: readonly ObstacleInfo[]): void {
    const rng = createRng(LANDMARK_SEED);
    const material = new MeshStandardMaterial({
      color: linear(0.1, 0.12, 0.15),
      roughness: 0.95,
      flatShading: true,
    });
    for (const obstacle of obstacles) {
      const rock = new Mesh(new IcosahedronGeometry(obstacle.radius, 1), material);
      rock.position.set(obstacle.x, between(rng, -obstacle.radius, 0) * 0.5, obstacle.z);
      rock.rotation.set(between(rng, 0, 6), between(rng, 0, 6), 0);
      this.#scene.add(rock);
    }
  }
}
