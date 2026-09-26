// FrontierOutpost/src/liblt/Shaders/BeamPS.hlsl
//
// GameData/shader/fragment/beam.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

static float3 position = 0.0;
static float3 normal = 0.0;

float3 eye;
float3 baseColor;
float2 size;
float t;
float thinness;

void Shade() {
  float u = uv.x;
  float v = uv.y;

  float normV = v;
  v *= size.y / 10.0;

  float uClamp = saturate(1.0 - abs(u));
  float vClamp = saturate(1.0 - normV);

  float alpha = 0.25 * exp(-abs(uv.x) * thinness);

  const float pulseFactor = 0.3;
  alpha *= (1.0 - pulseFactor) + pulseFactor * sin(v - 15.0 * t) * sin(v / 2.0 - 5.0 * t + 1.0);

  alpha +=
    0.25 * exp(-abs(uv.x) * thinness / 4.0) +
    0.125 * exp(-abs(uv.x) * thinness / 8.0);

  /* Head fade. */
  alpha *= (1.0 - exp(-5.0 * v));

  /* Head pulse. */
  alpha *= 1.0 + 2.0 * exp(-v);

  alpha *= 1.0 - exp(-10.0 * (1.0 - abs(uv.x)));

  /* Tail fade. */
  alpha *= (1.0 - exp(40.0 * (normV - 1.0)));
  alpha = saturate(alpha);
  alpha *= alpha;
  float3 color = baseColor;
  color += ((float3)(1.0)) * exp(-abs(u*4.0));
  RETURN(float4(color, 1.0) * alpha);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  position = input.position;
  normal = input.normal;
  Shade();
  return fragment_color0;
}
