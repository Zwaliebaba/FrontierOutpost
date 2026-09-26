// FrontierOutpost/src/liblt/Shaders/LightGlobalPS.hlsl
//
// GameData/shader/fragment/light/global.jsl, in HLSL. The branches on material, which varies by
// pixel, are flattened: two of them sample with implicit derivatives, which FXC takes only outside
// flow control. Each pixel's light is what its branch alone gave.

#include "Common.hlsli"
#include "Deferred.hlsli"
#include "Lighting.hlsli"
#include "Texturing.hlsli"

TEXTURE2D(normalBuffer);

void Shade() {
  float depth = GetDepthNormalized();
  float3 color = toLinear(starColor);
  float3 light = ((float3)(0.0));

  float4 normalmat = texture2D(normalBuffer, uv);
  float3 N = decodeNormal(normalmat.xy);
  float roughness = normalmat.z;
  int material = int(kMaterialCount * normalmat.w);

  float3 p = worldRayO + depth * worldRayD;
  float fog = getFoginess(length(p));

  /* Lambertian. */
  [flatten] if (material == MATERIAL_PHONG) {
    float3 V = normalize(p - eye);
    float3 R = normalize(reflect(V, N));
    float3 L = normalize(starPos);
    float NL = dot(N, L);
    float l = exp(-pow(abs(1.0 - NL), 12.0));
    float3 env = toLinear(textureCube(envMapLF, N).xyz);

    light += 8.0 * l;
    light += 8.0 * env;
  }

  /* Ice. */
  else [flatten] if (material == MATERIAL_ICE) {
    const float3 specColor = float3(1.5, 1.8, 2.0);
    const float3 transColor = float3(0.5, 1.0, 1.5);

    float3 V = normalize(p - eye);
    float3 L = normalize(starPos - p);
    float3 refl = normalize(reflect(V, N));
    float3 refr = normalize(refract(V, N, 1.0 / 1.15));
    float fresnel = pow2(1.0 - saturate(dot(N, refl)));

    light +=
      0.3 * toLinear(textureCube(envMap, refr).xyz) +
      4.0 * fresnel * specColor * toLinear(textureCube(envMap, refl).xyz);

    float specular = 16.0 * exp(-sqrt(256.0 * (1.0 - dot(L, refl))));
    float transmission =
      0.1 * exp(-32.0 * (1.0 - dot(L, refr))) +
      4.0 * exp(-512.0 * (1.0 - dot(L, refr)));

    light += (color / length(L)) * lerp(
      transmission * transColor,
      specular * specColor,
      fresnel);
  }

  /* Metal. */
  else [flatten] if (material == MATERIAL_COOKT) {
    float3 V = normalize(p - eye);
    float3 R = normalize(reflect(V, N));
    float3 L = normalize(starPos);
    // light += color * cookTorrance(L, p, N, pow2(roughness), 1.0);
    float3 bg = toLinear(texLod(TEXTURE_ARG(irMap), R, lerp(0.0, 9.0, sqrt(roughness))).xyz);
    light += bg;
  }

  else [flatten] if (material == MATERIAL_NOSHADE) {
    light = ((float3)(1.0));
  }

  RETURN(float4(light, 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  ReadDeferredVaryings(input);
  Shade();
  return fragment_color0;
}
