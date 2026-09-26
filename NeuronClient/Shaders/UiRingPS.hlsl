// FrontierOutpost/src/liblt/Shaders/UiRingPS.hlsl
//
// GameData/shader/fragment/ui/ring.jsl, in HLSL.

#include "Common.hlsli"
#include "Ui.hlsli"

static float4 attrib1 = 0.0;
static float4 attrib2 = 0.0;

static const float kThickness = 1.0;

void Shade() {
  float4 color = attrib1;
  float2 uv = attrib2.xy;
  float radius = attrib2.z;
  float phase = attrib2.w;

  float2 uvp = (uv * frame - 0.5) / (frame - 1.0);
  uvp = (2.0 * uvp - 1.0);
  float angle = atan2(uvp.y, uvp.x) + phase;
  uvp = length(uvp) * float2(cos(angle), sin(angle));

  float dist = length(abs(uvp));
  float mult = 1.00 * exp(-max(0.0, abs(2.0 * radius * (dist - 0.25)) - kThickness));
  mult *= 1.00 - exp(-32.0 * max(0.0, abs(uvp.x) - 0.05));
  mult *= 1.00 - exp(-32.0 * max(0.0, abs(uvp.y) - 0.05));
  mult += 0.07 * exp(-8.00 * abs(dist - 0.25));
  RETURN(color * mult);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  attrib1 = input.attrib1;
  attrib2 = input.attrib2;
  Shade();
  return fragment_color0;
}
