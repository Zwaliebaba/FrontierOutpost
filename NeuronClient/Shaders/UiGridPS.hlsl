// FrontierOutpost/src/liblt/Shaders/UiGridPS.hlsl
//
// GameData/shader/fragment/ui/grid.jsl, in HLSL.

#include "Common.hlsli"
#include "Ui.hlsli"
#include "Noise.hlsli"

static float4 attrib1 = 0.0;
static float4 attrib2 = 0.0;
static float4 attrib3 = 0.0;

float dfield(float d) {
  return exp(-2.0 * d * d) + 0.2 * exp(-pow(abs(0.2 * d), 0.75));
}

void Shade() {
  float4 color = attrib1;
  float2 uv = attrib2.xy;
  float2 scale = attrib2.zw;
  float2 offset = attrib3.xy;
  float2 scale2 = attrib3.zw;

  float2 uvp = (uv * frame - 0.5) / (frame - 1.0);
  float2 p = (2.0 * uvp - 1.0) * scale * scale2 + offset;
  uvp = abs(2.0 * uvp - 1.0);
  float2 d1 = 16.0 * abs(2.0 * frac(p / 16.0 - 0.5) - 1.0);
  float2 d2 = 64.0 * abs(2.0 * frac(p / 64.0 - 0.5) - 1.0);
  float mult = 1.5;
  mult += 6.0 * exp(-16.0 * 32.0 * max(0.0, length(abs(2.0 * frac(p / 16.0 - 0.5) - 1.0)) - 0.75 / 16.0));
  mult += 6.0 * exp(-16.0 * 128.0 * max(0.0, length(abs(2.0 * frac(p / 64.0 - 0.5) - 1.0)) - 0.75 / 64.0));
  float d = 1.0;
  mult +=
      0.5 * max(
        dfield(max(0.0, d1.x - 1.0 / 16.0)),
        dfield(max(0.0, d1.y - 1.0 / 16.0)));
  mult +=
      1.5 * max(
        dfield(max(0.0, d2.x - 1.0 / 64.0)),
        dfield(max(0.0, d2.y - 1.0 / 64.0)));
  mult += d;
  mult *= 0.25;

  mult *= 2.0 * exp(-2.0 * uv.y);
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
