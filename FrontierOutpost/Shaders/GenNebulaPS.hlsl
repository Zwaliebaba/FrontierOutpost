// FrontierOutpost/src/liblt/Shaders/GenNebulaPS.hlsl
//
// GameData/shader/fragment/gen/nebula.jsl, in HLSL.

#include "Common.hlsli"
#include "Color.hlsli"
#include "Cube.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"
#include "Quat.hlsli"

float seed;
float roughness;
float3 color1;
float3 color2;
float3 starDir;
float4 offset;
float4 orientation1;
float4 orientation2;

static const float kDepth = 2.0;
static const float kEmission = 1.50;
static const float kSamples = 128.0;

float magic(float3 p, float r) {
  float4 z = float4(0.25 * p + 0.75, 0.5) + ((float4)(0.2));
  float a = 0.0, l = 0.0, tw = 0.0, w = 1.0;
  float4 c = float4(0.3, 0.5, 0.4, 0.5);
  for (int i = 0; i < 24; ++i) {
    float m = dot(z, z);
    z = abs(z) / dot(z, z) - c;
    z += 0.01 * log(1.e-10 + noise4(float(i) + seed));
    z *= 1.00 - 0.1 * (2.0 * noise_(float(i) + seed * 2.0 + 32.0) - 1.0);
    z += 0.1 * sin(z);
    a += w * exp(-4.0 * pow2(abs(l - m)));
    tw += w;
    if (i > 3) w *= r;
    l = m;
  }
  return 30.0 * a / tw;
}

float emission(float3 p) {
  return exp(-8.0 * fsnoise(p, 4, 2.0));
}

float absorption(float3 p) {
  return 0.5 + 0.5 * cos(magic(0.75 + 0.1 * p, 0.72));
}

float4 generate(float3 dir, float4 q1, float4 q2) {
  float3 color = ((float3)(0.0));
  float density = 0.0;
  float w = 1.0 / float(kSamples);

  /* Emission. */ {
    float mask = 1.0;
    float3 p = quatMul(q1, dir);
    float t = kEmission * emission(dir);
    t *= mask;
    t += 0.05;
    color += t * color1;
  }

  /* Central Star. */ {
    float d = 1.0 - dot(dir, starDir);
    float t = 2.0 * exp(-512.0 * d);
    color += t * color1;
  }

  /* Absorption. */ {
    float mask = 1.0;
    // mask *= exp(-0.5 * pow(abs(pos.y), 2.0));

    for (float i = 0.0; i < kSamples; ++i) {
      float3 pos = (1.0 + kDepth * (i * w)) * dir;
      float t = absorption(quatMul(q2, pos));
      t = exp(-t);

      float3 wv = normalize(pow(abs(color2), ((float3)(2.0))));
      float3 vs = exp(-(16.0 * wv * max(0.0, abs(t - 0.95) - 0.00)));

      vs *= mask;
      density += w * avg(vs);

      vs -= 1.3 * exp(-pow(abs(16.0 * abs(t - 0.90)), 0.75));
      color *= exp(-6.0 * w * vs);
    }
  }

  return float4(color, density);
}

void Shade() {
  float3 dir = GetCubePosition(uv, false);
  float4 q1 = quatRotation(orientation1.xyz, orientation1.w);
  float4 q2 = quatRotation(orientation2.xyz, orientation2.w);
  float4 c = generate(dir, q1, q2);
  // c.xyz = normalize(c.xyz) * pow(length(c.xyz), 0.5);
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
