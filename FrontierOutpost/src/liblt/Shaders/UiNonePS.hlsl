// FrontierOutpost/src/liblt/Shaders/UiNonePS.hlsl
//
// GameData/shader/fragment/ui/none.jsl, in HLSL.

#include "Common.hlsli"
#include "Noise.hlsli"

TEXTURE2D(layer);

static const float kA = 1.75;
static const float kB = 1.25;

void Shade() {
  float4 c = texture2D(layer, uv);
  c.xyz = sign(c.xyz) * (1.0 - exp(-kA * pow(abs(c.xyz), ((float3)(kB)))));
  c.xyz -= noise3(noise_(uv + 3.0)) / 256.0;
  c.w = saturate(c.w);
  c.xyz /= max(0.00001, c.w);
  c.xyz = max(((float3)(0.0)), c.xyz);
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
