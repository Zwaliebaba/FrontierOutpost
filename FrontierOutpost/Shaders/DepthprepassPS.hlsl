// FrontierOutpost/src/liblt/Shaders/DepthprepassPS.hlsl
//
// GameData/shader/fragment/depthprepass.jsl, in HLSL.

#include "Common.hlsli"

void Shade() {
  RETURN(((float4)(linearDepth)));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
