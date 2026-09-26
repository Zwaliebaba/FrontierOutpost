// FrontierOutpost/src/liblt/Shaders/WidgetVS.hlsl
//
// GameData/shader/vertex/widget.jsl, in HLSL. vert_attrib1 to vert_attrib4 follow the position in
// the widget's vertex, so they are attributes 1 to 4.

#include "Common.hlsli"

float4x4 PROJ;

Varyings main(float3 vertex_position : ATTRIB0, float4 vert_attrib1 : ATTRIB1, float4 vert_attrib2 : ATTRIB2,
              float4 vert_attrib3 : ATTRIB3, float4 vert_attrib4 : ATTRIB4) {
  gl_Position = mul(PROJ, float4(vertex_position, 1.0));

  Varyings varyings = WriteVaryings();
  varyings.attrib1 = vert_attrib1;
  varyings.attrib2 = vert_attrib2;
  varyings.attrib3 = vert_attrib3;
  varyings.attrib4 = vert_attrib4;
  varyings.position = vertex_position;
  return varyings;
}
