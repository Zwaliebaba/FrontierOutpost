// ShapeVS.hlsl -- interface geometry, positioned in screen pixels.
//
// The vertex arrives in 1280x720 space rather than clip space, for the same reason TextVS does: a
// card at (964, 120) is written as (964, 120) and no arithmetic on the CPU side turns interface
// coordinates into anything else.

cbuffer ShapeConstants : register(b0)
{
  float2 g_screenPixels;
};

struct VertexIn
{
  float2 positionPixels : POSITION;
  // R8G8B8A8_UNORM, so the four bytes Pack() wrote arrive as 0-1 floats. Alpha is meaningful
  // here, unlike everywhere else in this renderer: the interface passes blend (ADR-014).
  float4 color : COLOR0;
};

struct VertexOut
{
  float4 position : SV_Position;
  // INTERPOLATED, unlike the mesh and text passes. Almost every shape is one flat colour, and for
  // those the interpolation is a no-op because all three vertices carry the same value. The two
  // that are not -- the map's background gradient and the glow at the horizon -- are the reason
  // this is not `nointerpolation`, and they are the only gradients on the screen.
  float4 color : COLOR0;
};

VertexOut main(VertexIn _input)
{
  VertexOut output;

  float2 normalized = _input.positionPixels / g_screenPixels;
  output.position = float4(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0, 0.0, 1.0);
  output.color = _input.color;

  return output;
}
