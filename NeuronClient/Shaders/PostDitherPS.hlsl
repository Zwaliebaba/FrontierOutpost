// FrontierOutpost/src/liblt/Shaders/PostDitherPS.hlsl
//
// GameData/shader/fragment/post/dither.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

TEXTURE2D(texture_);

void Shade() {
  float3 c = texture2D(texture_, uv).xyz;
  c -= (2.0 * noise3(noise_(uv * 16.0)) - 1.0) / 256.0;
  RETURN(float4(saturate(c), 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
