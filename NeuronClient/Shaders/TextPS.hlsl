// TextPS.hlsl -- one byte of coverage a pixel, and the coverage decides how much of the string's
// color lands.
//
// A glyph pixel that is wholly uncovered is discarded rather than written as the background color:
// the text is drawn over whatever is already on the screen, and a background-colored box around
// every letter is not what text looks like.
//
// COVERAGE IS ALPHA HERE, AND NOWHERE ELSE (ADR-074). ADR-014 settled that alpha in the interface
// passes is a material and not coverage -- a card fill really is 4% white -- and this pass is the
// one exception, because the faces ADR-074 chose are vector faces at sizes where one bit a pixel
// is not a smaller version of the letter but noise. The shape pass is untouched, and so is every
// scene pass. Blending was already enabled for this pipeline (InterfaceBlendState), so nothing is
// switched on here: what changed is that the value multiplying the string's alpha is no longer
// always one.
//
// The atlas is read with Load(), so there is still no sampler. The quad is `scale` times the size
// of the glyph, so the truncation below is what turns one atlas texel into an exact block of
// screen pixels -- there is no filtering a later edit could switch on.

Texture2D<float> g_fontAtlas : register(t0);

struct VertexOut
{
  float4 position : SV_Position;
  float2 glyphTexels : TEXCOORD0;
  nointerpolation float4 color : COLOR0;
};

float4 main(VertexOut _input) : SV_Target
{
  float coverage = g_fontAtlas.Load(int3(int2(_input.glyphTexels), 0));
  if (coverage == 0.0)
  {
    discard;
  }

  return float4(_input.color.rgb, _input.color.a * coverage);
}
