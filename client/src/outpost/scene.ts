/**
 * The outpost as the overview shows it (design §2): the station, each installed facility on its
 * socket at the tier it owns, unpowered ones dimmed, and a ring on the selected socket. It needs
 * no WebGL, so the placement rules are tested without a renderer.
 */
import {
  Box3,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  Scene,
  SphereGeometry,
  TorusGeometry,
  Vector3,
} from "three";
import type { Material, Object3D, Raycaster } from "three";
import { Group } from "three";
import type { AccountInfo, CrewInfo } from "../protocol";
import { stationLights } from "../view/lights";
import { STRUCTURE_STENCIL, linear, placeShip, shipMeshes } from "../view/readability";
import type { ShipMeshes } from "../view/readability";
import { facilityKey } from "./models";
import type { OutpostModels } from "./models";
import { BERTHS, STATION_DROP } from "./placement";
import type { Placement } from "./placement";

/** How far above its deck a socket's pick target sits, and how large it is, metres. */
const PICK_LIFT = 1;
const PICK_RADIUS = 1.9;
const RING = linear(1, 0.62, 0.15);

interface Placed {
  key: string;
  powered: boolean;
  run: number;
  object: Object3D;
}

export interface SceneReport {
  berths: { berth: string; callsign: string; you: boolean }[];
  sockets: string[];
  facilities: string[];
  /** What is actually in the scene graph under each socket. */
  installed: { socket: string; facility: string; tier: number; powered: boolean }[];
}

/**
 * Makes every mesh in `object` a structure (design §5): a ship behind it shows as a silhouette,
 * as behind an asteroid.
 */
function structure(object: Object3D): void {
  object.traverse((node) => {
    if (!(node instanceof Mesh)) return;
    const material = node.material as Material | Material[];
    for (const m of Array.isArray(material) ? material : [material])
      Object.assign(m, STRUCTURE_STENCIL);
  });
}

/** Every material in `object`, cloned and dimmed, so an unpowered module reads as off. */
function dim(object: Object3D): void {
  object.traverse((node) => {
    if (!(node instanceof Mesh)) return;
    const dimmed = (material: Material): Material => {
      if (!(material instanceof MeshStandardMaterial)) return material;
      const copy = material.clone();
      copy.emissiveIntensity = 0;
      copy.color.multiplyScalar(0.45);
      return copy;
    };
    const material = node.material as Material | Material[];
    node.material = Array.isArray(material) ? material.map(dimmed) : dimmed(material);
  });
}

export class OutpostScene {
  readonly scene = new Scene();
  /**
   * The station with its facilities and, at home, the pilot's ship on the berth: placed where
   * the run's dock is, it stands in the outpost view or in the expedition, the same object.
   */
  readonly root = new Group();
  /** The middle of the station, which the overview orbits. */
  readonly centre = new Vector3();
  readonly #models: OutpostModels;
  readonly #placed = new Map<string, Placed>();
  readonly #targets = new Map<string, Mesh>();
  readonly #ring: Mesh;
  #selected: string | null = null;
  #crewShips: ShipMeshes[] = [];
  #roster: { berth: string; callsign: string; you: boolean }[] = [];
  #atHome = true;

