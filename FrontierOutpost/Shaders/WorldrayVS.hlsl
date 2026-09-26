// FrontierOutpost/src/liblt/Shaders/WorldrayVS.hlsl
//
// GameData/shader/vertex/worldray.jsl, in HLSL. The ray is found in GL's clip space, which INVPROJ
// inverts.

#include "Common.hlsli"

float4x4 INVVIEW;
float4x4 INVPROJ;

Varyings main(float3 vertex_position : ATTRIB0, float2 vertex_uv : ATTRIB2) {
  uv = vertex_uv;
  gl_Position = float4(vertex_position, 1.);
  float4 ray = mul(INVPROJ, float4(vertex_position.xy, 1., 1.));
  ray /= ray.w;
  float3 worldRayO = mul(INVVIEW, float4(0., 0., 0., 1.)).xyz;
  float3 worldRayD = mul(INVVIEW, ray).xyz - worldRayO;

  Varyings varyings = WriteVaryings();
  varyings.worldRayO = worldRayO;
  varyings.worldRayD = worldRayD;
  return varyings;
}
