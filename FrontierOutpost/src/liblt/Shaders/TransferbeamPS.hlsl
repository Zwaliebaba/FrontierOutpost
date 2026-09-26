// FrontierOutpost/src/liblt/Shaders/TransferbeamPS.hlsl
//
// GameData/shader/fragment/transferbeam.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

static float3 position = 0.0;
static float3 normal = 0.0;

float4 color;
float3 eye;
float2 size;
float t;

void Shade() {
  float2 uvp = uv;
  float d = max(0.0, pow2(abs(uvp.x)));
  float alpha = 0.0;
  alpha += 4.0 * exp(-pow2(1024.0 * d));
  alpha *= 1.0 - 0.2 * log(1e-8 + noise_(t));
  float power = pow2(snoise(32.0 * uv.y + 16.0 * t));
  alpha *= power;
  alpha += exp(-6.0 * sqrt(d));

  alpha *= (1.0 - exp(-32.0 * uv.y));
  alpha *= (1.0 - exp(-32.0 * (1.0 - uv.y)));

  float4 c = color * alpha;
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  position = input.position;
  normal = input.normal;
  Shade();
  return fragment_color0;
}
