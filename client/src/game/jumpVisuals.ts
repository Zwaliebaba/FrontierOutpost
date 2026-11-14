/**
 * The jump drive drawn (jumpEffects says what is on show and when). Every piece is light, not
 * matter: additive, soft-edged, a bright core in the drive's blue (style guide §6), so nothing
 * behind one is hidden and the spool is not taken for a shield. The glow and rings lie on the
 * plane just under the hull, so the hull draws over them and sparks vanish into it. One crisp
 * ring still closes as the countdown: a crewmate must read "about to jump" zoomed fully out,
 * where a soft glow alone is lost. With reduced motion nothing flies: no sparks, no scatter, and
 * the glow's bands hold still.
 */
import {
  AdditiveBlending,
  Color,
  Group,
  Mesh,
  MeshBasicMaterial,
  PlaneGeometry,
  ShaderMaterial,
  TorusGeometry,
} from "three";
import type { Points } from "three";
import { SHIP_HEIGHT } from "../view/hulls";
import { RENDER_HEIGHT, linear } from "../view/readability";
import { softPoints, softPointsMaterial } from "../view/softPoints";
import type { SoftPointsUniforms } from "../view/softPoints";
import {
  FLASH_SECONDS,
  SCATTER,
  SPARKS,
  flashShape,
  scatterAt,
  sparkAt,
  type Charge,
  type Flash,
  type JumpEffects,
} from "./jumpEffects";

/** The jump drive's light: the blue of propulsion, on the pilots' side of the palette. */
export const JUMP = linear(0.25, 0.72, 1);
/** The countdown ring, metres from the hull: it closes as the spool runs. */
export const COUNTDOWN = { far: 5.5, near: 1.8 };
/** How far the spool's glow reaches, metres. */
const GLOW_RADIUS = 8.5;
/** Just under the hull, so it draws over the effects on the plane. */
const UNDER = RENDER_HEIGHT - SHIP_HEIGHT / 2 - 0.05;
/** Where a ring sits on its plane, as a share of the plane's half-width. */
const RING_EDGE = 0.75;
const WHITE = new Color(1, 1, 1);

const flat = (size: number) => new PlaneGeometry(size, size).rotateX(-Math.PI / 2);

/**
 * A soft ring with a bright core: sharp on the outside, fading inside, like a wave front. With
 * `edge` 0 it is a soft blob of light instead.
 */
function ringMaterial(edge: number, inner: number, outer: number): ShaderMaterial {
  return new ShaderMaterial({
    uniforms: {
      colour: { value: JUMP },
      opacity: { value: 0 },
      edge: { value: edge },
      inner: { value: inner },
      outer: { value: outer },
    },
    vertexShader: /* glsl */ `
      varying vec2 vUv;
      void main() {
        vUv = uv;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }
    `,
    fragmentShader: /* glsl */ `
      uniform vec3 colour;
      uniform float opacity;
      uniform float edge;
      uniform float inner;
      uniform float outer;
      varying vec2 vUv;
      void main() {
        float r = length(vUv * 2.0 - 1.0);
        float d = r - edge;
        float w = d < 0.0 ? inner : outer;
        float glow = exp(-(d * d) / (w * w));
        float core = exp(-(d * d) / (outer * outer * 0.12));
        gl_FragColor = vec4(mix(colour, vec3(1.0), core * 0.75), glow * opacity);
        #include <colorspace_fragment>
      }
    `,
    blending: AdditiveBlending,
    transparent: true,
    depthWrite: false,
  });
}

/**
 * A streak of light along +x, 1 m long and 1 m across before scaling: bright along its middle,
 * narrowing by `taper` toward its far end and fading there by `tail`.
 */
