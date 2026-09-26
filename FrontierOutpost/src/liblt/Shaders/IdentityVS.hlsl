// FrontierOutpost/src/liblt/Shaders/IdentityVS.hlsl
//
// GameData/shader/vertex/identity.jsl, in HLSL: positions already in clip space, as the
// full-screen quad gives them.

#include "Common.hlsli"

Varyings main(float3 vertex_position : ATTRIB0, float3 vertex_normal : ATTRIB1, float2 vertex_uv : ATTRIB2,
              float3 vertex_color : ATTRIB3) {
  uv = vertex_uv;
  vertpos = vertex_position;
  vertnormal = vertex_normal;
  vertcolor = vertex_color;
  gl_Position = float4(vertex_position, 1.0);
  return WriteVaryings();
}
