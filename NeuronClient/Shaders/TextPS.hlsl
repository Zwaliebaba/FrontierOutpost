// TextPS.hlsl -- one bit a pixel, and the bit chooses between a palette index and nothing.
//
// The target is the R8_UINT index buffer (ADR-001), so this shader returns a uint and not a
// color. A glyph pixel that is not set is discarded rather than written as index 0: the text is
// drawn over whatever is already on the screen, and a background-colored box around every letter
// is not what an 8x8 font looked like.
//
// The atlas is read with Load(), so there is no sampler here either. At present scale 2 each
// glyph texel becomes an exact 2x2 block of physical pixels, which is the whole point.

Texture2D<uint> g_fontAtlas : register(t0);

struct VertexOut
{
  float4 position : SV_Position;
  float2 glyphTexels : TEXCOORD0;
  nointerpolation uint paletteIndex : TEXCOORD1;
};

uint main(VertexOut _input) : SV_Target
{
  uint lit = g_fontAtlas.Load(int3(int2(_input.glyphTexels), 0));
  if (lit == 0)
  {
    discard;
  }

  return _input.paletteIndex;
}
