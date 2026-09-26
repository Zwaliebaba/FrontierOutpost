// FrontierOutpost/src/liblt/Shaders/GenNormalmapLumPS.hlsl
//
// GameData/shader/fragment/gen/normalmap_lum.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"

TEXTURE2D(texture_);
float invWidth;
float invHeight;
float height;

float cellW;
float cellH;

float getHeight(float x, float y) {
  return avg(texture2D(texture_, float2(x, y)).xyz);
}

void Shade() {
  float2 uvp = uv;
  float dx = getHeight(uvp.x + invWidth, uvp.y) - getHeight(uvp.x - invWidth, uvp.y);
  float dy = getHeight(uvp.x, uvp.y + invHeight) - getHeight(uvp.x, uvp.y - invHeight);
  dx *= height;
  dy *= height;
  float3 normal = cross(
    float3(cellW * invWidth, 0.0, height * dx),
    float3(0.0, cellH * invHeight, height * dy));
  normal = normalize(normal);
  RETURN(float4(0.5 * normal + 0.5, 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
