// FrontierOutpost/src/liblt/Shaders/GenPlanetPS.hlsl
//
// GameData/shader/fragment/gen/planet.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"
#include "Cube.hlsli"

float seed;
float4 coef;
float freq;
float power;

float genHeight(float3 p) {
  float4 z = float4(p / 4.0 + 0.75, 0.3);
  float a = 0.0, l = 0.0, w = 1.0;
  for (int i = 0; i < 32; ++i) {
    float m = dot(z, z);
    z = abs(z) / m - float4(0.4, 0.5, 0.6, 0.3);
    z += 0.1 * log(1.0e-10 + noise4(float(i) + seed));
    z *= 1.0 + 0.25 * noise_(float(i) + seed * 2.0 + 32.0);
    z = z.yzwx;
    m = coef.x*z.x*z.x + coef.y*z.y*z.y + coef.z*z.z*z.z + coef.w*z.w*z.w;
    if (i > 0) {
      a += w * exp(-abs(m - l));
      w *= 0.8 + 0.2 * (2.0*noise_(seed + float(i)*3.3) - 1.0);
    }
    l = m;
  }
  return gain(pow(abs(0.5 + 0.5 * sin(freq * a)), power), 4.0);
}

float genClouds(float3 p) {
  p += 0.5 * float3(fcnoise(p, seed + 1.0, 4, 1.3),
                 fcnoise(p, seed + 5.0, 4, 1.3),
                 fcnoise(p, seed + 8.0, 4, 1.3));
  return 0.5 + 0.5 * sin(8.0 * frcnoise(p, seed + 6.0, 12, 1.4));
}

float genColor(float3 p) {
  float4 z = float4(p / 4.0 + 0.75, 0.3);
  float a = 0.0, l = 0.0, w = 1.0;
  for (int i = 0; i < 24; ++i) {
    float m = dot(z, z);
    z = abs(z) / m - float4(0.4, 0.5, 0.6, 0.4);
    z += 0.1 * log(1.0e-10 + noise4(float(i) + seed + 58.329));
    z *= 1.0 + 0.25 * noise_(float(i) + seed * 5.0 + 12.0);
    z = z.yzwx;
    m = coef.x*z.x*z.x + coef.y*z.y*z.y + coef.z*z.z*z.z + coef.w*z.w*z.w;
    a += w * exp(-m);
    w *= 0.85;
    l = m;
  }
  return 0.5 + 0.5 * sin(4.0 * a);
}

void Shade() {
  float3 p = GetCubePosition(uv, false);
  RETURN(float4(genHeight(p), genColor(p), genClouds(p), 0.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
