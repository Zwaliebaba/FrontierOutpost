// FrontierOutpost/src/liblt/Shaders/FilterDirtPS.hlsl
//
// GameData/shader/fragment/filter_dirt.jsl, in HLSL.

#include "Common.hlsli"
#include "Noise.hlsli"

TEXTURE2D(texture_);

void Shade() {
  float2 uvp = abs(uv - 0.5);
  float4 c = texture2D(texture_, uv);
  c.xyz *= 1.0 - pow(abs(fcnoise(uvp * 4.0, 3.0, 8, 1.3)), 4.0);
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
