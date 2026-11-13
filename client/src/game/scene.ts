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
import type { Color, Material as ThreeMaterial, Object3D } from "three";
import type { DepositInfo, EntityKind, Material, ObstacleInfo, SiteInfo } from "../protocol";
import { berthPositions } from "../outpost/placement";
import type { EntityPose } from "../net/interpolation";
import { ExtractorBeam } from "./extractorBeam";
import type { Flash, JumpEffects } from "./jumpEffects";
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
/** The jump drive's light: the blue of propulsion, on the pilots' side of the palette. */
const JUMP = linear(0.25, 0.72, 1);
/**
 * The spool's two rings, metres from the hull: the steady one closes as the spool runs, the
 * other loops inward faster and faster.
 */
const CHARGE_RING = { far: 5.5, near: 1.8 };
const PULSE_RING = { far: 7.5, near: 1.5 };
/** How far a jump's streak reaches along the way the ship went, metres. */
const STREAK_LENGTH = 60;
/** A flash's ring, metres: it opens where a ship left and closes where one arrives. */
const FLASH_RING = { near: 1.5, far: 7.5 };
const LANDMARK_SEED = 2266;
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

/** A flat ring on the plane, one metre in radius: a coloured edge around a bright core. */
function effectRing(): Mesh<TorusGeometry, MeshBasicMaterial> {
  const ring = new Mesh(
    new TorusGeometry(1, 0.024, 6, 64).rotateX(Math.PI / 2),
    new MeshBasicMaterial({ toneMapped: false, color: JUMP, transparent: true, depthWrite: false }),
  );
  ring.add(
    new Mesh(
      new TorusGeometry(1, 0.008, 6, 64).rotateX(Math.PI / 2),
      new MeshBasicMaterial({ toneMapped: false, transparent: true, depthWrite: false }),
    ),
  );
  return ring;
}

/** Sets a ring's radius and the opacity of its edge and its core. */
function setRing(ring: Mesh<TorusGeometry, MeshBasicMaterial>, radius: number, opacity: number) {
  ring.scale.setScalar(radius);
  ring.material.opacity = opacity;
  const core = ring.children[0];
  if (core instanceof Mesh) (core.material as MeshBasicMaterial).opacity = opacity;
}

/** The spool on a ship: two rings around the hull. Nothing solid, so it is no shield. */
interface DrawnCharge {
  group: Group;
  steady: Mesh<TorusGeometry, MeshBasicMaterial>;
  pulse: Mesh<TorusGeometry, MeshBasicMaterial>;
}

/**
 * One end of a jump: a ring that opens or closes, a second that goes the other way, a point
 * of light at the middle and a streak along the way the ship went.
 */
interface DrawnFlash {
  group: Group;
  ring: Mesh<TorusGeometry, MeshBasicMaterial>;
  echo: Mesh<TorusGeometry, MeshBasicMaterial>;
  spark: Mesh<IcosahedronGeometry, MeshBasicMaterial>;
  streak: Mesh<BoxGeometry, MeshBasicMaterial>;
  core: Mesh<BoxGeometry, MeshBasicMaterial>;
}

/**
 * Draws one end of a jump at `flash.age`. Where the ship left, the ring opens and the streak
 * shoots out the way it went; where it arrives, the streak comes in behind it and the ring
 * closes on the hull.
 */
function drawFlash(drawn: DrawnFlash, flash: Flash): void {
  const leaving = flash.kind === "leave";
  const fade = 1 - flash.age;
  // Fast at first, then easing: most of the streak's reach comes in its first third.
  const reach = 1 - fade * fade * fade;
  drawn.group.position.set(flash.x, SHIP_HEIGHT / 2, flash.z);
  drawn.group.rotation.set(0, -flash.heading, 0);
  const span = FLASH_RING.far - FLASH_RING.near;
  const out = FLASH_RING.near + span * reach;
  const back = FLASH_RING.far - span * reach;
  setRing(drawn.ring, leaving ? out : back, fade);
  setRing(drawn.echo, (leaving ? back : out) * 0.6, 0.45 * fade);
  drawn.spark.scale.setScalar(Math.max(0.01, fade));
  drawn.spark.material.opacity = fade * fade;
  // No longer than the jump itself: a short hop's streak must not shoot past where it ends.
  const most = Math.min(STREAK_LENGTH, flash.distance);
  const length = Math.max(0.01, most * (leaving ? reach : 1 - reach));
  drawn.streak.scale.set(length, fade, fade);
  // Ahead of where the ship left; behind where it arrives.
  drawn.streak.position.set(((leaving ? 1 : -1) * length) / 2, 0, 0);
  drawn.streak.material.opacity = 0.55 * fade;
  drawn.core.material.opacity = fade;
}

const HIT_LIGHT = linear(0.8, 0.9, 1);

interface FeedbackMaterial {
  material: MeshStandardMaterial;
  emission: Color;
  intensity: number;
}

