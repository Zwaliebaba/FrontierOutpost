// FrontierOutpost/src/liblt/Shaders/Smaa2VS.hlsl
//
// GameData/shader/vertex/smaa_2.jsl, in HLSL: SMAA's blending weight calculation.

#include "Common.hlsli"

#define SMAA_ONLY_COMPILE_VS
#include "Smaa.hlsli"

Varyings main(float3 vertex_position : ATTRIB0, float2 vertex_uv : ATTRIB2) {
  uv = vertex_uv;
  gl_Position = float4(vertex_position, 1.0);

  float2 pixcoord;
  float4 offset[3];
  SMAABlendingWeightCalculationVS(uv, pixcoord, offset);

  Varyings varyings = WriteVaryings();
  varyings.pixcoord = pixcoord;
  varyings.offset[0] = offset[0];
  varyings.offset[1] = offset[1];
  varyings.offset[2] = offset[2];
  return varyings;
}
