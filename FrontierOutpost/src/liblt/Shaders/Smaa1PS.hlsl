// FrontierOutpost/src/liblt/Shaders/Smaa1PS.hlsl
//
// GameData/shader/fragment/smaa_1.jsl, in HLSL: SMAA's colour edge detection.

#include "Common.hlsli"

#define SMAA_ONLY_COMPILE_PS
#include "Smaa.hlsli"

TEXTURE2D(texture_);
TEXTURE2D(depthBuffer);

static float4 offset[3];

void Shade() {
  float4 c = SMAAColorEdgeDetectionPS(uv, offset, SMAATexturePair(TEXTURE_ARG(texture_)));
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  offset[0] = input.offset[0];
  offset[1] = input.offset[1];
  offset[2] = input.offset[2];
  Shade();
  return fragment_color0;
}
