// FrontierOutpost/src/liblt/Shaders/GenPlatingPS.hlsl
//
// GameData/shader/fragment/gen/plating.jsl, in HLSL. It writes two targets.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

float seed;

float quantize(float f, float l) {
  return floor(f * l) / l;
}

float worley(float2 p) {
  float2 c = floor(p);
  float d1 = 100.0;
  float d2 = 100.0;
  float d3 = 100.0;
  const float r = 2.0;
  for (float xo = -r; xo <= r; ++xo) {
    for (float yo = -r; yo <= r; ++yo) {
      float2 m = float2(xo, yo);
      float2 n = c + m + r*(noise2(noise_(c + m) + seed) - 0.5) - p;
      float d = max(abs(n.x), abs(n.y));
      if (d < d1) {
        d3 = d2;
        d2 = d1;
        d1 = d;
      } else if (d < d2) {
        d3 = d2;
        d2 = d;
      } else if (d < d3)
        d3 = d;
    }
  }
  return d3;
}

float plating(float2 p, int octaves) {
  float t = 1.0;
  for (int i = 0; i < octaves; ++i) {
    t = min(t, worley(p));
    p = 2.0 * p + 0.532;
  }
  return t;
}

void Shade() {
  float2 uvp = abs(uv - 0.5);
  float v = 1.5;
  float d = 1.0 - exp(-2.0 * abs(sin(4.0 * plating(uvp, 4))));
  v *= 0.1 + 0.9 * quantize(abs(sin(3.0 + 4.0 * plating(uvp + 1.0, 5))), 2.0);
  v *= 1.0 - exp(-16.0 * abs(sin(8.0 * plating(uvp * 4.0 + 3.0, 4))));

  const float nStrength = 1.0;
  float3 dx = float3(ddx(uv.x), 0, nStrength * ddx(v));
  float3 dy = float3(0, ddy(uv.y), nStrength * ddy(v));
  float3 normal = normalize(cross(dy, dx));

  fragment_color0 = ((float4)(saturate(v)));
  fragment_color1 = float4(0.5 + 0.5 * normal, 1.0);
}

TwoTargets main(Varyings input) {
  ReadVaryings(input);
  Shade();
  TwoTargets targets;
  targets.color0 = fragment_color0;
  targets.color1 = fragment_color1;
  return targets;
}