function streakMaterial(taper: number, tail: number, head: number): ShaderMaterial {
  return new ShaderMaterial({
    uniforms: {
      colour: { value: JUMP },
      opacity: { value: 0 },
      taper: { value: taper },
      tail: { value: tail },
      head: { value: head },
    },
    vertexShader: /* glsl */ `
      varying vec2 vUv;
      void main() {
        vUv = uv;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }
    `,
    fragmentShader: /* glsl */ `
      uniform vec3 colour;
      uniform float opacity;
      uniform float taper;
      uniform float tail;
      uniform float head;
      varying vec2 vUv;
      void main() {
        float x = vUv.x;
        float across = abs(vUv.y * 2.0 - 1.0) / max(mix(1.0, taper, x), 0.001);
        float glow = exp(-across * across * 3.0);
        float core = exp(-across * across * 30.0);
        float along = smoothstep(0.0, head, x) * smoothstep(1.0, 1.0 - head, x)
          * pow(1.0 - x, tail);
        gl_FragColor = vec4(mix(colour, vec3(1.0), core * 0.8), glow * along * opacity);
        #include <colorspace_fragment>
      }
    `,
    blending: AdditiveBlending,
    transparent: true,
    depthWrite: false,
  });
}

/** Light gathering under a spooling ship: a soft disc whose bands run in toward the hull. */
function glowMaterial(): ShaderMaterial {
  return new ShaderMaterial({
    uniforms: { colour: { value: JUMP }, opacity: { value: 0 }, phase: { value: 0 } },
    vertexShader: /* glsl */ `
      varying vec2 vUv;
      void main() {
        vUv = uv;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }
    `,
    fragmentShader: /* glsl */ `
      uniform vec3 colour;
      uniform float opacity;
      uniform float phase;
      varying vec2 vUv;
      void main() {
        vec2 p = vUv * 2.0 - 1.0;
        float r = length(p);
        if (r > 1.0) discard;
        float turn = atan(p.y, p.x) / 6.2831853;
        float envelope = smoothstep(1.0, 0.35, r) * smoothstep(0.08, 0.3, r);
        // Three arms wound into a spiral; a rising phase draws each arm in toward the hull. Two
        // windings at different pitches keep the arms from reading as rings.
        float arms = 0.5 + 0.5 * sin((r * 2.2 + phase + turn * 3.0) * 6.2831853);
        float swirl = 0.5 + 0.5 * sin((r * 3.4 + phase * 1.5 - turn * 2.0) * 6.2831853);
        float band = arms * arms * (0.55 + 0.45 * swirl);
        float glow = envelope * (0.35 + 0.65 * band) * (0.35 + 0.65 * (1.0 - r));
        gl_FragColor = vec4(mix(colour, vec3(1.0), band * envelope * 0.25), glow * opacity);
        #include <colorspace_fragment>
      }
    `,
    blending: AdditiveBlending,
    transparent: true,
    depthWrite: false,
  });
}

/** The crisp countdown ring, one metre in radius: a coloured edge around a bright core. */
function countdownRing(): Mesh<TorusGeometry, MeshBasicMaterial> {
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
  ring.name = "countdown";
  return ring;
}

function setCountdown(ring: Mesh<TorusGeometry, MeshBasicMaterial>, share: number): void {
  ring.scale.setScalar(COUNTDOWN.far + (COUNTDOWN.near - COUNTDOWN.far) * share);
  const opacity = 0.35 + 0.5 * share;
  ring.material.opacity = opacity;
  const core = ring.children[0];
  if (core instanceof Mesh) (core.material as MeshBasicMaterial).opacity = opacity;
}

const setOpacity = (mesh: Mesh, opacity: number) => {
  const material = mesh.material as ShaderMaterial;
  if (material.uniforms.opacity) material.uniforms.opacity.value = opacity;
  mesh.visible = opacity > 0.002;
};

interface DrawnCharge {
  group: Group;
  glow: Mesh;
  countdown: Mesh<TorusGeometry, MeshBasicMaterial>;
  sparks: Points;
}

