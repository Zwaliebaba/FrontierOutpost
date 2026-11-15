/**
 * The outpost as the home shows it (design §11): the station's exterior, with each installed
 * facility's tell on its socket's hardpoint, or, once the station is selected, the interior's
 * cutaway in its place. A facility shows outside only: the interior keeps its socket as a service
 * location, marked with the facility's state (owner decision, 15 November 2026; design §11),
 * closed sockets plated, a ring on the selected socket and the pilot's ship parked on the
 * hangar deck. The same object stands at the dock in a run. It needs no WebGL, so the
 * placement rules are tested without a renderer.
 */
import {
  Box3,
  BoxGeometry,
  CylinderGeometry,
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
import type { AccountInfo } from "../protocol";
import contract from "../../../Design/Art/contract.json";
import { SHIP_HEIGHT } from "../view/hulls";
import { stationLights } from "../view/lights";
import { STRUCTURE_STENCIL, linear, placeShip, shipMeshes } from "../view/readability";
import type { ShipMeshes } from "../view/readability";
import { facilityKey } from "./models";
import type { OutpostModels } from "./models";
import { HOME_STATION, placeStation } from "./placement";
import type { Placement } from "./placement";
import type { StationView } from "./stationView";

/** How far above its deck a socket's pick target sits, and how large it is, metres. */
const PICK_LIFT = 1;
const PICK_RADIUS = 1.9;
/** How tall the exterior's pick target stands either side of the plane, metres. */
const STATION_PICK_HEIGHT = 8;
const RING = linear(1, 0.62, 0.15);
/** A service location's state: lit as the style guide's indicators when powered, dark when not. */
const POWERED = linear(0.04, 0.65, 1);
const UNPOWERED = linear(0.12, 0.14, 0.17);
const PIP_OFF = linear(0.05, 0.06, 0.08);
const SERVICE_RADIUS = 1.15;

/**
 * The mark an installed facility leaves on its interior socket: a flat ring in its state's
 * colour and one pip per owned tier, lit up to the tier it runs at. It stands no taller than a
 * deck plate, so the cutaway shows the facility's state without duplicating its exterior model.
 */
export function serviceMarker(
  facility: string,
  tier: number,
  run: number,
  powered: boolean,
): Object3D {
  const marker = new Group();
  marker.name = `Service_${facility}_T${String(tier)}`;
  const ring = new Mesh(
    new TorusGeometry(SERVICE_RADIUS, 0.09, 6, 40),
    new MeshBasicMaterial({ color: powered ? POWERED : UNPOWERED, toneMapped: false }),
  );
  ring.rotation.x = Math.PI / 2;
  ring.position.y = 0.04;
  marker.add(ring);
  for (let n = 1; n <= tier; n += 1) {
    const pip = new Mesh(
      new BoxGeometry(0.28, 0.06, 0.28),
      new MeshBasicMaterial({ color: n <= run ? POWERED : PIP_OFF, toneMapped: false }),
    );
    pip.name = `Service_pip_${String(n)}${n <= run ? "_lit" : ""}`;
    pip.position.set((n - (tier + 1) / 2) * 0.42, 0.05, 0);
    marker.add(pip);
  }
  return marker;
}

interface Placed {
  key: string;
  powered: boolean;
  run: number;
  object: Object3D;
}

export interface SceneReport {
  /** Which of the station's two views is drawn. */
  view: StationView;
  sockets: string[];
  facilities: string[];
  /** The service marker under each interior socket. */
  installed: { socket: string; facility: string; tier: number; powered: boolean }[];
  /** Facility module models under interior sockets: none, since facilities show outside. */
  modules: number;
  /** What stands on each exterior hardpoint. */
  tells: { hardpoint: string; facility: string; tier: number; powered: boolean }[];
  /** The sockets whose closure plate is shown. */
  closed: string[];
  /** The hulls parked on the hangar deck. */
  parked: string[];
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

const FACILITY = /^Facility_([a-z][a-z0-9_]*)_T(\d+)$/;
const SERVICE = /^Service_([a-z][a-z0-9_]*)_T(\d+)$/;
const TELL = /^Tell_([a-z][a-z0-9_]*)_T(\d+)$/;

export class OutpostScene {
  readonly scene = new Scene();
  /**
   * The station, exterior and interior, placed where the run's dock is: it stands in the outpost
   * view or in the expedition, the same object.
   */
  readonly root = new Group();
  /** The middle of the station on the plane, which the home view orbits. */
  readonly centre = new Vector3();
  readonly #models: OutpostModels;
  readonly #placed = new Map<string, Placed>();
  readonly #tells = new Map<string, Placed>();
  readonly #targets = new Map<string, Mesh>();
  readonly #station: Mesh;
  readonly #ring: Mesh;
  readonly #parked: ShipMeshes;
  #selected: string | null = null;
  #view: StationView = "exterior";
  #atHome = true;
  #open = new Set<string>();
  #account: AccountInfo | null = null;
  readonly #loading = new Set<string>();
  readonly #changed: () => void;
  readonly #failed: (error: unknown) => void;

  constructor(
    models: OutpostModels,
    changed: () => void = () => undefined,
    failed: (error: unknown) => void = () => undefined,
  ) {
    this.#models = models;
    this.#changed = changed;
    this.#failed = failed;
    this.scene.add(...stationLights());
    structure(models.exterior);
    structure(models.interior);
    // Socket C opens with the reactor (plan M10); until then neither is shown.
    for (const part of models.reactor) part.visible = false;
    this.root.name = "station";
    this.root.add(models.exterior, models.interior);
    const hidden = new MeshBasicMaterial({ visible: false });
    // Selecting the station anywhere on its ring opens the interior: one generous target.
    const bounds = new Box3().setFromObject(models.exterior);
    const reach = Math.max(1, -bounds.min.x, bounds.max.x, -bounds.min.z, bounds.max.z);
    this.#station = new Mesh(
      new CylinderGeometry(reach, reach, STATION_PICK_HEIGHT * 2, 32),
      hidden,
    );
    this.#station.name = "pick:station";
    models.exterior.add(this.#station);
    const target = new SphereGeometry(PICK_RADIUS, 12, 8);
    for (const [id, anchor] of models.sockets) {
      const pick = new Mesh(target, hidden);
      pick.name = `pick:${id}`;
      pick.position.set(0, PICK_LIFT, 0);
      anchor.add(pick);
      this.#targets.set(id, pick);
    }
    this.#ring = new Mesh(
      new TorusGeometry(contract.socket.half_width + contract.clearance * 2, 0.06, 6, 48),
      new MeshBasicMaterial({ color: RING, toneMapped: false }),
    );
    this.#ring.rotation.x = Math.PI / 2;
    this.#ring.visible = false;
    // The pilot's own ship waits on its parking place, facing out of the mouth (glTF −z).
    this.#parked = shipMeshes("self");
    const parking = models.parking.get("frigate");
    for (const mesh of [this.#parked.hull, this.#parked.silhouette]) {
      mesh.name = "parked ship";
      parking?.add(mesh);
    }
    placeShip(this.#parked, 0, 0, -Math.PI / 2);
    // Its underside on the deck, as it is on the plane in flight.
    for (const mesh of [this.#parked.hull, this.#parked.silhouette])
      mesh.position.y = SHIP_HEIGHT / 2;
    this.home();
    this.place(placeStation(HOME_STATION));
    this.#show();
  }

  /** Draws `view` of the station in place of the other; in a run only the exterior stands. */
  setView(view: StationView): void {
    this.#view = view;
    this.#show();
  }

  get view(): StationView {
    return this.#view;
  }

  #show(): void {
    const inside = this.#atHome && this.#view === "interior";
    this.#models.exterior.visible = !inside;
    this.#models.interior.visible = inside;
    this.scene.updateMatrixWorld(true);
  }

  /**
   * Shows each socket as `account` has it: its tell on the socket's hardpoint outside, growing
   * with the tier and dimmed if unpowered, and its service marker on the socket inside.
   */
  apply(account: AccountInfo): void {
    this.#account = account;
    this.#open = new Set(account.outpost.sockets.filter((s) => s.open).map((s) => s.id));
    for (const [id, closure] of this.#models.closures) closure.visible = !this.#open.has(id);
    for (const socket of account.outpost.sockets) {
      const anchor = this.#models.sockets.get(socket.id);
      if (!anchor) continue;
      const tier =
        account.outpost.unlocks.find((unlock) => unlock.facility === socket.facility)?.tier ?? 0;
      const key = socket.facility === undefined ? null : facilityKey(socket.facility, tier);
      const powered = socket.run > 0;
      const current = this.#placed.get(socket.id);
      if (
        current &&
        current.key === key &&
        current.powered === powered &&
        current.run === socket.run &&
        (this.#tells.has(socket.id) || !this.#models.hardpoints.has(socket.id))
      )
        continue;
      this.#clear(socket.id);
      // The facility's file holds its tells; the marker waits for it, so both appear together.
      const template = key === null ? undefined : this.#models.facilities.get(key);
      if (key && !template && socket.facility) this.#load(socket.facility);
      if (!key || !template || !socket.facility) continue;
      const object = serviceMarker(socket.facility, tier, socket.run, powered);
      anchor.add(object);
      this.#placed.set(socket.id, { key, powered, run: socket.run, object });
      const hardpoint = this.#models.hardpoints.get(socket.id);
      const tell = this.#models.tells.get(key)?.clone();
      if (hardpoint && tell) {
        tell.position.set(0, 0, 0);
        if (!powered) dim(tell);
        structure(tell);
        hardpoint.add(tell);
        this.#tells.set(socket.id, { key, powered, run: socket.run, object: tell });
      }
    }
    this.scene.updateMatrixWorld(true);
  }

  /** Takes the service marker and the tell off socket `id`. */
  #clear(id: string): void {
    this.#placed.get(id)?.object.removeFromParent();
    this.#placed.delete(id);
    this.#tells.get(id)?.object.removeFromParent();
    this.#tells.delete(id);
  }

  /** Fetches a facility's file, then shows the account again with its models in. */
  #load(id: string): void {
    if (!this.#models.loadFacility || this.#loading.has(id)) return;
    this.#loading.add(id);
    void this.#models
      .loadFacility(id)
      .then(() => {
        this.#loading.delete(id);
        if (this.#account) this.apply(this.#account);
        this.#changed();
      })
      .catch((error: unknown) => {
        this.#loading.delete(id);
        this.#failed(error);
      });
  }

  /** Stands the station where the rules put it (`placeStation`). */
  place(placement: Placement): void {
    this.root.position.set(placement.x, placement.y, placement.z);
    this.root.rotation.set(0, placement.yaw, 0);
    this.root.updateMatrixWorld(true);
    this.centre.set(placement.x, 0, placement.z);
  }

  /** Brings the station home to the outpost view, the pilot's ship parked inside. */
  home(): void {
    this.scene.add(this.root);
    this.#atHome = true;
    this.#parked.hull.visible = true;
    this.#parked.silhouette.visible = true;
    this.#show();
  }

  /**
   * The station leaves for a run: only the exterior stands at the dock, and the ship flies from
   * the hangar, drawn from snapshots.
   */
  launch(): Object3D {
    this.#atHome = false;
    this.#parked.hull.visible = false;
    this.#parked.silhouette.visible = false;
    this.#show();
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

  /** The open socket under a ray from the camera, if any, while the interior is drawn. */
  pick(raycaster: Raycaster): string | null {
    if (this.#view !== "interior" || !this.#atHome) return null;
    const open = [...this.#targets].filter(([id]) => this.#open.has(id)).map(([, pick]) => pick);
    const [hit] = raycaster.intersectObjects(open, false);
    return hit ? hit.object.name.slice("pick:".length) : null;
  }

  /** Whether a ray from the camera meets the station, while its exterior is drawn. */
  pickStation(raycaster: Raycaster): boolean {
    if (this.#view !== "exterior" || !this.#atHome) return false;
    return raycaster.intersectObject(this.#station, false).length > 0;
  }

  /** Where a socket's anchor is, for labels. */
  socketPosition(id: string): Vector3 | null {
    return this.#models.sockets.get(id)?.getWorldPosition(new Vector3()) ?? null;
  }

  /** What the scene graph holds, for tests and the page's diagnostics. */
  report(): SceneReport {
    const installed: SceneReport["installed"] = [];
    let modules = 0;
    for (const [socket, anchor] of this.#models.sockets) {
      anchor.traverse((node) => {
        if (FACILITY.test(node.name)) modules += 1;
      });
      for (const child of anchor.children) {
        const match = SERVICE.exec(child.name);
        if (!match?.[1] || !match[2]) continue;
        installed.push({
          socket,
          facility: match[1],
          tier: Number(match[2]),
          powered: this.#placed.get(socket)?.powered ?? false,
        });
      }
    }
    const tells: SceneReport["tells"] = [];
    for (const [hardpoint, anchor] of this.#models.hardpoints) {
      for (const child of anchor.children) {
        const match = TELL.exec(child.name);
        if (!match?.[1] || !match[2]) continue;
        tells.push({
          hardpoint,
          facility: match[1],
          tier: Number(match[2]),
          powered: this.#tells.get(hardpoint)?.powered ?? false,
        });
      }
    }
    const parked: string[] = [];
    for (const [hull, anchor] of this.#models.parking) {
      if (anchor.children.some((child) => child.name === "parked ship" && child.visible))
        parked.push(hull);
    }
    return {
      view: this.#atHome ? this.#view : "exterior",
      sockets: [...this.#models.sockets.keys()].sort(),
      facilities: [...this.#models.facilities.keys()].sort(),
      installed,
      modules,
      tells,
      closed: [...this.#models.closures]
        .filter(([, closure]) => closure.visible)
        .map(([id]) => id)
        .sort(),
      parked,
    };
  }
}
