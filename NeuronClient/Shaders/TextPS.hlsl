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

// WHY COVERAGE IS NOT ALPHA DIRECTLY, EVEN THOUGH THAT IS WHAT IT MEANS.
//
// The back buffer is R8G8B8A8_UNORM and deliberately not _SRGB (ADR-011), so a channel authored as
// 0xAA is presented as 0xAA. That is the right decision for every colour on this screen and it has
// one consequence here: the blender does `dst * (1 - a) + src * a` on values that are sRGB-ENCODED
// while treating them as if they were linear. Light on dark, that arithmetic lands too dark. A
// pixel the rasterizer says is half covered should end up at about 73% of the string's brightness
// once encoded, and blending in the wrong space puts it at 50% -- so stems thin out, and the
// thinner the stem the more of it is half-covered pixels. It reads as a weight that was never
// baked.
//
// Raising coverage to 1/2.2 is the correction, and it is not a taste value: for a black background
// it is exactly `lin_to_srgb(coverage)`, which is the number blending in linear space would have
// produced. The background here is near-black rather than black, so this slightly overshoots on
// anything drawn over a lit card -- which is the direction to err, because the alternative is text
// that looks a weight lighter than the cut it was baked from.
static const float COVERAGE_GAMMA = 1.0 / 2.2;

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

  return float4(_input.color.rgb, _input.color.a * pow(coverage, COVERAGE_GAMMA));
}
