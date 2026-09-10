// StarfieldVS.hlsl -- the vertex half of the backdrop.
//
// The same fullscreen triangle as PaletteResolveVS, and duplicated rather than shared because
// Shaders/ holds .hlsl and nothing else (AGENTS.md 2): there is no header for four lines to live
// in. Three vertices out of SV_VertexID, no vertex buffer and no input layout.

void main(uint _vertexId : SV_VertexID, out float4 _position : SV_Position)
{
  float2 uv = float2((_vertexId << 1) & 2, _vertexId & 2);
  _position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