interface DrawnFlash {
  group: Group;
  line: Mesh;
  point: Mesh;
  ring: Mesh;
  trail: Mesh;
  scatter: Points;
}

export class JumpVisuals {
  readonly group = new Group();
  readonly #points: SoftPointsUniforms = { pixelRatio: { value: 1 }, opacity: { value: 1 } };
  readonly #charges: DrawnCharge[] = [];
  readonly #flashes: DrawnFlash[] = [];

  constructor() {
    this.group.name = "jumps";
    // The pilot's own spool is always the first, so tests and the HUD can find it.
    this.#addCharge();
  }

  /** Device pixels per CSS pixel, so sparks are as wide on every screen. */
  setPixelRatio(ratio: number): void {
    this.#points.pixelRatio.value = ratio;
  }

  /** The first spool: the pilot's own, or a crewmate's when they have none. */
  get spool(): Readonly<Group> {
    return (this.#charges[0] as DrawnCharge).group;
  }

  get flashes(): readonly Readonly<Group>[] {
    return this.#flashes.filter((flash) => flash.group.visible).map((flash) => flash.group);
  }

  /**
   * Draws each spool on its ship, found by `shipAt`, and both ends of every jump. `calm` is the
   * system's request for reduced motion.
   */
  update(
    jumps: JumpEffects,
    shipAt: (id: number) => { x: number; z: number } | undefined,
    calm: boolean,
  ): void {
    const charges = jumps.charges;
    while (this.#charges.length < charges.length) this.#addCharge();
    this.#charges.forEach((drawn, index) => {
      const charge = charges[index];
      const ship = charge ? shipAt(charge.ship) : undefined;
      drawn.group.visible = charge !== undefined && ship !== undefined;
      if (charge && ship) this.#drawCharge(drawn, charge, ship, calm);
    });

    const flashes = jumps.flashes;
    while (this.#flashes.length < flashes.length) this.#addFlash();
    this.#flashes.forEach((drawn, index) => {
      const flash = flashes[index];
      drawn.group.visible = flash !== undefined;
      if (flash) this.#drawFlash(drawn, flash, calm);
    });
  }

  #addCharge(): void {
    const glow = new Mesh(flat(2 * GLOW_RADIUS), glowMaterial());
    glow.position.y = UNDER;
    glow.name = "glow";
    const countdown = countdownRing();
    countdown.position.y = SHIP_HEIGHT / 2;
    const sparks = softPoints(SPARKS, softPointsMaterial(this.#points, false));
    sparks.name = "sparks";
    const sizes = sparks.geometry.getAttribute("size");
    for (let i = 0; i < SPARKS; i += 1) sizes.setX(i, 3 + 0.5 * ((i * 7) % 5));
    const group = new Group();
    group.name = this.#charges.length === 0 ? "jump spool" : "crew jump spool";
    group.visible = false;
    group.add(glow, countdown, sparks);
    this.group.add(group);
    this.#charges.push({ group, glow, countdown, sparks });
  }

  #drawCharge(
    drawn: DrawnCharge,
    charge: Charge,
    ship: { x: number; z: number },
    calm: boolean,
  ): void {
    drawn.group.position.set(ship.x, 0, ship.z);
    setCountdown(drawn.countdown, charge.share);
    const glow = drawn.glow.material as ShaderMaterial;
    if (glow.uniforms.phase) glow.uniforms.phase.value = calm ? 0 : charge.pulse;
    setOpacity(drawn.glow, 0.12 + 0.5 * charge.share * charge.share);
    drawn.sparks.visible = !calm;
    if (calm) return;
    const positions = drawn.sparks.geometry.getAttribute("position");
    const tints = drawn.sparks.geometry.getAttribute("tint");
    // More and brighter as the jump nears.
    const strength = 0.35 + 0.65 * charge.share;
    for (let i = 0; i < SPARKS; i += 1) {
      const spark = sparkAt(i, SPARKS, charge.pulse);
      positions.setXYZ(i, spark.x, UNDER + 0.02, spark.z);
      const bright = spark.glow * strength * (i % 3 === 0 ? 1 : 0.7);
      const tint = JUMP.clone().lerp(WHITE, i % 4 === 0 ? 0.6 : 0.2);
      tints.setXYZ(i, tint.r * bright, tint.g * bright, tint.b * bright);
    }
    positions.needsUpdate = true;
    tints.needsUpdate = true;
  }

  #addFlash(): void {
    const line = new Mesh(new PlaneGeometry(1, 1).rotateX(-Math.PI / 2), streakMaterial(1, 0, 0.3));
    line.name = "squeeze";
    const point = new Mesh(flat(2), ringMaterial(0, 1, 0.35));
    point.name = "point";
    const ring = new Mesh(flat(2), ringMaterial(RING_EDGE, 0.07, 0.02));
    ring.name = "ring";
    const trail = new Mesh(
      new PlaneGeometry(1, 1).rotateX(-Math.PI / 2),
      streakMaterial(0.25, 1.4, 0.02),
    );
    trail.name = "streak";
    const scatter = softPoints(SCATTER, softPointsMaterial(this.#points, false));
    scatter.name = "scatter";
    const sizes = scatter.geometry.getAttribute("size");
    for (let i = 0; i < SCATTER; i += 1) sizes.setX(i, 2.5 + (i % 3));
    for (const mesh of [line, point, ring, trail]) mesh.position.y = UNDER;
    const group = new Group();
    group.name = "jump flash";
    group.add(line, point, ring, trail, scatter);
    this.group.add(group);
    this.#flashes.push({ group, line, point, ring, trail, scatter });
  }

  #drawFlash(drawn: DrawnFlash, flash: Flash, calm: boolean): void {
    const shape = flashShape(flash);
    drawn.group.position.set(flash.x, 0, flash.z);
    // Local +x is the way the ship went.
    drawn.group.rotation.set(0, -flash.heading, 0);

    drawn.line.scale.set(Math.max(0.01, shape.line.length), 1, Math.max(0.01, shape.line.width));
    setOpacity(drawn.line, shape.line.length > 0 ? 1 : 0);

    drawn.point.scale.setScalar(Math.max(0.01, shape.point.size / 2));
    setOpacity(drawn.point, shape.point.opacity);

    drawn.ring.scale.setScalar(shape.ring.radius / RING_EDGE);
    setOpacity(drawn.ring, shape.ring.opacity);

    // Ahead of where the ship left; behind where it arrives, its bright end on the ship.
    const leaving = flash.kind === "leave";
    const length = Math.max(0.01, shape.trail.length);
    drawn.trail.scale.set(length, 1, leaving ? 1.1 : 0.8);
    drawn.trail.position.x = ((leaving ? 1 : -1) * length) / 2;
    drawn.trail.rotation.y = leaving ? 0 : Math.PI;
    setOpacity(drawn.trail, shape.trail.opacity);

    drawn.scatter.visible = !leaving && !calm;
    if (!drawn.scatter.visible) return;
    const positions = drawn.scatter.geometry.getAttribute("position");
    const tints = drawn.scatter.geometry.getAttribute("tint");
    for (let i = 0; i < SCATTER; i += 1) {
      // In the group's frame the ship went along +x, so heading 0 here.
      const spark = scatterAt(i, SCATTER, flash.age * FLASH_SECONDS, 0);
      positions.setXYZ(i, spark.x, UNDER + 0.02, spark.z);
      const tint = JUMP.clone().lerp(WHITE, i % 3 === 0 ? 0.5 : 0.15);
      tints.setXYZ(i, tint.r * spark.glow, tint.g * spark.glow, tint.b * spark.glow);
    }
    positions.needsUpdate = true;
    tints.needsUpdate = true;
  }
}
