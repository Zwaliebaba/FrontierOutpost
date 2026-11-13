/**
 * The extractor's beam and the ore it pulls in. Playtesting found the first beam, a thin box in
 * the deposit's own muted colour, read as a solid rod: nothing about it said material was moving.
 * Now a soft glow with a bright core (the style guide's rule for effects, §6) carries bands that
 * run from the deposit to the ship, and a few fragments break from the crystals and fly into the
 * hull, so cargo is seen arriving. With reduced motion the beam holds still and the fragments
 * stay away. The paths are plain functions, tested without WebGL.
 */
import {
  AdditiveBlending,
  BoxGeometry,
  Color,
  DataTexture,
  DoubleSide,
  Group,
  Mesh,
  MeshBasicMaterial,
  OctahedronGeometry,
  PlaneGeometry,
  RepeatWrapping,
  RGBAFormat,
  Vector3,
} from "three";

/** How many fragments are in flight at once. */
export const FRAGMENTS = 6;
/** How fast a fragment flies from the deposit to the hull, metres per second. */
export const FRAGMENT_SPEED = 3.5;
/** How fast the glow's bands run toward the ship, metres per second. */
const BAND_SPEED = 5;
/** Metres between bands along the glow. */
const BAND_SPACING = 0.9;
/** The glow's width, metres; the core is far thinner. */
const GLOW_WIDTH = 0.55;
const CORE_WIDTH = 0.05;
/** How far a fragment's path bows out from the beam at its middle, metres. */
const BOW = 0.35;
const X_AXIS = new Vector3(1, 0, 0);
const UP = new Vector3(0, 1, 0);

/** Where fragment `index` of `count` is, `clock` seconds in, on a beam `length` metres long. */
export interface FragmentPlace {
  /** 0 at the deposit, rising to 1 at the hull. */
  along: number;
  /** Sideways from the beam, metres: out at the middle, none at either end. */
  side: number;
  /** 0 to 1: it breaks out of the crystal, then shrinks into the hull. */
  scale: number;
}

export function fragmentAt(
  index: number,
  count: number,
  clock: number,
  length: number,
): FragmentPlace {
  const raw = (clock * FRAGMENT_SPEED) / Math.max(length, 1) + index / count;
  const along = raw - Math.floor(raw);
  // Alternate sides, a little different each, so the stream is not a single file.
  const lean = (index % 2 === 0 ? 1 : -1) * (0.5 + (0.5 * ((index * 7) % count)) / count);
  return {
    along,
    side: BOW * lean * Math.sin(Math.PI * along),
    scale: Math.min(1, along / 0.12, (1 - along) / 0.25),
  };
}

/** Across the glow: bright in the middle, gone at the edges. Along it: bands. */
function glowTexture(): DataTexture {
  const width = 32;
  const height = 16;
  const data = new Uint8Array(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    const across = (y + 0.5) / height - 0.5;
    const profile = Math.exp(-across * across * 28);
    for (let x = 0; x < width; x += 1) {
      const band = 0.5 + 0.5 * Math.cos((2 * Math.PI * (x + 0.5)) / width);
      const value = Math.round(255 * profile * (0.45 + 0.55 * band));
      data.set([value, value, value, 255], (y * width + x) * 4);
    }
  }
  const texture = new DataTexture(data, width, height, RGBAFormat);
  texture.wrapS = RepeatWrapping;
  texture.needsUpdate = true;
  return texture;
}

export class ExtractorBeam {
  /** The glow and core, laid along +X and stretched to the beam's length. */
  readonly beam = new Group();
  /** The fragments, in the world: not stretched with the beam. */
  readonly fragments = new Group();
  readonly #texture = glowTexture();
  readonly #glow: MeshBasicMaterial;
  readonly #core: MeshBasicMaterial;
  readonly #chunk: MeshBasicMaterial;
  readonly #start = new Vector3();
  readonly #end = new Vector3();
  #clock = 0;

  constructor() {
    this.#glow = new MeshBasicMaterial({
      map: this.#texture,
      blending: AdditiveBlending,
      transparent: true,
      depthWrite: false,
      side: DoubleSide,
      toneMapped: false,
    });
    this.#core = new MeshBasicMaterial({ toneMapped: false });
    this.#chunk = new MeshBasicMaterial({ toneMapped: false });
    // Two glows crossed along the beam, so it reads as round from the tilted camera.
    const upright = new Mesh(new PlaneGeometry(1, GLOW_WIDTH), this.#glow);
    const flat = new Mesh(new PlaneGeometry(1, GLOW_WIDTH).rotateX(Math.PI / 2), this.#glow);
    const core = new Mesh(new BoxGeometry(1, CORE_WIDTH, CORE_WIDTH), this.#core);
    this.beam.add(upright, flat, core);
    this.beam.name = "extractor beam";
    this.fragments.name = "extracted fragments";
    for (let i = 0; i < FRAGMENTS; i += 1) {
      const chunk = new Mesh(new OctahedronGeometry(0.11, 0), this.#chunk);
      this.fragments.add(chunk);
    }
    this.hide();
  }

  /** Lays the beam from `start`, under the ship, to `end`, in the crystals, in `colour`. */
  aim(start: Vector3, end: Vector3, colour: Color): void {
    this.#start.copy(start);
    this.#end.copy(end);
    const length = start.distanceTo(end);
    this.beam.visible = true;
    this.beam.position.copy(start).add(end).multiplyScalar(0.5);
    this.beam.scale.set(length, 1, 1);
    this.beam.quaternion.setFromUnitVectors(X_AXIS, end.clone().sub(start).normalize());
    this.#texture.repeat.x = length / BAND_SPACING;
    // The glow keeps the material's tint; the core and the additive overlap give the heat. Any
    // brighter and the glow washed out to white, saying nothing about what was being mined.
    this.#glow.color
      .copy(colour)
      .lerp(new Color(1, 1, 1), 0.1)
      .multiplyScalar(0.9);
    this.#core.color.copy(colour).lerp(new Color(1, 1, 1), 0.75);
    this.#chunk.color
      .copy(colour)
      .lerp(new Color(1, 1, 1), 0.15)
      .multiplyScalar(1.3);
  }

  hide(): void {
    this.beam.visible = false;
    this.fragments.visible = false;
  }

  /** Runs the bands and the fragments on by `dt` seconds; `calm` holds them still and away. */
  step(dt: number, calm: boolean): void {
    if (!this.beam.visible) {
      this.fragments.visible = false;
      return;
    }
    this.fragments.visible = !calm;
    if (calm) return;
    this.#clock += dt;
    // Sampling further along moves the bands back toward the ship, at the beam's start.
    this.#texture.offset.x = (this.#clock * BAND_SPEED) / BAND_SPACING;
    this.#glow.opacity = 0.85 + 0.15 * Math.sin(this.#clock * 2 * Math.PI * 2.5);
    const path = this.#start.clone().sub(this.#end);
    const length = path.length();
    const sideways = path.clone().cross(UP).normalize();
    this.fragments.children.forEach((chunk, index) => {
      const place = fragmentAt(index, FRAGMENTS, this.#clock, length);
      chunk.position
        .copy(this.#end)
        .addScaledVector(path, place.along)
        .addScaledVector(sideways, place.side);
      chunk.scale.setScalar(Math.max(place.scale, 1e-3));
      chunk.rotation.set(this.#clock * 3 + index, this.#clock * 2, 0);
    });
  }
}