/** Clone once per ship: flashing one hull must not change its crewmates' shared materials. */
function feedbackMaterials(root: Object3D): FeedbackMaterial[] {
  const copies = new Map<MeshStandardMaterial, MeshStandardMaterial>();
  root.traverse((node) => {
    if (!(node instanceof Mesh)) return;
    const copy = (material: ThreeMaterial): ThreeMaterial => {
      if (!(material instanceof MeshStandardMaterial)) return material;
      let owned = copies.get(material);
      if (!owned) {
        owned = material.clone();
        copies.set(material, owned);
      }
      return owned;
    };
    node.material = Array.isArray(node.material)
      ? node.material.map(copy)
      : copy(node.material as ThreeMaterial);
  });
  return [...copies.values()].map((material) => ({
    material,
    emission: material.emissive.clone(),
    intensity: material.emissiveIntensity,
  }));
}

interface DrawnShip extends ShipMeshes {
  health: number;
  shield: number;
  hit: number;
  feedback: FeedbackMaterial[];
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
  readonly #charge: DrawnCharge;
  readonly #flashes: DrawnFlash[] = [];
  readonly #crewCharges: DrawnCharge[] = [];
  readonly #obstacles: Mesh[] = [];
  readonly #site = new Group();
  readonly #pockets: Group[] = [];
  readonly #deposits: DrawnDeposit[] = [];
  /** Holds the outpost's station at the dock; not cleared with the site, which it outlives. */
  readonly #dock = new Group();
  /** The extractor's beam from the player's ship to the deposit it works; one at a time. */
  readonly #extractor = new ExtractorBeam();
  #station: Object3D | null = null;
  #berths: { x: number; z: number }[] = [];
  #ownShipId: number;
  #feedback = true;
  #shield = 0;

  constructor(ownShipId: number) {
    this.#ownShipId = ownShipId;
    this.scene.add(...stationLights());
    this.#bracket.visible = false;
    this.#dock.name = "dock";
    this.#charge = { group: new Group(), steady: effectRing(), pulse: effectRing() };
    this.#charge.group.add(this.#charge.steady, this.#charge.pulse);
    this.#charge.group.name = "jump spool";
    this.#charge.group.visible = false;
    this.scene.add(
      this.#bracket,
      this.#site,
      this.#dock,
      this.#extractor.beam,
      this.#extractor.fragments,
      this.#charge.group,
    );
  }

  /** Health and shields are authoritative readings; cosmetic bolt arrival never implies a hit. */
  configureFeedback(enabled: boolean, shield: number): void {
    this.#feedback = enabled;
    this.#shield = shield;
  }

  /** The spool's rings and glow, and the flashes on show, for tests. */
  get jumpSpool(): Readonly<Group> {
    return this.#charge.group;
  }

  get jumpFlashes(): readonly Readonly<Group>[] {
    return this.#flashes.filter((flash) => flash.group.visible).map((flash) => flash.group);
  }

  get beam(): Readonly<Group> {
    return this.#extractor.beam;
  }

  get beamFragments(): Readonly<Group> {
    return this.#extractor.fragments;
  }

