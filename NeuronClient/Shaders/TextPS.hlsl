// TextPS.hlsl -- one bit a pixel, and the bit decides between the string's color and nothing.
//
// A glyph pixel that is not set is discarded rather than written as the background color: the text
// is drawn over whatever is already on the screen, and a background-colored box around every
// letter is not what an 8x8 font looked like. Discard rather than blend, because nothing in this
// renderer blends (ADR-011).
//
// The atlas is read with Load(), so there is no sampler here. The quad is GLYPH_SCALE times the
// size of the glyph, so the truncation below is what turns one atlas texel into an exact square
// block of screen pixels -- there is no filtering a later edit could switch on.

Texture2D<uint> g_fontAtlas : register(t0);

struct VertexOut
{
  float4 position : SV_Position;
  float2 glyphTexels : TEXCOORD0;
  nointerpolation float4 color : COLOR0;
};

float4 main(VertexOut _input) : SV_Target
{
  uint lit = g_fontAtlas.Load(int3(int2(_input.glyphTexels), 0));
  if (lit == 0)
  {
    discard;
  }

  return _input.color;
}
