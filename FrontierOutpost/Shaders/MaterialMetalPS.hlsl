// FrontierOutpost/src/liblt/Shaders/MaterialMetalPS.hlsl
//
// GameData/shader/fragment/material/metal.jsl, in HLSL. It writes two targets.

#include "Common.hlsli"
#include "Color.hlsli"
#include "Lighting.hlsli"
#include "Math.hlsli"
#include "Texturing.hlsli"

int prepass;

static float3 normal = 0.0;
static float3 position = 0.0;
static float3 vertposscaled = 0.0;

float4x4 WORLDIT;
TEXTURE2D(depthBuffer);

TEXTURE2D(albedoMap);
TEXTURE2D(normalMap);
TEXTURE2D(detailMap);
TEXTURE2D(decal);

#include "Fdm.hlsli"

void Shade() {
  PREPASS
  EARLY_Z

  float value = emix(
    sampleTriplanar(TEXTURE_ARG(albedoMap), vertpos / 8.0).x,
    sampleTriplanar(TEXTURE_ARG(albedoMap), vertposscaled / 32.0).x,
    0.5);

  float dn = length(normal - normalize(normal));
  float dd = exp(-16.0 * value);
  float3 big = ((float3)(sampleTriplanar(TEXTURE_ARG(albedoMap), vertpos / 12.0).xxx));
  float color =
    sampleTriplanar(TEXTURE_ARG(albedoMap), vertpos / 16.0 + 3).x *
    sampleTriplanar(TEXTURE_ARG(albedoMap), vertpos / 32.0 + 7).x;
  float3 c = lerp(((float3)(1.00)), big, 0.75) + dd;
  // c = mix(c, 3.0 * vec3(1.0, 0.3, 0.1), step(0.3, color));
  c *= lerp(((float3)(0.05)), ((float3)(1.0)), (exp(-4096.0 * dn)));

  float spec = 1.0 - saturate(dd);

  c *= pow2(uv.x * sampleFDM(TEXTURE_ARG(detailMap), vertpos).x);
  c *= 16.0;

  float3 bump = sampleTriplanarBumpmap(TEXTURE_ARG(normalMap), vertpos / 8.0);
  bump = mul(WORLDIT, float4(bump, 0)).xyz;
  float3 n = normalize(lerp(normalize(normal), normalize(bump), 0.0001));
  float r = lerp(0.01, 0.2, spec);

  outputAlbedo(c);
  outputAlpha(1.0);
  outputNormal(n);
  outputMaterial(MATERIAL_COOKT);
  outputRoughness(r);
}

TwoTargets main(Varyings input) {
  ReadVaryings(input);
  normal = input.normal;
  position = input.position;
  vertposscaled = input.vertposscaled;
  Shade();
  TwoTargets targets;
  targets.color0 = fragment_color0;
  targets.color1 = fragment_color1;
  return targets;
}