  constructor(models: OutpostModels) {
    this.#models = models;
    this.scene.add(...stationLights());
    structure(models.station);
    this.root.name = "station";
    this.root.add(models.station);
    this.setCrew(null, "Pilot");
    this.home();
    this.place({ x: 0, z: 0, yaw: 0, y: 0, heading: 0 });
    const target = new SphereGeometry(PICK_RADIUS, 12, 8);
    const hidden = new MeshBasicMaterial({ visible: false });
    for (const [id, anchor] of models.sockets) {
      const pick = new Mesh(target, hidden);
      pick.name = `pick:${id}`;
      pick.position.set(0, PICK_LIFT, 0);
      anchor.add(pick);
      this.#targets.set(id, pick);
    }
    this.#ring = new Mesh(
      new TorusGeometry(1.55, 0.06, 6, 48),
      new MeshBasicMaterial({ color: RING, toneMapped: false }),
    );
    this.#ring.rotation.x = Math.PI / 2;
    this.#ring.visible = false;
    this.scene.updateMatrixWorld(true);
  }

  /** Roster ships are presentation at each pilot's own home, never shared inventory. */
  setCrew(crew: CrewInfo | null, callsign: string): void {
    const roster = crew?.members ?? [{ callsign, you: true, share: 100, berth: 0 }];
    const next = roster.slice(0, BERTHS.length).map((member, index) => ({
      berth: BERTHS[member.berth ?? index]?.id ?? "07",
      callsign: member.callsign,
      you: member.you,
    }));
    if (JSON.stringify(next) === JSON.stringify(this.#roster)) return;
    for (const ship of this.#crewShips) {
      ship.hull.removeFromParent();
      ship.silhouette.removeFromParent();
      ship.dispose();
    }
    this.#roster = next;
    this.#crewShips = next.map((member) => {
      const berth = BERTHS.find((b) => b.id === member.berth);
      const ship = shipMeshes(member.you ? "self" : "ally");
      if (!berth) return ship;
      placeShip(ship, berth.x, berth.z, Math.PI / 2);
      for (const mesh of [ship.hull, ship.silhouette]) {
        mesh.position.y += STATION_DROP;
        mesh.name = "berth ship";
        mesh.userData.callsign = member.callsign;
        mesh.visible = this.#atHome;
        this.root.add(mesh);
      }
      return ship;
    });
    this.root.updateMatrixWorld(true);
  }

  /** Shows each socket as `account` has it: the module at its owned tier, dimmed if unpowered. */
  apply(account: AccountInfo): void {
    for (const socket of account.outpost.sockets) {
      const anchor = this.#models.sockets.get(socket.id);
      if (!anchor) continue;
      anchor.traverse((node) => {
        if (node.name.startsWith("SocketClosure_")) node.visible = !socket.open;
      });
      const current = this.#placed.get(socket.id);
      const tier =
        account.outpost.unlocks.find((unlock) => unlock.facility === socket.facility)?.tier ?? 0;
      const key = socket.facility === undefined ? null : facilityKey(socket.facility, tier);
      const powered = socket.run > 0;
      if (
        current &&
        current.key === key &&
        current.powered === powered &&
        current.run === socket.run
      )
        continue;
      if (current) {
        anchor.remove(current.object);
        this.#placed.delete(socket.id);
      }
      const template = key === null ? undefined : this.#models.facilities.get(key);
      if (!key || !template) continue;
      const object = template.clone();
      object.position.set(0, 0, 0);
      object.traverse((node) => {
        const match = /^Operating_Tier_(\d+)/.exec(node.name);
        if (match?.[1]) node.visible = Number(match[1]) <= socket.run;
      });
      if (!powered) dim(object);
      structure(object);
      anchor.add(object);
      this.#placed.set(socket.id, { key, powered, run: socket.run, object });
    }
    this.scene.updateMatrixWorld(true);
  }

  /** Stands the station where a run's dock puts it (`placeStation`). */
  place(placement: Placement): void {
    this.root.position.set(placement.x, placement.y, placement.z);
    this.root.rotation.set(0, placement.yaw, 0);
    this.root.updateMatrixWorld(true);
    new Box3().setFromObject(this.#models.station).getCenter(this.centre);
    this.centre.y = 0;
  }

  /** Brings the station home to the outpost view, with the ship back on its berth. */
  home(): void {
    this.scene.add(this.root);
    this.#atHome = true;
    for (const ship of this.#crewShips) {
      ship.hull.visible = true;
      ship.silhouette.visible = true;
    }
    this.scene.updateMatrixWorld(true);
  }

  /** The station leaves for a run: the ship flies from the berth, drawn from snapshots. */
  launch(): Object3D {
    this.#atHome = false;
    for (const ship of this.#crewShips) {
      ship.hull.visible = false;
      ship.silhouette.visible = false;
    }
    return this.root;
  }

  /** Rings the socket `id`, or none. */
  select(id: string | null): void {
    this.#selected = id;
    const anchor = id === null ? undefined : this.#models.sockets.get(id);
    this.#ring.removeFromParent();
    this.#ring.visible = anchor !== undefined;
    anchor?.add(this.#ring);
    this.#ring.position.set(0, 0.05, 0);
    this.scene.updateMatrixWorld(true);
  }

  get selected(): string | null {
    return this.#selected;
  }

  /** The socket under a ray from the camera, if any. */
  pick(raycaster: Raycaster): string | null {
    const [hit] = raycaster.intersectObjects([...this.#targets.values()], false);
    return hit ? hit.object.name.slice("pick:".length) : null;
  }

  /** Where a socket's anchor is, for labels. */
  socketPosition(id: string): Vector3 | null {
    return this.#models.sockets.get(id)?.getWorldPosition(new Vector3()) ?? null;
  }

  /** What the scene graph holds, for tests and the page's diagnostics. */
  report(): SceneReport {
    const installed: SceneReport["installed"] = [];
    for (const [socket, anchor] of this.#models.sockets) {
      for (const child of anchor.children) {
        const match = /^Facility_([a-z_]+)_T(\d+)$/.exec(child.name);
        if (!match?.[1] || !match[2]) continue;
        installed.push({
          socket,
          facility: match[1],
          tier: Number(match[2]),
          powered: this.#placed.get(socket)?.powered ?? false,
        });
      }
    }
    return {
      berths: this.#roster,
      sockets: [...this.#models.sockets.keys()].sort(),
      facilities: [...this.#models.facilities.keys()].sort(),
      installed,
    };
  }
}
