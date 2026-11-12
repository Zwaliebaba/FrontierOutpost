/**
 * The expedition's scene graph: landmarks, ships, the lock bracket and shots, drawn by the
 * readability rules in view/readability. It needs no WebGL, so the rules are tested here.
 */
import {
  BoxGeometry,
  CylinderGeometry,
  EdgesGeometry,
  Group,
  IcosahedronGeometry,
  LineBasicMaterial,
  LineSegments,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  OctahedronGeometry,
  Scene,
  TorusGeometry,
  Vector3,
} from "three";
import type { Material as ThreeMaterial, Object3D } from "three";
import type { DepositInfo, EntityKind, Material, ObstacleInfo, SiteInfo } from "../protocol";
import type { EntityPose } from "../net/interpolation";
import type { Shots } from "./shots";
import { SHIP_HEIGHT } from "../view/hulls";
import { stepBank } from "../view/bank";
import { setGear, stepGear } from "../view/frigate";
import { stationLights } from "../view/lights";
import { between, createRng } from "../view/rng";
import {
  BOLT,
  RENDER_HEIGHT,
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
  core: 4 / 1.3,
};

const BLAST = linear(1, 0.6, 0.2);
const X_AXIS = new Vector3(1, 0, 0);
const LANDMARK_SEED = 2266;
/** The extractor beam's coloured edge, metres across: thinner than a bolt, which it is not. */
const BEAM_WIDTH = 0.2;
/** How high in a full deposit's crystals the beam ends, metres. */
const BEAM_END_HEIGHT = 0.6;

/**
 * Deposit crystals by material. None is orange or cyan, the signal colours, and each also has
 * its own shape count, so material never rests on colour alone.
 */
const DEPOSIT: Readonly<Record<Material, { colour: ReturnType<typeof linear>; crystals: number }>> =
  {
    ore: { colour: linear(0.62, 0.5, 0.26), crystals: 3 },
    salvage: { colour: linear(0.42, 0.44, 0.46), crystals: 2 },
    volatiles: { colour: linear(0.45, 0.22, 0.62), crystals: 4 },
  };
const BEACON = linear(0.55, 0.62, 0.7);
const STATION = linear(0.095, 0.135, 0.19);

interface DrawnDeposit {
  group: Group;
  units: number;
  material: MeshStandardMaterial;
}

/** Other pilots are allies; every NPC is hostile in Stage 1. */
export function allegianceOf(kind: EntityKind, own: boolean): Allegiance {
  if (kind !== "playerShip") return "hostile";
  return own ? "self" : "ally";
}

interface DrawnShip extends ShipMeshes {
  allegiance: Allegiance;
  kind: EntityKind;
  /** The landing gear, 0 folded to 1 lowered. */
  lowered: number;
  /** The heading it was last drawn at, and its lean into the turn (view/bank). */
  heading: number;
  bank: number;
}

/** A ship this close to the dock is on berth 07, so its gear is down, metres. */
export const ON_BERTH = 1.2;

export class ExpeditionScene {
  readonly scene = new Scene();
  readonly #ships = new Map<number, DrawnShip>();
  readonly #bracket: Group = lockBracket();
  readonly #bolts: Mesh<BoxGeometry, MeshBasicMaterial>[] = [];
  readonly #blasts: Mesh<IcosahedronGeometry, MeshBasicMaterial>[] = [];
  readonly #obstacles: Mesh[] = [];
  readonly #site = new Group();
  readonly #pockets: Group[] = [];
  readonly #deposits: DrawnDeposit[] = [];
  /** Holds the outpost's station at the dock; not cleared with the site, which it outlives. */
  readonly #dock = new Group();
  /** The extractor's beam from the player's ship to the deposit it works; one at a time. */
  readonly #beam: Mesh<BoxGeometry, MeshBasicMaterial>;
  #station: Object3D | null = null;
  #dockAt: { x: number; z: number } | null = null;
  #ownShipId: number;

