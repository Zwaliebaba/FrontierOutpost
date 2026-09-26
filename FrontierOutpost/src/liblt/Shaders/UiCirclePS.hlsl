// FrontierOutpost/src/liblt/Shaders/UiCirclePS.hlsl
//
// GameData/shader/fragment/ui/circle.jsl, in HLSL.

#include "Common.hlsli"
#include "Ui.hlsli"

static float4 attrib1 = 0.0;
static float4 attrib2 = 0.0;
static float4 attrib3 = 0.0;

void Shade() {
  float4 color = attrib1;
  float2 uv = attrib2.xy;

  float2 uvp = (uv * frame - 0.5) / (frame - 1.0);
  uvp = (2.0 * uvp - 1.0);

  float dist = max(0.0, length(uvp) - 0.01);
  float mult =   2.00 * exp(-4.0 * pow(abs(48.0 * max(0.0, dist - 0.001)), 0.75))
               + 0.20 * exp(-12.0 * max(0.0, dist - 0.001));
  RETURN(color * mult);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  attrib1 = input.attrib1;
  attrib2 = input.attrib2;
  attrib3 = input.attrib3;
  Shade();
  return fragment_color0;
}
