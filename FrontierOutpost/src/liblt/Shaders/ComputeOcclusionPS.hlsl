// FrontierOutpost/src/liblt/Shaders/ComputeOcclusionPS.hlsl
//
// GameData/shader/fragment/compute/occlusion.jsl, in HLSL. Its one output is a float, as the .jsl's
// #output declared it; PlateMesh renders it into an R32F texture. The loops run as sDim says, and
// sample where no derivative varies, so they take level 0 as GL's texture2D did.
//
// The .jsl summed every surfel in one draw, which on a big hull ran long enough for Windows to
// reset the GPU (TDR). Here a draw sums only the surfel rows [sRowBegin, sRowEnd), and outputs the
// raw sum: PlateMesh adds the draws up with additive blending, and takes the visibility,
// exp(-pow(sum, 0.75)), of the total on the CPU.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

static float total = 0.0;

int sDim;
int sRowBegin;
int sRowEnd;
TEXTURE2D(sPointBuffer);
TEXTURE2D(sNormalBuffer);
TEXTURE2D(vPointBuffer);
TEXTURE2D(vNormalBuffer);

void Shade() {
  float3 p = texture2D(vPointBuffer, uv).xyz;
  float3 n = texture2D(vNormalBuffer, uv).xyz;

  for (int y = sRowBegin; y < sRowEnd; ++y) {
    float v = (float(y) + 0.5) / float(sDim);
    for (int x = 0; x < sDim; ++x) {
      float u = (float(x) + 0.5) / float(sDim);

      float4 sp = texture2DLod(sPointBuffer, float2(u, v), 0.0);
      float4 sn = texture2DLod(sNormalBuffer, float2(u, v), 0.0);
      float area = sp.w;

      float3 r = sp.xyz - p;
      float d = dot(r, r) + 1e-16;
      r *= rsqrt(d);

      float value = 1.0 - rsqrt(area / (d * d) + 1.0);
      value *= abs(dot(r, sn.xyz));
      value *= saturate(4.0 * dot(r, n));
      total += value;
    }
  }
}

float main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return total;
}