  constructor(ownShipId: number) {
    this.#ownShipId = ownShipId;
    this.scene.add(...stationLights());
    this.#bracket.visible = false;
    this.#dock.name = "dock";
    // The style guide's rule for effects: a bright core in a coloured edge.
    this.#beam = new Mesh(
      new BoxGeometry(1, BEAM_WIDTH, BEAM_WIDTH),
      new MeshBasicMaterial({ transparent: true, opacity: 0.5 }),
    );
    this.#beam.add(
      new Mesh(new BoxGeometry(1, BEAM_WIDTH / 3, BEAM_WIDTH / 3), new MeshBasicMaterial()),
    );
    this.#beam.name = "extractor beam";
    this.#beam.visible = false;
    this.scene.add(this.#bracket, this.#site, this.#dock, this.#beam);
  }

  get beam(): Readonly<Mesh> {
    return this.#beam;
  }

  /**
   * Draws the extractor's beam from `from`, the player's ship, to the deposit numbered `deposit`
   * in the site's list, or hides it with `null`. Playtesting found extraction gave no sign in
   * the world that anything was happening; the beam shows which deposit is being worked.
   */
  setBeam(from: { x: number; z: number } | null, deposit: number | null): void {
    const drawn = deposit === null ? undefined : this.#deposits[deposit];
    if (!from || !drawn) {
      this.#beam.visible = false;
      return;
    }
    // From the keel, so the hull hides the beam beneath it and the beam leaves from under the
    // ship's edge. Started at bolt height it lay along the roof, drawn across the hull.
    const start = new Vector3(from.x, RENDER_HEIGHT - SHIP_HEIGHT / 2, from.z);
    // Into the crystals, which shrink as the deposit empties.
    const end = drawn.group.position.clone().setY(BEAM_END_HEIGHT * drawn.group.scale.y);
    const length = start.distanceTo(end);
    if (length < 1e-3) {
      this.#beam.visible = false;
      return;
    }
    this.#beam.visible = true;
    this.#beam.material.color.copy(drawn.material.color);
    this.#beam.position.copy(start).add(end).multiplyScalar(0.5);
    this.#beam.scale.set(length, 1, 1);
    this.#beam.quaternion.setFromUnitVectors(X_AXIS, end.sub(start).normalize());
  }

  /**
   * Draws the outpost's station at the dock instead of the stand-in, the same object the outpost
   * view shows, placed there already (`placeStation`). `null` takes it away again.
   */
  setStation(station: Object3D | null): void {
    if (this.#station) this.#dock.remove(this.#station);
    this.#station = station;
    if (station) this.#dock.add(station);
    const standIn = this.#site.getObjectByName("stand-in dock");
    if (standIn) standIn.visible = station === null;
  }

  /** Drops every ship's drawing, so the next update draws them anew (a hull model loaded). */
  redraw(): void {
    for (const [id, drawn] of this.#ships) this.#remove(id, drawn);
  }

  /** Drops every ship: a new run's instance numbers its ships afresh. */
  reset(ownShipId: number): void {
    for (const [id, drawn] of this.#ships) this.#remove(id, drawn);
    this.#ownShipId = ownShipId;
    this.#bracket.visible = false;
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

  /**
   * Moves every ship to its pose, brackets the locked one, and draws the shots. `dt`, seconds
   * since the last update, paces the landing gear.
   */
  update(poses: readonly EntityPose[], lock: number | null, shots?: Shots, dt = 0): void {
    const seen = new Set<number>();
    for (const pose of poses) {
      seen.add(pose.id);
      const allegiance = allegianceOf(pose.kind, pose.id === this.#ownShipId);
      let drawn = this.#ships.get(pose.id);
      if (drawn && (drawn.allegiance !== allegiance || drawn.kind !== pose.kind)) {
        this.#remove(pose.id, drawn);
        drawn = undefined;
      }
      // Gear is down only on the berth: it folds as a launching ship lifts off.
      const dock = this.#dockAt;
      const berthed = dock !== null && Math.hypot(pose.x - dock.x, pose.z - dock.z) <= ON_BERTH;
      const target = berthed ? 1 : 0;
      if (!drawn) {
        const meshes = shipMeshes(allegiance, SIZE[pose.kind]);
        drawn = {
          ...meshes,
          allegiance,
          kind: pose.kind,
          lowered: target,
          heading: pose.heading,
          bank: 0,
        };
        this.#ships.set(pose.id, drawn);
        this.scene.add(drawn.hull, drawn.silhouette);
      } else {
        drawn.lowered = stepGear(drawn.lowered, target, dt);
        // Structures never move; everything that flies leans into its turns.
        if (pose.kind !== "core")
          drawn.bank = stepBank(drawn.bank, drawn.heading, pose.heading, dt);
        drawn.heading = pose.heading;
      }
      setGear(drawn.gear, drawn.lowered);
      placeShip(drawn, pose.x, pose.z, pose.heading, drawn.bank);
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
    drawn.dispose();
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

  get pockets(): readonly Group[] {
    return this.#pockets;
  }

  /** Scale of each deposit's crystals, for tests. */
  get depositScales(): number[] {
    return this.#deposits.map((deposit) => deposit.group.scale.x);
  }

  /**
   * Draws a run's site in place of the last one: its obstacles, dock, pocket beacons and
   * deposits. Obstacles come from the server, which collides ships with them; only their tilt
   * and depth below the plane are decoration. They and the dock are structures: a ship behind
   * one shows as a silhouette.
   */
  setSite(site: SiteInfo, obstacles: readonly ObstacleInfo[], deposits: readonly DepositInfo[]) {
    this.#clearSite();
    this.#dockAt = { x: site.dockX, z: site.dockZ };
    const rng = createRng(LANDMARK_SEED);
    const rockMaterial = new MeshStandardMaterial({
      color: linear(0.1, 0.12, 0.15),
      roughness: 0.95,
      flatShading: true,
      ...STRUCTURE_STENCIL,
    });
    for (const obstacle of obstacles) {
      const rock = new Mesh(new IcosahedronGeometry(obstacle.radius, 1), rockMaterial);
      rock.position.set(obstacle.x, between(rng, -obstacle.radius, 0) * 0.5, obstacle.z);
      rock.rotation.set(between(rng, 0, 6), between(rng, 0, 6), 0);
      this.#site.add(rock);
      this.#obstacles.push(rock);
    }
    // Drawn while the station's models load, or if they never do.
    const standIn = station(site.dockX, site.dockZ);
    standIn.visible = this.#station === null;
    this.#site.add(standIn);
    for (const pocket of site.pockets) {
      const beacon = pocketBeacon(pocket.locked);
      beacon.position.set(pocket.x, 0, pocket.z);
      beacon.name = pocket.kind;
      this.#site.add(beacon);
      this.#pockets.push(beacon);
    }
    for (const deposit of deposits) {
      const look = DEPOSIT[deposit.material];
      const material = new MeshStandardMaterial({
        color: look.colour,
        emissive: look.colour,
        emissiveIntensity: 0.35,
        roughness: 0.4,
        flatShading: true,
      });
      const group = new Group();
      for (let i = 0; i < look.crystals; i += 1) {
        const crystal = new Mesh(new OctahedronGeometry(0.9 - i * 0.12), material);
        const angle = (i / look.crystals) * Math.PI * 2;
        crystal.position.set(Math.cos(angle) * 0.9, 0.2 + i * 0.25, Math.sin(angle) * 0.9);
        crystal.scale.set(0.7, 1.6, 0.7);
        group.add(crystal);
      }
      group.position.set(deposit.x, 0, deposit.z);
      this.#site.add(group);
      this.#deposits.push({ group, units: deposit.units, material });
    }
  }

  /** Shrinks each deposit's crystals with what is left in it; an empty one goes dark. */
  setDeposits(units: readonly number[]): void {
    this.#deposits.forEach((deposit, index) => {
      const left = units[index] ?? deposit.units;
      const share = deposit.units > 0 ? Math.min(1, left / deposit.units) : 0;
      deposit.group.scale.setScalar(left > 0 ? 0.45 + 0.55 * share : 0.35);
      deposit.material.emissiveIntensity = left > 0 ? 0.35 : 0;
    });
  }

  #clearSite(): void {
    const materials = new Set<ThreeMaterial>();
    this.#site.traverse((node: Object3D) => {
      if (node instanceof Mesh || node instanceof LineSegments) {
        (node.geometry as { dispose(): void }).dispose();
        const material = node.material as ThreeMaterial | ThreeMaterial[];
        for (const m of Array.isArray(material) ? material : [material]) materials.add(m);
      }
    });
    for (const material of materials) material.dispose();
    this.#site.clear();
    this.#obstacles.length = 0;
    this.#pockets.length = 0;
    this.#deposits.length = 0;
    this.#beam.visible = false;
  }
}

/** The pilots' home berth: a hub and a docking ring, drawn as a structure. */
function station(x: number, z: number): Group {
  const material = new MeshStandardMaterial({
    color: STATION,
    roughness: 0.5,
    metalness: 0.6,
    flatShading: true,
    ...STRUCTURE_STENCIL,
  });
  const group = new Group();
  const hub = new Mesh(new CylinderGeometry(4, 5, 6, 12), material);
  hub.position.y = -3.5;
  const ring = new Mesh(new TorusGeometry(10, 0.8, 8, 32).rotateX(Math.PI / 2), material);
  ring.position.y = -1.5;
  group.add(hub, ring);
  group.position.set(x, 0, z);
  group.name = "stand-in dock";
  return group;
}

/**
 * A pocket's beacon: a mast topped with a crystal when the pocket is open, and with a cage when
 * it is locked, so its state reads by shape as well as by its label.
 */
function pocketBeacon(locked: boolean): Group {
  const material = new MeshBasicMaterial({ color: BEACON });
  const group = new Group();
  const mast = new Mesh(new CylinderGeometry(0.25, 0.35, 8, 6), material);
  mast.position.y = 4;
  group.add(mast);
  if (locked) {
    const cage = new LineSegments(
      new EdgesGeometry(new BoxGeometry(2.6, 2.6, 2.6)),
      new LineBasicMaterial({ color: BEACON }),
    );
    cage.position.y = 9.3;
    cage.name = "locked";
    group.add(cage);
  } else {
    const top = new Mesh(new OctahedronGeometry(1.4), material);
    top.position.y = 9.4;
    top.name = "open";
    group.add(top);
  }
  return group;
}
