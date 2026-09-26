// FrontierOutpost/src/liblt/Shaders/Smaa2PS.hlsl
//
// GameData/shader/fragment/smaa_2.jsl, in HLSL: SMAA's blending weight calculation.

#include "Common.hlsli"

#define SMAA_ONLY_COMPILE_PS
#include "Smaa.hlsli"

TEXTURE2D(edgeBuffer);
TEXTURE2D(areaBuffer);
TEXTURE2D(searchBuffer);

static float2 pixcoord = 0.0;
static float4 offset[3];

void Shade() {
  float4 c = SMAABlendingWeightCalculationPS(uv, pixcoord, offset,
    SMAATexturePair(TEXTURE_ARG(edgeBuffer)), SMAATexturePair(TEXTURE_ARG(areaBuffer)),
    SMAATexturePair(TEXTURE_ARG(searchBuffer)), ((int4)(0)));
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  pixcoord = input.pixcoord;
  offset[0] = input.offset[0];
  offset[1] = input.offset[1];
  offset[2] = input.offset[2];
  Shade();
  return fragment_color0;
}
