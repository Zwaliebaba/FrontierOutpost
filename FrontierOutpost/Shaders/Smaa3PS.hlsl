// FrontierOutpost/src/liblt/Shaders/Smaa3PS.hlsl
//
// GameData/shader/fragment/smaa_3.jsl, in HLSL: SMAA's neighborhood blending.

#include "Common.hlsli"

#define SMAA_ONLY_COMPILE_PS
#include "Smaa.hlsli"

TEXTURE2D(texture_);
TEXTURE2D(blendWeights);

static float4 offset[2];

void Shade() {
  float4 c = SMAANeighborhoodBlendingPS(uv, offset, SMAATexturePair(TEXTURE_ARG(texture_)),
    SMAATexturePair(TEXTURE_ARG(blendWeights)));
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  offset[0] = input.offset[0];
  offset[1] = input.offset[1];
  Shade();
  return fragment_color0;
}
