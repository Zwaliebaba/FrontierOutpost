// FrontierOutpost/src/liblt/Shaders/GenPlanetskyboxPS.hlsl
//
// GameData/shader/fragment/gen/planetskybox.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"
#include "Cube.hlsli"
#include "Lighting.hlsli"

float seed;
float3 position;

#include "Scattering.hlsli"

void Shade() {
  float3 rd = GetCubePosition(uv, false);
  float3 ro = float3(0., kPlanetRadius, 0.);
  float4 c = getScatteringInside(ro, rd, farPlane, 0.);
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  ReadScatteringVaryings(input);
  Shade();
  return fragment_color0;
}
