// FrontierOutpost/src/liblt/Shaders/PostColorgrade1DPS.hlsl
//
// GameData/shader/fragment/post/colorgrade1D.jsl, in HLSL.

#include "Common.hlsli"
#include "Color.hlsli"
#include "Math.hlsli"
#include "Texturing.hlsli"

TEXTURE2D(texture_);
TEXTURE2D(rCurve);
TEXTURE2D(gCurve);
TEXTURE2D(bCurve);
TEXTURE2D(rCurve2);
TEXTURE2D(gCurve2);
TEXTURE2D(bCurve2);
float2 rDir;
float2 gDir;
float2 bDir;
float colorPoints;

static const float kVariation = 0.5;

void Shade() {
  float3 original = saturate(texture2D(texture_, uv).xyz);
  original = (original * (colorPoints - 1.0) + 0.5) / colorPoints;
  float3 c = original;

  c.x = lerp(
    texLod(TEXTURE_ARG(rCurve), float2(c.x, 0.5), 0).x,
    texLod(TEXTURE_ARG(rCurve2), float2(c.x, 0.5), 0).x,
    kVariation * dot(uv, rDir));

  c.y = lerp(
    texLod(TEXTURE_ARG(gCurve), float2(c.y, 0.5), 0).x,
    texLod(TEXTURE_ARG(gCurve2), float2(c.y, 0.5), 0).x,
    kVariation * dot(uv, gDir));

  c.z = lerp(
    texLod(TEXTURE_ARG(bCurve), float2(c.z, 0.5), 0).x,
    texLod(TEXTURE_ARG(bCurve2), float2(c.z, 0.5), 0).x,
    kVariation * dot(uv, bDir));

  c *= avg(original) / max(0.00001, avg(c));
  // c = mix(c, original, 0.999);

  RETURN(float4(c, 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
