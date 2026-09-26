// FrontierOutpost/src/liblt/Shaders/WormholePS.hlsl
//
// GameData/shader/fragment/wormhole.jsl, in HLSL.

#include "Common.hlsli"
#include "Lighting.hlsli"
#include "Raytracing.hlsli"

static float3 normal = 0.0;
static float3 position = 0.0;

float time;

static const int kSamples = 8;

float field(float3 p) {
  float4 z = float4(p.xyz, 0.5);
  float4 kOffset = float4(0.4 + 0.2 * cos(time / 4.0), 0.7 + 0.2 * sin(time / 3.32), 0.5, 0.5);
  z += 0.2;
  float a = 0, l = 0;
  for (int i = 0; i < 10; ++i) {
    float m = dot(z, z);
    z = abs(z) / m - kOffset;
    a += abs(m - l);
    l = m;
  }
  a *= 0.05 * exp(-10.0 * dot(p, p));
  return a;
}

void Shade() {
  float a = 0;
  float3 ro = normalize(vertpos);
  float3 rd = normalize(eye - position);
  float t = interSphere(float4(0, 0, 0, 1.0001), ro, rd).y;
  rd *= t / float(kSamples);
  for (int i = 0; i < kSamples; ++i) {
    a += field(ro);
    ro += rd;
  }
  a /= float(kSamples);
  a = clamp(a, 0.0, 1.0);

  float3 c = 4.0 * a * float3(1.0, 1.5, 2.0);
  float alpha = 1.0 - getFoginess(length(position - eye));
  RETURN(float4(c, 1.0) * alpha);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  normal = input.normal;
  position = input.position;
  Shade();
  return fragment_color0;
}
