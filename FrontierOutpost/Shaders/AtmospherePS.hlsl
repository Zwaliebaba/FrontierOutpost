// FrontierOutpost/src/liblt/Shaders/AtmospherePS.hlsl
//
// GameData/shader/fragment/atmosphere.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Lighting.hlsli"
#include "Scattering.hlsli"

static float3 normal = 0.0;
static float3 position = 0.0;
static float3 origin = 0.0;

void Shade() {
  float3 rd = position - eye;
  float depth = length(rd);
  float4 scattering = toGamma(getScattering(origin, eye, normalize(rd), depth));
  RETURN(float4(scattering.xyz, 1.0) * (1.0 - getFoginess(depth)));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  ReadScatteringVaryings(input);
  normal = input.normal;
  position = input.position;
  origin = input.origin;
  Shade();
  return fragment_color0;
}
