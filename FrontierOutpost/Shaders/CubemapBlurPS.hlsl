// FrontierOutpost/src/liblt/Shaders/CubemapBlurPS.hlsl
//
// GameData/shader/fragment/cubemap/blur.jsl, in HLSL. The loop runs as samples says, so its samples
// take their derivatives from p outside it, which gives the level GL's textureCube chose.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"
#include "Cube.hlsli"

TEXTURECUBE(source);
float radius;
int samples;

void Shade() {
  float2 uvp = uv;
  float3 p = GetCubePosition(uvp, true);
  float4 c = ((float4)(0.0));

  float3 pdx = ddx(p);
  float3 pdy = ddy(p);
  for (int i = 0; i < samples; ++i) {
    float3 dir = noise3(float(i));
    dir = 2.0 * dir - 1.0;
    c += textureCubeGrad(source, p + 0.5 * radius * dir, pdx, pdy);
  }

  c /= float(samples);
  c += ((float4)(1.0 / 256.0)) * noise_(p);
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
