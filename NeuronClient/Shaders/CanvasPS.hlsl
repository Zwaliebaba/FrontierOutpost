// CanvasPS.hlsl -- the 1280x720 canvas, magnified onto the back buffer by a whole number.
//
// THIS IS THE ONE PASS BETWEEN THE GAME AND THE DISPLAY, AND IT RESAMPLES NOTHING (ADR-075). The
// canvas is read with Load(), which takes integer texel coordinates and has no filtering to switch
// on, and the pixel position is divided by an integer scale. So a canvas texel becomes an exact
// block of scale x scale screen pixels: at scale 2 a one-pixel rule is two pixels, not one and a
// half smeared over three. There is no sampler here, as there is nowhere else in this renderer
// (AGENTS.md R12).
//
// THE SCISSOR IS LOAD-BEARING, not an optimization. The caller sets it to the canvas rectangle
// before drawing, so _position never lands left of or above the offset and the subtraction below
// never goes negative. It matters because HLSL integer division truncates TOWARDS ZERO: -1 / 2 is
// 0, not -1, so a negative position would not read a texel outside the canvas -- it would read row
// zero again, and the letterbox would wear a smeared copy of the canvas's first row and column.
// The letterbox is cleared to black before this draw and the scissor is what keeps it that way.
//
// Alpha is written as 1 rather than carried through. The canvas's own alpha is inert -- the one
// blend state in this renderer writes the source's alpha straight through and reads the
// destination's never -- so whatever is in it is whatever the last shader to touch that pixel
// happened to return, which from ADR-075 stage 6 onwards includes zero on an uncovered glyph
// pixel. The back buffer is presented, so it is opaque.

Texture2D<float4> g_canvas : register(t0);

cbuffer CanvasConstants : register(b0)
{
  uint2 g_offsetPixels;
  uint g_scale;
};

float4 main(float4 _position : SV_Position) : SV_Target
{
  int2 texel = (int2(_position.xy) - int2(g_offsetPixels)) / int(g_scale);

  return float4(g_canvas.Load(int3(texel, 0)).rgb, 1.0);
}
