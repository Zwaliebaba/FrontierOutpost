// FrontierOutpost/src/liblt/Shaders/MaterialWaterPS.hlsl
//
// GameData/shader/fragment/material/water.jsl, in HLSL.

#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Math.hlsli"
#include "Scattering.hlsli"
#include "Softparticle.hlsli"
#include "Texturing.hlsli"

TEXTURE2D(normalMap);
float time;

static float3 normal = 0.0;
static float3 position = 0.0;

#include "Fdm.hlsli"

void Shade() {
  float3 bump = (sampleFDMTexture(TEXTURE_ARG(normalMap), vertpos.xz * 1.0 + time * 0.130).xyz +
               sampleFDMTexture(TEXTURE_ARG(normalMap), vertpos.xz / 8.0 + time * 0.277).xyz) / 2.0;
  bump = normalize(2.0 * bump - 1.0);
  bump = lerp(bump, float3(0.0, 0.0, 1.0), 0.95);
  float3 n = bump.x * float3(1.0, 0.0, 0) + bump.y * float3(0.0, 0.0, 1.0) + bump.z * normal;
  n = normalize(normal);

  float3 view = normalize(position - eye);
  float3 refl = normalize(reflect(view, n));
  refl.y = abs(refl.y);
  float2 uv = GetSSPosition();

  float delta = GetDepthDifference();
  float depth = 1.0 - exp(-20000.0 * delta);
  float3 color = lerp(float3(37.0, 114.0, 96.0) / 255.0,
                   float3(31.0, 052.0, 65.0) / 255.0, depth);

  float fresnel = 1.0 - pow4(saturate(-dot(normalize(position - eye), n)));
  float3 env = toLinear(textureCube(envMap, refl).xyz);
  color = lerp(color, env, 0.25 + 0.75 * fresnel);

  float3 rd = (position - eye);
  // vec4 atmo = getScatteringInside(eye, normalize(rd), length(rd), 0.);
  // color = mix(color.xyz, atmo.xyz, atmo.w);
  float alpha = depth;
  float fogDepth = length(position - eye);
  float3 fog = getFog(eye, normalize(position - eye), fogDepth, 0.0);
  color = lerp(color, fog, getFoginess(fogDepth));
  color = toGamma(color);

  RETURN(float4(color, 1.0) * alpha);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  ReadScatteringVaryings(input);
  ReadSoftparticleVaryings(input);
  normal = input.normal;
  position = input.position;
  Shade();
  return fragment_color0;
}
