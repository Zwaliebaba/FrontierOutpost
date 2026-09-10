// StarfieldVS.hlsl -- the vertex half of the backdrop.
//
// A fullscreen triangle generated from SV_VertexID: no vertex buffer, no index buffer and no input
// layout. Three vertices, one draw call, and nothing to keep in sync with a C++ struct. A quad
// would need two triangles and would rasterize the diagonal twice.

void main(uint _vertexId : SV_VertexID, out float4 _position : SV_Position)
{
  // (0,0), (2,0), (0,2) in UV space. The triangle that spans is twice the size of the screen;
  // the half outside the clip volume costs nothing because it is clipped before rasterization.
  float2 uv = float2((_vertexId << 1) & 2, _vertexId & 2);
  _position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
