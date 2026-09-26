// FrontierOutpost/src/liblt/Shaders/LightPointPS.hlsl
//
// GameData/shader/fragment/light/point.jsl, in HLSL.

#include "Common.hlsli"
#include "Deferred.hlsli"
#include "Texturing.hlsli"
#include "Lighting.hlsli"

TEXTURE2D(normalBuffer);

static const int kMaxLights = 16;
float3 color[kMaxLights];
float3 center[kMaxLights];

static const float kMinDistance = 0.0001;

void Shade() {
  float2 uvp = gl_FragCoord.xy * rcpFrame;
  float depth = GetDepthNormalized();
  if (depth >= 1.0)
    discard;

  float4 color1 = texture2D(normalBuffer, uvp);
  float3 N = decodeNormal(color1.xy);
  float R = color1.z;
  int material = int(kMaterialCount * color1.w);

  float3 p = worldRayO + depth * worldRayD;
  float3 light = ((float3)(0.0));

  /* Lambertian. */
  if (material == MATERIAL_PHONG) {
    for (int i = 0; i < kMaxLights; ++i) {
      float3 L = center[i] - p;
      float Lmag = 1.0 / max(kMinDistance, length(L));
      light += color[i] * Lmag *
        saturate(dot(L * Lmag, N));
    }
  }

  /* Ice. */
  else if (material == MATERIAL_ICE) {
    const float3 specColor = float3(1.5, 1.8, 2.0);
    const float3 transColor = float3(0.5, 1.0, 1.5);

    float3 V = normalize(p - eye);
    float3 refl = normalize(reflect(V, N));
    float3 refr = normalize(refract(V, N, 1.0 / 1.15));
    float fresnel = pow2(1.0 - saturate(dot(N, refl)));

    for (int i = 0; i < kMaxLights; ++i) {
      float3 L = center[i] - p;
      float Lmag = 1.0 / max(kMinDistance, length(L));
      L *= Lmag;

      float specular = exp(-256.0 * (1.0 - dot(L, refl)));
      float transmission =
        0.1 * exp(-32.0 * (1.0 - dot(L, refr))) +
        1.0 * exp(-512.0 * (1.0 - dot(L, refr)));

      light += color[i] * Lmag * lerp(
        transmission * transColor,
        specular * specColor,
        fresnel);
    }
  }

  /* Metal. */
  else if (material == MATERIAL_COOKT) {
    R *= R;
    for (int i = 0; i < kMaxLights; ++i) {
      float3 L = center[i] - p;
      float Lmag = 1.0 / max(kMinDistance, length(L));
      light += color[i] * Lmag *
        cookTorrance(L * Lmag, p, N, R, 1.0);
    }
  }

  light *= kDynamicLightMult;
  RETURN(float4(light, 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  ReadDeferredVaryings(input);
  Shade();
  return fragment_color0;
}
