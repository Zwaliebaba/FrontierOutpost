// FrontierOutpost/src/liblt/Shaders/SkyboxVS.hlsl
//
// GameData/shader/vertex/skybox.jsl, in HLSL.

#include "Common.hlsli"

float4x4 WORLD;
float4x4 VIEW;
float4x4 PROJ;

float3 eye;

Varyings main(float3 vertex_position : ATTRIB0, float3 vertex_normal : ATTRIB1, float2 vertex_uv : ATTRIB2) {
  VS_PROLOGUE;
  float4 origin = float4(vp.xyz * farPlane, 0.0);
  float3 position = origin.xyz;
  gl_Position = mul(PROJ, mul(VIEW, origin));
  gl_Position.z = gl_Position.w * (1.0 - 1e-6);
  linearDepth = farPlane;
  vertpos = vertex_position.xyz;
  vertnormal = vertex_normal.xyz;

  Varyings varyings = WriteVaryings();
  varyings.position = position;
  return varyings;
}
