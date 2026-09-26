// FrontierOutpost/src/liblt/Shaders/UiGradientPS.hlsl
//
// GameData/shader/fragment/ui/gradient.jsl, in HLSL.

#include "Common.hlsli"
#include "Ui.hlsli"

static float4 attrib1 = 0.0;
static float4 attrib2 = 0.0;
static float4 attrib3 = 0.0;

void Shade() {
  float4 color1 = attrib1;
  float4 color2 = attrib2;
  float2 uv = attrib3.xy;
  float2 size = attrib3.zw;

  float2 uvp = (uv * frame - 0.5) / (frame - 1.0);
  RETURN(lerp(color1, color2, uv.y * uv.y));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  attrib1 = input.attrib1;
  attrib2 = input.attrib2;
  attrib3 = input.attrib3;
  Shade();
  return fragment_color0;
}
