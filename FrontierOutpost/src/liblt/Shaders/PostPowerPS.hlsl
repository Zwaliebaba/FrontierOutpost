// FrontierOutpost/src/liblt/Shaders/PostPowerPS.hlsl
//
// GameData/shader/fragment/post/power.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Color.hlsli"

TEXTURE2D(texture_);
float3 power;

void Shade() {
  float4 c = texture2D(texture_, uv);
  RETURN(pow(abs(c), float4(power, 1.0)));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
