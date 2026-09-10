// ShapePS.hlsl -- writes the colour the vertex carried, alpha included.
//
// Nothing here, and that is the point. A signed-distance field would give this interface smooth
// EDGES, and a smooth edge is coverage the rasterizer did not decide -- which on a screen built
// out of named colours (Color.h) puts a colour on a pixel that nobody chose. Alpha in this pass
// is a MATERIAL, not coverage: a card fill really is 4% white over its background (ADR-014).

struct VertexOut
{
  float4 position : SV_Position;
  float4 color : COLOR0;
};

float4 main(VertexOut _input) : SV_Target
{
  return _input.color;
}
