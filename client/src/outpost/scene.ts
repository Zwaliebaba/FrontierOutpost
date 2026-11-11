/**
 * The outpost as the overview shows it (design §2): the station, each installed facility on its
 * socket at the tier it owns, unpowered ones dimmed, and a ring on the selected socket. It needs
 * no WebGL, so the placement rules are tested without a renderer.
 */
import {
  Box3,
  DirectionalLight,
  HemisphereLight,
  Mesh,
  MeshBasicMaterial,
  MeshStandardMaterial,
  Scene,
  SphereGeometry,
  TorusGeometry,
  Vector3,
} from "three";
import type { Material, Object3D, Raycaster } from "three";
import type { AccountInfo } from "../protocol";
import { linear } from "../view/readability";
import { facilityKey } from "./models";
import type { OutpostModels } from "./models";

/** How far above its deck a socket's pick target sits, and how large it is, metres. */
const PICK_LIFT = 1;
const PICK_RADIUS = 1.9;
const RING = linear(1, 0.62, 0.15);

interface Placed {
  key: string;
  powered: boolean;
  object: Object3D;
}

export interface SceneReport {
  sockets: string[];
  facilities: string[];
  /** What is actually in the scene graph under each socket. */
  installed: { socket: string; facility: string; tier: number; powered: boolean }[];
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
  /** The middle of the station, which the overview orbits. */
  readonly centre = new Vector3();
  readonly #models: OutpostModels;
  readonly #placed = new Map<string, Placed>();
  readonly #targets = new Map<string, Mesh>();
  readonly #ring: Mesh;
  #selected: string | null = null;

  constructor(models: OutpostModels) {
    this.#models = models;
    this.scene.add(new HemisphereLight(linear(0.3, 0.48, 1), linear(0.025, 0.038, 0.055), 0.7));
    const starlight = new DirectionalLight(linear(0.67, 0.77, 1), 2.4);
    starlight.position.set(-4, 10, 6);
    this.scene.add(starlight);
    const warm = new DirectionalLight(linear(1, 0.62, 0.37), 1.2);
    warm.position.set(6, 4, -5);
    this.scene.add(warm);
    this.scene.add(models.station);
    models.station.updateMatrixWorld(true);
    new Box3().setFromObject(models.station).getCenter(this.centre);
    this.centre.y = 0;
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
      new MeshBasicMaterial({ color: RING }),
    );
    this.#ring.rotation.x = Math.PI / 2;
    this.#ring.visible = false;
    this.scene.updateMatrixWorld(true);
  }

  /** Shows each socket as `account` has it: the module at its owned tier, dimmed if unpowered. */
  apply(account: AccountInfo): void {
    for (const socket of account.outpost.sockets) {
      const anchor = this.#models.sockets.get(socket.id);
      if (!anchor) continue;
      const current = this.#placed.get(socket.id);
      const tier =
        account.outpost.unlocks.find((unlock) => unlock.facility === socket.facility)?.tier ?? 0;
      const key = socket.facility === undefined ? null : facilityKey(socket.facility, tier);
      const powered = socket.run > 0;
      if (current && current.key === key && current.powered === powered) continue;
      if (current) {
        anchor.remove(current.object);
        this.#placed.delete(socket.id);
      }
      const template = key === null ? undefined : this.#models.facilities.get(key);
      if (!key || !template) continue;
      const object = template.clone();
      object.position.set(0, 0, 0);
      if (!powered) dim(object);
      anchor.add(object);
      this.#placed.set(socket.id, { key, powered, object });
    }
    this.scene.updateMatrixWorld(true);
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
      sockets: [...this.#models.sockets.keys()].sort(),
      facilities: [...this.#models.facilities.keys()].sort(),
      installed,
    };
  }
}
