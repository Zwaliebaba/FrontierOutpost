// FrontierOutpost/src/liblt/Shaders/TrailVS.hlsl
//
// GameData/shader/vertex/trail.jsl, in HLSL. It writes no linear depth, as the .jsl wrote none.

#include "Common.hlsli"
#include "Math.hlsli"

float4x4 WORLD;
float4x4 VIEW;
float4x4 PROJ;

float3 eye;
float size;

Varyings main(float3 vertex_position : ATTRIB0, float3 vertex_normal : ATTRIB1, float2 vertex_uv : ATTRIB2) {
  VS_PROLOGUE;
  float3 position = vertex_position;
  float3 toCam = eye - position;
  float camDistance = length(toCam);
  toCam /= camDistance;
  float3 right = normalize(cross(toCam, vertex_normal));

  uv.x = sign(u);
  float3 offset = size * uv.x * right;
  gl_Position = mul(PROJ, mul(VIEW, float4(position + offset, 1.0)));
  gl_Position.z = LogDepth(gl_Position.z, gl_Position.w);
  float alpha = abs(u);

  Varyings varyings = WriteVaryings();
  varyings.alpha = alpha;
  varyings.position = position;
  return varyings;
}
