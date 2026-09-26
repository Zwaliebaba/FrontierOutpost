// FrontierOutpost/src/liblt/Shaders/MaterialLambertPS.hlsl
//
// GameData/shader/fragment/material/lambert.jsl, in HLSL. It writes the G-buffer's two targets.

#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"
#include "Texturing.hlsli"

static float3 normal = 0.0;
static float3 origin = 0.0;
static float3 position = 0.0;
static float3 vertposscaled = 0.0;
static float3 scale = 0.0;

float4x4 WORLDIT;
TEXTURE2D(albedoMap);
TEXTURE2D(depthBuffer);
TEXTURE2D(normalMap);
float objectRadius;
int prepass;

#include "Fdm.hlsli"

void Shade() {
  PREPASS
  EARLY_Z

  float3 c = sampleFDM(TEXTURE_ARG(albedoMap), vertposscaled).xyz;
  float a = sampleTriplanar(TEXTURE_ARG(albedoMap), 0.5 * vertpos).x;

  c = (c / avg(c)) * pow(abs(3.25 * avg(c)), 4.0);
  float3 bump = sampleFDMBumpmap(TEXTURE_ARG(normalMap), vertposscaled);
  float3 n = normalize(lerp(normalize(normal), normalize(mul(WORLDIT, float4(bump, 0)).xyz), 0.75));
  c *= pow4(uv.x);
  float r = 1.0;

  outputAlbedo(c);
  outputAlpha(1.0);
  outputNormal(n);
  outputMaterial(MATERIAL_COOKT);
  outputRoughness(r);
}

TwoTargets main(Varyings input) {
  ReadVaryings(input);
  normal = input.normal;
  origin = input.origin;
  position = input.position;
  vertposscaled = input.vertposscaled;
  scale = input.scale;
  Shade();
  TwoTargets targets;
  targets.color0 = fragment_color0;
  targets.color1 = fragment_color1;
  return targets;
}
