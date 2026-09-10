// MeshPS.hlsl -- writes the color the vertex shader already chose.
//
// There is nothing to do here, and that is the shape ADR-011 and ADR-012 together produce: the
// framebuffer holds a color, but WHICH of a face's two colors it is was settled per face. A pixel
// shader that did any arithmetic on it would be computing a third tone this game has decided it
// does not have.

struct VertexOut
{
  float4 position : SV_Position;
  nointerpolation float4 color : COLOR0;
};

float4 main(VertexOut _input) : SV_Target
{
  return _input.color;
}
