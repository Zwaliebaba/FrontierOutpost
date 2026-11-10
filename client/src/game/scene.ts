/**
 * The expedition's scene graph: landmarks, ships, the lock bracket and shots, drawn by the
 * readability rules in view/readability. It needs no WebGL, so the rules are tested here.
 */
import {
  BoxGeometry,
  DirectionalLight,
  HemisphereLight,
  IcosahedronGeometry,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  Scene,
} from "three";
import type { Group } from "three";
import type { EntityKind, ObstacleInfo } from "../protocol";
import type { EntityPose } from "../net/interpolation";
import type { Shots } from "./shots";
import { SHIP_HEIGHT } from "../view/hulls";
import { between, createRng } from "../view/rng";
import {
  BOLT,
  STRUCTURE_STENCIL,
  linear,
  lockBracket,
  placeShip,
  shipMeshes,
} from "../view/readability";
import type { Allegiance, ShipMeshes } from "../view/readability";

/**
 * Each kind's collision radius over the starter hull's (frontier_sim::profile), so a drawn
 * hull is as large as the ship the server collides.
 */
export const SIZE: Readonly<Record<EntityKind, number>> = {
  playerShip: 1,
  drone: 0.9 / 1.3,
  raider: 1,
  warden: 2.2 / 1.3,
};

const BLAST = linear(1, 0.6, 0.2);
const LANDMARK_SEED = 2266;

/** Other pilots are allies; every NPC is hostile in Stage 1. */
export function allegianceOf(kind: EntityKind, own: boolean): Allegiance {
  if (kind !== "playerShip") return "hostile";
  return own ? "self" : "ally";
}

interface DrawnShip extends ShipMeshes {
  allegiance: Allegiance;
  kind: EntityKind;
}

export class ExpeditionScene {
  readonly scene = new Scene();
  readonly #ships = new Map<number, DrawnShip>();
  readonly #bracket: Group = lockBracket();
  readonly #bolts: Mesh<BoxGeometry, MeshBasicMaterial>[] = [];
  readonly #blasts: Mesh<IcosahedronGeometry, MeshBasicMaterial>[] = [];
  readonly #obstacles: Mesh[] = [];
  #ownShipId: number;

  constructor(ownShipId: number) {
    this.#ownShipId = ownShipId;
    this.scene.add(new HemisphereLight(linear(0.3, 0.48, 1), linear(0.025, 0.038, 0.055), 0.6));
    const starlight = new DirectionalLight(linear(0.67, 0.77, 1), 2.4);
    starlight.position.set(-4, 10, 6);
    this.scene.add(starlight);
    this.#bracket.visible = false;
    this.scene.add(this.#bracket);
  }

  /** The player's ship after a relaunch; ships are redrawn if their allegiance changed. */
  setOwnShip(id: number): void {
    this.#ownShipId = id;
  }

  /** The drawn meshes for a ship, for tests and picking. */
  ship(id: number): Readonly<DrawnShip> | undefined {
    return this.#ships.get(id);
  }

  get bracket(): Group {
    return this.#bracket;
  }

  get obstacles(): readonly Mesh[] {
    return this.#obstacles;
  }

  /** Moves every ship to its pose, brackets the locked one, and draws the shots. */
  update(poses: readonly EntityPose[], lock: number | null, shots?: Shots): void {
    const seen = new Set<number>();
    for (const pose of poses) {
      seen.add(pose.id);
      const allegiance = allegianceOf(pose.kind, pose.id === this.#ownShipId);
      let drawn = this.#ships.get(pose.id);
      if (drawn && drawn.allegiance !== allegiance) {
        this.#remove(pose.id, drawn);
        drawn = undefined;
      }
      if (!drawn) {
        drawn = { ...shipMeshes(allegiance, SIZE[pose.kind]), allegiance, kind: pose.kind };
        this.#ships.set(pose.id, drawn);
        this.scene.add(drawn.hull, drawn.silhouette);
      }
      placeShip(drawn, pose.x, pose.z, pose.heading);
    }
    for (const [id, drawn] of this.#ships) {
      if (!seen.has(id)) this.#remove(id, drawn);
    }

    const locked = lock === null ? undefined : this.#ships.get(lock);
    this.#bracket.visible = locked !== undefined;
    if (locked) {
      // Square to the world, not the hull, so the bracket holds still while its target turns.
      this.#bracket.position.copy(locked.hull.position);
      this.#bracket.scale.setScalar(SIZE[locked.kind]);
    }

    if (shots) this.#drawShots(shots);
  }

  #remove(id: number, drawn: DrawnShip): void {
    this.scene.remove(drawn.hull, drawn.silhouette);
    // Geometry is shared between ships of one shape; only the materials are this ship's.
    drawn.hull.material.dispose();
    drawn.silhouette.material.dispose();
    this.#ships.delete(id);
  }

  /** Bolts keep the style guide's rule for effects: a bright core in a coloured edge. */
  #drawShots(shots: Shots): void {
    const bolts = shots.bolts;
    while (this.#bolts.length < bolts.length) {
      const edge = new Mesh(
        new BoxGeometry(1.1, 0.26, 0.26),
        new MeshBasicMaterial({ transparent: true, opacity: 0.6 }),
      );
      edge.add(new Mesh(new BoxGeometry(0.9, 0.1, 0.1), new MeshBasicMaterial()));
      this.scene.add(edge);
      this.#bolts.push(edge);
    }
    this.#bolts.forEach((mesh, index) => {
      const bolt = bolts[index];
      mesh.visible = bolt !== undefined;
      if (!bolt) return;
      const shooter = this.#ships.get(bolt.shooter);
      const hostile = shooter !== undefined && shooter.allegiance === "hostile";
      mesh.material.color.copy(hostile ? BOLT.hostile : BOLT.pilot);
      mesh.position.set(bolt.x, SHIP_HEIGHT / 2, bolt.z);
      mesh.rotation.set(0, -bolt.heading, 0);
    });

    const blasts = shots.blasts;
    while (this.#blasts.length < blasts.length) {
      const mesh = new Mesh(
        new IcosahedronGeometry(1, 1),
        new MeshBasicMaterial({ color: BLAST, transparent: true }),
      );
      this.scene.add(mesh);
      this.#blasts.push(mesh);
    }
    this.#blasts.forEach((mesh, index) => {
      const blast = blasts[index];
      mesh.visible = blast !== undefined;
      if (!blast) return;
      mesh.position.set(blast.x, SHIP_HEIGHT / 2, blast.z);
      mesh.scale.setScalar(1 + blast.age * 3);
      mesh.material.opacity = 1 - blast.age;
    });
  }

  /**
   * Draws the expedition's obstacles. They come from the server, which collides ships with them;
   * only their tilt and depth below the plane are decoration. They are structures: a ship behind
   * one shows as a silhouette.
   */
  setObstacles(obstacles: readonly ObstacleInfo[]): void {
    const rng = createRng(LANDMARK_SEED);
    const material = new MeshStandardMaterial({
      color: linear(0.1, 0.12, 0.15),
      roughness: 0.95,
      flatShading: true,
      ...STRUCTURE_STENCIL,
    });
    for (const obstacle of obstacles) {
      const rock = new Mesh(new IcosahedronGeometry(obstacle.radius, 1), material);
      rock.position.set(obstacle.x, between(rng, -obstacle.radius, 0) * 0.5, obstacle.z);
      rock.rotation.set(between(rng, 0, 6), between(rng, 0, 6), 0);
      this.scene.add(rock);
      this.#obstacles.push(rock);
    }
  }
}
