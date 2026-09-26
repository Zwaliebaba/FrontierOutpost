// FrontierOutpost/src/liblt/Shaders/WidgetTextureVS.hlsl
//
// GameData/shader/vertex/widgetTexture.jsl, in HLSL.

#include "Common.hlsli"

float4x4 PROJ;

Varyings main(float3 vertex_position : ATTRIB0, float2 vertex_uv : ATTRIB2) {
  uv = vertex_uv;
  gl_Position = mul(PROJ, float4(vertex_position, 1.0));
  return WriteVaryings();
}
