// FrontierOutpost/src/liblt/Shaders/Smaa3VS.hlsl
//
// GameData/shader/vertex/smaa_3.jsl, in HLSL: SMAA's neighborhood blending. It uses two of the
// three offsets the varyings carry.

#include "Common.hlsli"

#define SMAA_ONLY_COMPILE_VS
#include "Smaa.hlsli"

Varyings main(float3 vertex_position : ATTRIB0, float2 vertex_uv : ATTRIB2) {
  uv = vertex_uv;
  gl_Position = float4(vertex_position, 1.0);

  float4 offset[2];
  SMAANeighborhoodBlendingVS(uv, offset);

  Varyings varyings = WriteVaryings();
  varyings.offset[0] = offset[0];
  varyings.offset[1] = offset[1];
  return varyings;
}
