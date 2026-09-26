// FrontierOutpost/src/liblt/Shaders/UiBoxPS.hlsl
//
// GameData/shader/fragment/ui/box.jsl, in HLSL.

#include "Common.hlsli"
#include "Ui.hlsli"

static float4 attrib1 = 0.0;
static float4 attrib2 = 0.0;
static float4 attrib3 = 0.0;

static const float kRadius = 2.0;

void Shade() {
  float4 color = attrib1;
  float2 uv = attrib2.xy;
  float2 size = attrib2.zw;
  float2 realSize = size - 48.0;

  float2 uvp = (uv * frame - 0.5) / (frame - 1.0);
  uvp = (2.0 * uvp - 1.0);

  float r = min(kRadius, min(realSize.x, realSize.y));
  float dist = length(max(((float2)(0.0)), size * abs(uvp) - (realSize - r))) - r;
  float mult = 0.0;
  mult += 1.0 * exp(-2.0 * max(0.0, dist));
  mult += 0.2 * exp(-pow(abs(0.25 * max(0.0, dist)), 0.75));
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
