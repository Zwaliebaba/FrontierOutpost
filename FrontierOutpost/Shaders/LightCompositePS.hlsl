// FrontierOutpost/src/liblt/Shaders/LightCompositePS.hlsl
//
// GameData/shader/fragment/light/composite.jsl, in HLSL.

#include "Common.hlsli"
#include "Deferred.hlsli"
#include "Lighting.hlsli"
#include "Noise.hlsli"

TEXTURE2D(albedoBuffer);
TEXTURE2D(lightBuffer);
TEXTURE2D(noiseBuffer);
float2 lightPosSS;

void Shade() {
  float4 albedo = texture2D(albedoBuffer, uv);
  /* A point light beside a surface (a thruster's, at its nozzle) can overflow the RGBA16F light
     buffer to Inf. The lerps below turn Inf into NaN, and bloom spreads one NaN into a black
     square. min() returns the other operand for a NaN, so this also clears a NaN. */
  static const float kLightMax = 65504.0;
  float3 light = min(texture2D(lightBuffer, uv).xyz, kLightMax);
  float3 fg = albedo.xyz * light;
  float depth = GetDepthNormalized() * length(worldRayD);
  float fog = getFoginess(depth);

  float3 ro = worldRayO;
  float3 rd = normalize(worldRayD);
  float occlusion = 0.0;

  float3 fogColor = getFog(ro, rd, depth, occlusion);
  // fg = mix(fg, vec3(fg), fog);
  fg = lerp(fg, fogColor, fog);

  float3 bg = lerp(
    toLinear(textureCube(envMap, rd).xyz),
    getFog(ro, rd, farPlane, occlusion),
    getFoginess(farPlane));

  RETURN(float4(toGamma(lerp(bg, fg, albedo.w)), 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  ReadDeferredVaryings(input);
  Shade();
  return fragment_color0;
}
