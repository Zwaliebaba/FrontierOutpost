// MeshPS.hlsl -- writes the index the vertex shader already decided.
//
// There is nothing to do here, and that is the shape ADR-001 and ADR-002 together produce: the
// color is one of sixteen names rather than a quantity, and which name it is was settled per
// face. A pixel shader that did any arithmetic on a palette index would be computing a color
// this screen cannot show.

struct VertexOut
{
  float4 position : SV_Position;
  nointerpolation uint paletteIndex : TEXCOORD0;
};

uint main(VertexOut _input) : SV_Target
{
  return _input.paletteIndex;
}
