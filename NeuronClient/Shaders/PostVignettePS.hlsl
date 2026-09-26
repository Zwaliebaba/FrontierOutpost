// FrontierOutpost/src/liblt/Shaders/PostVignettePS.hlsl
//
// GameData/shader/fragment/post/vignette.jsl, in HLSL.

#include "Common.hlsli"
#include "Color.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

TEXTURE2D(texture_);
float hardness;
float opacity;

void Shade() {
  float4 c = texture2D(texture_, uv);
  float mask =
    (1.0 - exp(-hardness * (1.0 - abs(2.0 * uv.x - 1.0)))) *
    (1.0 - exp(-hardness * (1.0 - abs(2.0 * uv.y - 1.0))));
  c *= lerp(1.0, mask, opacity);
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
