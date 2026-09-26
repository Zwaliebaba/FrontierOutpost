// FrontierOutpost/src/liblt/Shaders/Lighting.hlsli
//
// GameData/shader/common/lighting.jsl, in HLSL.

#ifndef LIGHTING_HLSLI
#define LIGHTING_HLSLI

#include "Noise.hlsli"
#include "Color.hlsli"
#include "Math.hlsli"
#include "Texturing.hlsli"

TEXTURECUBE(envMap);
TEXTURECUBE(envMapLF);
TEXTURECUBE(irMap);

float3 ambient;
float3 eye;
float fogDensity;
float2 rcpFrame;
float3 starColor;
float3 starPos;

static const float kDynamicLightMult = 16.0;

float cookTorrance(float3 L, float3 position, float3 N, float R2, float spec) {
  float3 V = normalize(eye - position);
  float3 H = normalize(L + V);

  float NL = max(0.0000001, dot(N, L));
  float NH = max(0.000001, dot(N, H));
  float NV = max(0.000001, dot(N, V));
  float VH = max(0.000001, dot(V, H));
  float NH2 = NH * NH;

  float G1 = (2.0 * NH * NV) / VH;
  float G2 = (2.0 * NH * NL) / VH;
  float geom = min(1.0, min(G1, G2));

  float denom = 4.0 * R2 * NH2 * NH2;
  float num = exp((NH2 - 1.0) / (R2 * NH2));
  float rough = num / denom;

  float fs = pow(saturate(1.0 - VH), 5.0);
  float fsg = exp2((-5.55473 * VH - 6.98316) * VH);
  float fresnel = lerp(1.0, fsg, 0.25);

  float speccoef = fresnel * (geom * rough) / (NV * NL * 3.14159);
  return NL * lerp(1.0, speccoef, spec);
}

float3 getMetalPBR(float3 p, float3 n, float rough) {
  const int kSamples = 128;
  rough *= rough;
  float3 c = ((float3)(0));
  float tw = 0.0;
  for (int i = 0; i < kSamples; ++i) {
    float3 dir = normalize(2.0 * noise3(float(i)) - 1.0);
    float3 sample_ = toLinear(textureCubeLod(irMap, dir, 0.0).xyz);
    float w = cookTorrance(dir, p, n, rough, 1.0);
    c += w * sample_;
    tw += w;
  }
  return c / max(0.00001, tw);
}

float3 getFog(float3 ro, float3 rd, float depth, float occlusion) {
  float3 c = ((float3)(0.0));
  float d = 1.0 - dot(rd, normalize(starPos - ro));
  float l = exp(-pow2(8.0 * d));
  float lod = lerp(2.0, 4.0, saturate((farPlane - depth) / farPlane));
  c += 0.1 * (1.0 - occlusion) * l * toLinear(texLod(TEXTURE_ARG(envMap), rd, lod).xyz);
  float3 bg = toLinear(texLod(TEXTURE_ARG(irMap), rd, lod).xyz);
  c += bg;
  return c;
}

float getFoginess(float depth) {
  float d = 1.0 * fogDensity * (depth / farPlane);

  /* NOTE - Do not fking change the power.  You've gone back and forth on
   *        it a million times, and I'm tired of you getting it wrong.
   *  0.5 - TOO GLOBAL.  Obfuscates near objects way too much.
   *        Totally ruins the beauty of objects in dust.
   *  2.0 - TOO LOCAL.  Can't see anything long-distance. Total loss of
   *        context in dust.
   *  1.0 - JUST RIGHT.  Leave it.  Stop poking it. */
  return 1.0 - exp(-pow(abs(d), 1.0));

}

#endif
