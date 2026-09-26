// FrontierOutpost/src/liblt/Shaders/Smaa1VS.hlsl
//
// GameData/shader/vertex/smaa_1.jsl, in HLSL: SMAA's edge detection.

#include "Common.hlsli"

#define SMAA_ONLY_COMPILE_VS
#include "Smaa.hlsli"

Varyings main(float3 vertex_position : ATTRIB0, float2 vertex_uv : ATTRIB2) {
  uv = vertex_uv;
  gl_Position = float4(vertex_position, 1.0);

  float4 offset[3];
  SMAAEdgeDetectionVS(uv, offset);

  Varyings varyings = WriteVaryings();
  varyings.offset[0] = offset[0];
  varyings.offset[1] = offset[1];
  varyings.offset[2] = offset[2];
  return varyings;
}
