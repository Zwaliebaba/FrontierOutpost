// TextVS.hlsl -- 8x8 glyph quads, positioned in screen pixels.
//
// The vertex arrives in 1280x720 space rather than clip space so that the CPU side never does
// projection arithmetic: a string at (16, 16) is written as (16, 16). The quad spans whole pixels,
// which is what makes the glyph land on exact pixel boundaries in the back buffer.

cbuffer TextConstants : register(b0)
{
  float2 g_screenPixels;
};

struct VertexIn
{
  float2 positionPixels : POSITION;
  float2 glyphTexels : TEXCOORD0;
  // R8G8B8A8_UNORM, so the four bytes Pack() wrote arrive as 0-1 floats and are written back
  // unchanged: the back buffer is the same format and is not _SRGB (ADR-011).
  float4 color : COLOR0;
};

struct VertexOut
{
  float4 position : SV_Position;
  float2 glyphTexels : TEXCOORD0;
  // nointerpolation: every pixel of a glyph is the one color the string was drawn in. Letting the
  // interpolator run would produce a gradient across the quad that nothing asked for.
  nointerpolation float4 color : COLOR0;
};

VertexOut main(VertexIn _input)
{
  VertexOut output;

  float2 normalized = _input.positionPixels / g_screenPixels;
  output.position = float4(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0, 0.0, 1.0);
  output.glyphTexels = _input.glyphTexels;
  output.color = _input.color;

  return output;
}
