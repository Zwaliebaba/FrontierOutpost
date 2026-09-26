// FrontierOutpost/src/liblt/Shaders/BillboardSoftVS.hlsl
//
// GameData/shader/vertex/billboard_soft.jsl, in HLSL. ndcPos is taken, as it was, from GL's clip
// space.

#include "Common.hlsli"
#include "Math.hlsli"

float4x4 WORLD;
float4x4 VIEW;
float4x4 PROJ;

float3 eye;
float billboardSize;
float zOffset;

Varyings main(float3 vertex_position : ATTRIB0, float3 vertex_normal : ATTRIB1, float2 vertex_uv : ATTRIB2) {
  VS_PROLOGUE;

  float3 toCam = normalize(eye - worldPos.xyz);
  float3 up = normalize(ortho(toCam));
  float3 right = cross(toCam, up);

  float opacityMult = 1.;
  float3 attrib = vertex_normal;

  worldPos.xyz += billboardSize * u * right;
  worldPos.xyz += billboardSize * v * up;
  worldPos.xyz += (eye - worldPos.xyz) / zOffset;

  gl_Position = mul(PROJ, mul(VIEW, worldPos));
  linearDepth = gl_Position.z;
  gl_Position.z = LogDepth(gl_Position.z, gl_Position.w);

  float4 ndcPos = gl_Position;
  float3 position = worldPos.xyz;

  Varyings varyings = WriteVaryings();
  varyings.opacityMult = opacityMult;
  varyings.attrib = attrib;
  varyings.position = position;
  varyings.ndcPos = ndcPos;
  return varyings;
}