  /**
   * Draws the extractor's beam from `from`, the player's ship, to the deposit numbered `deposit`
   * in the site's list, or hides it with `null`. Playtesting found extraction gave no sign in
   * the world that anything was happening; the beam shows which deposit is being worked.
   */
  setBeam(from: { x: number; z: number } | null, deposit: number | null): void {
    const drawn = deposit === null ? undefined : this.#deposits[deposit];
    if (!from || !drawn) {
      this.#extractor.hide();
      return;
    }
    // From the keel, so the hull hides the beam beneath it and the beam leaves from under the
    // ship's edge. Started at bolt height it lay along the roof, drawn across the hull.
    const start = new Vector3(from.x, RENDER_HEIGHT - SHIP_HEIGHT / 2, from.z);
    // Into the crystals, which shrink as the deposit empties.
    const end = drawn.group.position.clone().setY(BEAM_END_HEIGHT * drawn.group.scale.y);
    if (start.distanceTo(end) < 1e-3) {
      this.#extractor.hide();
      return;
    }
    this.#extractor.aim(start, end, drawn.material.color);
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
   * Moves every ship to its pose, brackets the locked one, and draws the shots and the jumps.
   * `dt`, seconds since the last update, paces the landing gear.
   */
  update(
    poses: readonly EntityPose[],
    lock: number | null,
    shots?: Shots,
    dt = 0,
    jumps?: JumpEffects,
  ): void {
    // Feedback is off when the system asks for reduced motion (configureFeedback).
    this.#extractor.step(dt, !this.#feedback);
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
      const berthed = this.#berths.some(
        (berth) => Math.hypot(pose.x - berth.x, pose.z - berth.z) <= ON_BERTH,
      );
      const target = berthed ? 1 : 0;
      if (!drawn) {
        const meshes = shipMeshes(allegiance, SIZE[pose.kind]);
        drawn = {
          ...meshes,
          health: pose.hull,
          shield: pose.id === this.#ownShipId ? this.#shield : 0,
          hit: 0,
          feedback: feedbackMaterials(meshes.hull),
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
      const shield = pose.id === this.#ownShipId ? this.#shield : 0;
      if (this.#feedback && (pose.hull < drawn.health || shield < drawn.shield)) drawn.hit = 0.3;
      drawn.health = pose.hull;
      drawn.shield = shield;
      if (!this.#feedback) drawn.hit = 0;
      const strength = drawn.hit / 0.3;
      for (const signal of drawn.feedback) {
        signal.material.emissive.copy(signal.emission).lerp(HIT_LIGHT, strength * 0.35);
        signal.material.emissiveIntensity = signal.intensity + strength * 0.55;
      }
      drawn.hit = Math.max(0, drawn.hit - dt);
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
    if (jumps) this.#drawJumps(jumps);
  }

  /**
   * The spool on the ship whose drive is charging, and a flash at each end of every jump. They
   * keep the rule for effects, a bright core in a coloured edge, and none of them is solid: a
   * hostile behind one stays in sight.
   */
  #drawJumps(jumps: JumpEffects): void {
    const charges = jumps.charges;
    while (this.#crewCharges.length + 1 < charges.length) {
      const drawn = { group: new Group(), steady: effectRing(), pulse: effectRing() };
      drawn.group.add(drawn.steady, drawn.pulse);
      drawn.group.name = "crew jump spool";
      this.scene.add(drawn.group);
      this.#crewCharges.push(drawn);
    }
    [this.#charge, ...this.#crewCharges].forEach((drawn, index) => {
      const charge = charges[index];
      const ship = charge ? this.#ships.get(charge.ship) : undefined;
      drawn.group.visible = charge !== undefined && ship !== undefined;
      if (!charge || !ship) return;
      drawn.group.position.copy(ship.hull.position).setY(SHIP_HEIGHT / 2);
      setRing(
        drawn.steady,
        CHARGE_RING.far + (CHARGE_RING.near - CHARGE_RING.far) * charge.share,
        0.35 + 0.5 * charge.share,
      );
      setRing(
        drawn.pulse,
        PULSE_RING.far + (PULSE_RING.near - PULSE_RING.far) * charge.pulse,
        0.1 + 0.6 * charge.pulse,
      );
    });

    const flashes = jumps.flashes;
    while (this.#flashes.length < flashes.length) {
      const spark = new Mesh(
        new IcosahedronGeometry(0.8, 2),
        new MeshBasicMaterial({ toneMapped: false, transparent: true, depthWrite: false }),
      );
      const streak = new Mesh(
        new BoxGeometry(1, 0.5, 0.5),
        new MeshBasicMaterial({
          toneMapped: false,
          color: JUMP,
          transparent: true,
          depthWrite: false,
        }),
      );
      const core = new Mesh(
        new BoxGeometry(1, 0.16, 0.16),
        new MeshBasicMaterial({ toneMapped: false, transparent: true, depthWrite: false }),
      );
      streak.add(core);
      streak.name = "streak";
      const drawn = {
        group: new Group(),
        ring: effectRing(),
        echo: effectRing(),
        spark,
        streak,
        core,
      };
      drawn.group.add(drawn.ring, drawn.echo, streak, spark);
      drawn.group.name = "jump flash";
      this.scene.add(drawn.group);
      this.#flashes.push(drawn);
    }
    this.#flashes.forEach((drawn, index) => {
      const flash = flashes[index];
      drawn.group.visible = flash !== undefined;
      if (flash) drawFlash(drawn, flash);
    });
  }

  #remove(id: number, drawn: DrawnShip): void {
    this.scene.remove(drawn.hull, drawn.silhouette);
    for (const signal of drawn.feedback) signal.material.dispose();
    drawn.dispose();
    this.#ships.delete(id);
  }

  /** Bolts keep the style guide's rule for effects: a bright core in a coloured edge. */
  #drawShots(shots: Shots): void {
    const bolts = shots.bolts;
    while (this.#bolts.length < bolts.length) {
      const edge = new Mesh(
        new BoxGeometry(1.1, 0.26, 0.26),
        new MeshBasicMaterial({ toneMapped: false, transparent: true, opacity: 0.6 }),
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
        new MeshBasicMaterial({ toneMapped: false, color: BLAST, transparent: true }),
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
    this.#berths = berthPositions(
      { x: site.dockX, z: site.dockZ },
      { x: site.centreX, z: site.centreZ },
    );
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
    this.#extractor.hide();
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
  const material = new MeshStandardMaterial({
    color: linear(0.18, 0.22, 0.28),
    metalness: 0.5,
    roughness: 0.7,
    ...STRUCTURE_STENCIL,
  });
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
    const top = new Mesh(
      new OctahedronGeometry(1.4),
      new MeshStandardMaterial({
        color: BEACON,
        emissive: BEACON,
        emissiveIntensity: 0.5,
        roughness: 0.5,
      }),
    );
    top.position.y = 9.4;
    top.name = "open";
    group.add(top);
  }
  return group;
}
