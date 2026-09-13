// CanvasVS.hlsl -- one triangle covering the whole viewport, built from SV_VertexID.
//
// There is no vertex buffer and no input layout: three vertices whose positions are arithmetic on
// their own index. The triangle is deliberately BIGGER than the viewport -- it reaches (3, 1) and
// (-1, -3) in clip space -- because one oversized triangle rasterizes the same pixels as two
// triangles forming a quad, without the diagonal seam where the two meet and without the quad's
// shared edge being rasterized twice.
//
// This is the shape ADR-001's palette resolve pass had, minus the palette. ADR-011 deleted it when
// the present scale went away; ADR-075 brings it back because the scale did.

float4 main(uint _vertexId : SV_VertexID) : SV_Position
{
  // 0 -> (0, 0), 1 -> (2, 0), 2 -> (0, 2), in a space where the viewport is the unit square.
  float2 corner = float2(float((_vertexId << 1) & 2), float(_vertexId & 2));

  // To clip space, with Y flipped: the canvas's first row is at the TOP of the screen.
  return float4(corner.x * 2.0 - 1.0, 1.0 - corner.y * 2.0, 0.0, 1.0);
}
