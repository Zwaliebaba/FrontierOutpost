// FrontierOutpost/src/liblt/Shaders/PlanetPS.hlsl
//
// GameData/shader/fragment/planet.jsl, in HLSL. It writes two targets.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Color.hlsli"
#include "Noise.hlsli"
#include "Lighting.hlsli"
#include "Scattering.hlsli"

int prepass;

static float3 normal = 0.0;
static float3 position = 0.0;
static float3 origin = 0.0;

TEXTURECUBE(planetMap);
float3 color1;
float3 color2;
float3 color3;
float3 color4;
float colorSeed;
float cloudLevel;
float heightMult;
float oceanLevel;

static const float kSpecular = 1.0;
static const float3 kOceanColor = float3(0.03, 0.05, 0.10);
static const float seed = 151.2;

float heightFn(float t) {
  t = pow2(sin(radians(90.0) * t) - 0.3);
  t = exp(-sqrt(6.0 * t));
  return t;
}

float visibility(TEXTURECUBE_PARAM(map), float offset, float radius, float strength) {
  float3 toStar = normalize(starPos - position);
  const float samples = 8.0;
  float v = 0.0;
  float tw = 0.0;
  for (float i = 0.0; i < samples; ++i) {
    float3 sp = normalize(lerp(vertpos, toStar, radius * (i + 1.0) / samples));
    float h = heightFn(textureCube(map, sp).x);
    float rh = h - (offset + (length(sp) - 1.0));
    v += exp(-strength * heightMult * max(0.0, rh));
  }
  return v / samples;
}

float3 gradient(float t) {
  float3 c12 = lerp(color1, color2, t);
  float3 c23 = lerp(color2, color3, t);
  float3 c34 = lerp(color3, color4, t);
  float3 c123 = lerp(c12, c23, t);
  float3 c234 = lerp(c23, c34, t);
  return lerp(c123, c234, t);
}

void Shade() {
  PREPASS

  float3 L = normalize(starPos - position);
  float3 N = normalize(normal);
  float3 V = normalize(position - eye);
  float NL = dot(N, L);
  float light = lerp(exp(-1.0 * pow(abs(1.0 - NL), 16.0)), 1.0, 0.001);

  float4 map = textureCube(planetMap, vertpos);
  float h = map.x;
  float c = 0.5 + 0.5 * sin(4.0 * map.y);
  float cloud = map.z;

  h = heightFn(h);
  float ocean = 1.0 - exp(-16.0 * max(0.0, 0.3 - h));
  float land = 1.0 - ocean;
  land *= visibility(TEXTURE_ARG(planetMap), h, 0.10, 6.0);

  float3 landColor = gradient(h) * gradient(c);
  float3 color = light * (land * landColor + ocean * kOceanColor);

  float3 R = reflect(V, N);
  color +=
    kSpecular * ocean * kOceanColor * starColor *
    cookTorrance(L, position, N, 0.01, 1.0);

  float3 clouds = 0.01 * light * ((float3)(exp(-4.0 * max(0.0, cloud - cloudLevel))));
  color = clouds + (1.0 - clouds) * color;

  float4 atmo = getScattering(origin, eye, V, length(position - eye));
  color += atmo.xyz;

  outputAlbedo(color);
  outputAlpha(1.0);
  outputNormal(N);
  outputMaterial(MATERIAL_NOSHADE);
  outputRoughness(0);
}

TwoTargets main(Varyings input) {
  ReadVaryings(input);
  ReadScatteringVaryings(input);
  normal = input.normal;
  position = input.position;
  origin = input.origin;
  Shade();
  TwoTargets targets;
  targets.color0 = fragment_color0;
  targets.color1 = fragment_color1;
  return targets;
}
