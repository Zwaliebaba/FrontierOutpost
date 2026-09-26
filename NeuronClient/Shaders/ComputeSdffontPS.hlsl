// FrontierOutpost/src/liblt/Shaders/ComputeSdffontPS.hlsl
//
// GameData/shader/fragment/compute/sdffont.jsl, in HLSL. The loops run as radius says, so their
// samples take their derivatives from uv outside them, which gives the level GL's texture2D chose.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

TEXTURE2D(bitmap);
float2 frame;
float radius;

float getDistance(float x) {
  return x <= 0.0 ? 1e6f : 1.0 - x;
}

void Shade() {
  float d = getDistance(texture2D(bitmap, uv).x);
  float2 uvdx = ddx(uv);
  float2 uvdy = ddy(uv);
  for (float y = -radius; y <= radius; y += 1.0) {
    for (float x = -radius; x <= radius; x += 1.0) {
      float2 offset = float2(x, y);
      float sample_ = getDistance(texture2DGrad(bitmap, uv + offset / frame, uvdx, uvdy).x);
      d = min(d, length(offset) + sample_);
    }
  }

  d = min(d, radius);
  RETURN(((float4)(d)));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
