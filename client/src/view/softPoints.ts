/**
 * Soft round points: each fades from its middle to nothing at its edge, where the stars are crisp
 * squares, so neither flight dust nor a jump's sparks is taken for a star. Each point has its own
 * colour (`tint`, linear RGB; dimmer is fainter, as the blending adds) and width (`size`, CSS
 * pixels). Additive, and none hides what is behind it.
 */
import { AdditiveBlending, BufferAttribute, BufferGeometry, Points, ShaderMaterial } from "three";

export interface SoftPointsUniforms {
  [name: string]: { value: number };
  pixelRatio: { value: number };
  opacity: { value: number };
}

/** `toneMapped` false keeps an effect's colour as specified, as the other effects do. */
export function softPointsMaterial(
  uniforms: SoftPointsUniforms,
  toneMapped = true,
): ShaderMaterial {
  return new ShaderMaterial({
    toneMapped,
    uniforms,
    vertexShader: /* glsl */ `
      attribute vec3 tint;
      attribute float size;
      uniform float pixelRatio;
      varying vec3 vTint;
      void main() {
        vTint = tint;
        gl_PointSize = size * pixelRatio;
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }
    `,
    fragmentShader: /* glsl */ `
      uniform float opacity;
      varying vec3 vTint;
      void main() {
        vec2 p = gl_PointCoord * 2.0 - 1.0;
        float r = dot(p, p);
        if (r > 1.0) discard;
        float fall = 1.0 - r;
        gl_FragColor = vec4(vTint, fall * fall * opacity);
        #include <tonemapping_fragment>
        #include <colorspace_fragment>
      }
    `,
    blending: AdditiveBlending,
    transparent: true,
    depthWrite: false,
  });
}

/** `count` soft points, positions, tints and sizes all left for the caller to write. */
export function softPoints(count: number, material: ShaderMaterial): Points {
  const points = new Points(
    new BufferGeometry()
      .setAttribute("position", new BufferAttribute(new Float32Array(count * 3), 3))
      .setAttribute("tint", new BufferAttribute(new Float32Array(count * 3), 3))
      .setAttribute("size", new BufferAttribute(new Float32Array(count), 1)),
    material,
  );
  // The points move every frame, so the bounds three.js computed once would cull them.
  points.frustumCulled = false;
  return points;
}
