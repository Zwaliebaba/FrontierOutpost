// FrontierOutpost/src/liblt/Shaders/Cube.hlsli
//
// GameData/shader/common/cube.jsl, in HLSL.

#ifndef CUBE_HLSLI
#define CUBE_HLSLI

float3 origin;
float3 du;
float3 dv;

float halfTexel;
float texelScale;

float3 GetCubePosition(float2 uv, bool texelCenter) {
  float u = texelCenter ? uv.x : (uv.x - halfTexel) * texelScale;
  float v = texelCenter ? uv.y : (uv.y - halfTexel) * texelScale;
  return normalize(origin + u * du + v * dv);
}

#endif
