// FrontierOutpost/src/liblt/Shaders/PostMedianPS.hlsl
//
// GameData/shader/fragment/post/median.jsl, in HLSL. The loops run as radius says, so their
// samples take their derivatives from uv outside them, which gives the level GL's texture2D chose.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Color.hlsli"

TEXTURE2D(texture_);
int radius;
float2 rcpFrame;
float variance;

void Shade() {
  float4 final = texture2D(texture_, uv);
  float4 cMin = final;
  float4 cMax = final;
  float totalWeight = 1;

  float2 uvdx = ddx(uv);
  float2 uvdy = ddy(uv);
  for (int y = -radius; y <= radius; ++y) {
    for (int x = -radius; x <= radius; ++x) {
      if ((x != 0 || y != 0)) {
        float2 offset = float2(float(x), float(y));
        float2 coord = uv + rcpFrame * offset;
        if (coord.x >= 0.0 && coord.x <= 1.0 && coord.y >= 0.0 && coord.y <= 1.0) {
          float4 c = texture2DGrad(texture_, coord, uvdx, uvdy);
          float w = exp(-pow2(length(offset) / variance));
          cMin = lerp(cMin, min(c, cMin), w);
          cMax = lerp(cMax, max(c, cMax), w);
          final += c;
          totalWeight += 1.0;
        }
      }
    }
  }

  RETURN(((float4)(cMax)));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
