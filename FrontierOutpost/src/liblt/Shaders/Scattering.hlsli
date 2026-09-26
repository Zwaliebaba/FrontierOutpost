// FrontierOutpost/src/liblt/Shaders/Scattering.hlsli
//
// GameData/shader/common/scattering.jsl, in HLSL.

#ifndef SCATTERING_HLSLI
#define SCATTERING_HLSLI

#include "Lighting.hlsli"
#include "Raytracing.hlsli"

static float3 scale = 0.0;

// The varying this file reads, which a shader that includes it copies in.
void ReadScatteringVaryings(Varyings varyings) {
  scale = varyings.scale;
}

float atmoDensity;
float3 atmoTint;
float3 wavelength;

static const float kAtmoDensityMult = 50.0;
static const float kAtmoScale = 0.025;
static const float kPlanetRadius = 50000.0;
static const float kOuterRadius = 1.025;

//float3 kRayleigh = ((float3)(0.0025)) / pow(float3(0.65, 0.57, 0.475), ((float3)(4.0)));
//float3 kRayleigh = ((float3)(0.0025)) / pow(float3(0.45, 0.57, 0.65), ((float3)(4.0)));
//float3 kRayleigh = ((float3)(0.0025)) / pow(float3(0.66, 0.53, 0.40), ((float3)(4.0)));
static const float3 kRayleigh = 0.4 * float3(0.0025, 0.0045, 0.0020);
static const float3 kMie = ((float3)(0.002));

static const float kDepth = 0.125;
static const float kRcpSamples = 1.0 / 8.0;

float phase(float x) {
  return exp(-0.00287 + x * (0.459 + x * (3.83 + x * (-6.80 + 6.25 * x))));
}

float hgPhase(float d, float g) {
  return
    1.5 * (1. - g*g) / (2.0 + g*g) * (1.0 + d*d) /
    pow(abs(1.0 + g*g - 2.0*g*d), 1.5);
}

float4 shadeAtmosphere(
  float3 rd,
  float3 near,
  float3 far,
  float3 starDir,
  float occlusion)
{
  float3 color = ((float3)(0.0));
  for (float t = 0.5 * kRcpSamples; t < 1.0; t += kRcpSamples) {
    float3 p = lerp(near, far, t);
    float density = exp(-((length(p) - 1.0) / kAtmoScale) / kDepth);
    float inScatter = density * phase(1.0 - dot(starDir, normalize(p)));
    color += density * exp(-radians(720.0) * kDepth * inScatter * (wavelength * kRayleigh + kMie));
  }

  color *= kAtmoDensityMult * atmoDensity * kRcpSamples * length(far - near) / kAtmoScale;
  color *= atmoTint * starColor;
  float d = dot(rd, starDir);
  float rayPhase = 1.0 - occlusion;
  float miePhase = (1.0 - sqrt(occlusion)) * hgPhase(d, 0.75);
  color *= rayPhase * wavelength * kRayleigh + miePhase * kMie;
  return toLinear(float4(color, saturate(lum(color))));
}

float4 getScattering(float3 center, float3 ro, float3 rd, float depth) {
  float3 p = ro + rd * depth;
  float rInner = scale.x;
  float rOuter = rInner * kOuterRadius;
  float2 inner = interSphere(float4(center, rInner), ro, rd);
  float2 outer = interSphere(float4(center, rOuter), ro, rd);
  if (inner.x < 0 && inner.y > 0)
    return ((float4)(0.0));
  outer.x = max(outer.x, 0.0);
  if (inner.x > 0)
    outer.y = min(outer.y, inner.x);
  float3 tn = ((ro + rd * outer.x) - center) / rInner;
  float3 tf = ((ro + rd * outer.y) - center) / rInner;
  return shadeAtmosphere(rd, tn, tf, normalize(starPos - p), 0.0);
}

float4 getScatteringInside(float3 ro, float3 rd, float depth, float occlusion) {
  float2 innerT = interSphere(float4(0.0, 0.0, 0.0, kPlanetRadius), ro, rd);
  float2 outerT = interSphere(float4(0.0, 0.0, 0.0, kPlanetRadius * kOuterRadius), ro, rd);

  float3 near = 0.0;
  float3 far = 0.0;

  /* Space. */
  if (outerT.x > 0.0 && outerT.x < farPlane) {
    near = (ro + rd * outerT.x) / kPlanetRadius;
    far = (ro + rd * min(outerT.y, min(innerT.x, depth))) / kPlanetRadius;
  } else if (outerT.x < 0 && outerT.y > 0) {
    near = ro / kPlanetRadius;
    far = (ro + rd * min(outerT.y, depth)) / kPlanetRadius;
  } else {
    return ((float4)(0.0));
  }

  return shadeAtmosphere(rd, near, far, normalize(starPos - ro), occlusion);
}

#endif
