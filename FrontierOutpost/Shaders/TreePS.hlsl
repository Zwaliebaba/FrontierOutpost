// FrontierOutpost/src/liblt/Shaders/TreePS.hlsl
//
// GameData/shader/fragment/tree.jsl, in HLSL.

#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Scattering.hlsli"

TEXTURE2D(texture_);

static float3 attrib = 0.0;
static float3 position = 0.0;

void Shade() {
  float2 uvp = uv;
  float4 texMask = texture2D(texture_, .5 + .5*uv);
  float3 rd = position - eye;
  float4 atmo = getScatteringInside(eye, normalize(rd), length(rd), 1.);
  RETURN(float4(lerp(atmo.xyz, texMask.xyz, atmo.w), texMask.w));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  ReadScatteringVaryings(input);
  attrib = input.attrib;
  position = input.position;
  Shade();
  return fragment_color0;
}
