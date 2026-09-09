// TextVS.hlsl -- 8x8 glyph quads, positioned in virtual screen texels.
//
// The vertex arrives in 640x400 space rather than clip space so that the CPU side never does
// projection arithmetic: a string at (8, 8) is written as (8, 8). The quad spans whole texels,
// which is what makes the glyph land on exact pixel boundaries in the index target.

cbuffer TextConstants : register(b0)
{
  float2 g_virtualScreenTexels;
};

struct VertexIn
{
  float2 positionTexels : POSITION;
  float2 glyphTexels : TEXCOORD0;
  uint paletteIndex : TEXCOORD1;
};

struct VertexOut
{
  float4 position : SV_Position;
  float2 glyphTexels : TEXCOORD0;
  // nointerpolation: a palette index is a name, not a quantity. Interpolating between index 1 and
  // index 9 would produce index 5, which is a different color rather than a shade between them.
  nointerpolation uint paletteIndex : TEXCOORD1;
};

VertexOut main(VertexIn _input)
{
  VertexOut output;

  float2 normalized = _input.positionTexels / g_virtualScreenTexels;
  output.position = float4(normalized.x * 2.0 - 1.0, 1.0 - normalized.y * 2.0, 0.0, 1.0);
  output.glyphTexels = _input.glyphTexels;
  output.paletteIndex = _input.paletteIndex;

  return output;
}
