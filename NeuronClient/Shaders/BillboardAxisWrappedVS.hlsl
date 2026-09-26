// FrontierOutpost/src/liblt/Shaders/BillboardAxisWrappedVS.hlsl
//
// GameData/shader/vertex/billboard_axis_wrapped.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"

float4x4 WORLD;
float4x4 VIEW;
float4x4 PROJ;

float3 eye;
float3 axis;
float2 size;

static const float kWrapDistance = 1024.0;

Varyings main(float3 vertex_position : ATTRIB0, float3 vertex_normal : ATTRIB1, float2 vertex_uv : ATTRIB2) {
  VS_PROLOGUE;

  float3 toCam = eye - worldPos.xyz;
  toCam = 2.0 * mod(toCam, (float3)kWrapDistance) - (float3)kWrapDistance;
  worldPos.xyz = eye - toCam;

  float3 up = normalize(axis);
  float3 right = cross(normalize(toCam), up);

  float opacity = 1.0;
  float3 attrib = vertex_normal;

  worldPos.xyz += size.x * u * right;
  worldPos.xyz += size.y * v * up;
  gl_Position = mul(PROJ, mul(VIEW, worldPos));
  linearDepth = gl_Position.z;
  gl_Position.z = LogDepth(gl_Position.z, gl_Position.w);

  float3 position = worldPos.xyz;

  Varyings varyings = WriteVaryings();
  varyings.opacity = opacity;
  varyings.attrib = attrib;
  varyings.position = position;
  return varyings;
}
