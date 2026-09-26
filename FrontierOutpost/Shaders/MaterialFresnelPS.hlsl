// FrontierOutpost/src/liblt/Shaders/MaterialFresnelPS.hlsl
//
// GameData/shader/fragment/material/fresnel.jsl, in HLSL. It writes two targets.

#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Math.hlsli"
#include "Texturing.hlsli"

int prepass;

static float3 normal = 0.0;
static float3 position = 0.0;
static float3 vertposscaled = 0.0;

float4x4 WORLDIT;
TEXTURE2D(albedoMap);
TEXTURE2D(depthBuffer);
TEXTURE2D(normalMap);

#include "Fdm.hlsli"

void Shade() {
  PREPASS
  EARLY_Z

  float3 c = ((float3)(1.0));
  float3 bump = sampleFDMBumpmap(TEXTURE_ARG(normalMap), vertposscaled / 512.0);
  float3 n = normalize(normal);

  outputAlbedo(c);
  outputAlpha(1.0);
  outputNormal(n);
  outputMaterial(MATERIAL_ICE);
  outputRoughness(1.0);
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
