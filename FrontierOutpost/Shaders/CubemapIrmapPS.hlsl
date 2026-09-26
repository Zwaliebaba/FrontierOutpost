// FrontierOutpost/src/liblt/Shaders/CubemapIrmapPS.hlsl
//
// GameData/shader/fragment/cubemap/irmap.jsl, in HLSL.

#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"
#include "Cube.hlsli"

TEXTURECUBE(source);
TEXTURE2D(sampleBuffer);
float angle;
int samples;

void Shade() {
  float2 uvp = uv;
  float3 p = GetCubePosition(uvp, true);
  float4 c = ((float4)(0.0));

  for (int i = 0; i < samples; ++i) {
    float u = float(i + 1) / float(samples + 1);
    float3 sample_ = texture2DLod(sampleBuffer, float2(u, 0.5), 0.0).xyz;
    sample_ = normalize(lerp(p, sample_, angle));
    sample_ *= sign(dot(sample_, p));
    c += textureCubeLod(source, sample_, 0.0);
  }

  RETURN(c / float(samples));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
