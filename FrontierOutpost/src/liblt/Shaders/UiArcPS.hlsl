// FrontierOutpost/src/liblt/Shaders/UiArcPS.hlsl
//
// GameData/shader/fragment/ui/arc.jsl, in HLSL.

#include "Common.hlsli"
#include "Ui.hlsli"

static float4 attrib1 = 0.0;
static float4 attrib2 = 0.0;
static float4 attrib3 = 0.0;

static const float kExpansion = 16.0;
static const float kRound = 0.5;

void Shade() {
  float4 color = attrib1;
  float2 uv = attrib2.xy;
  float radius = attrib2.z;
  float phase = attrib2.w;
  float rs = attrib3.x;
  float as = attrib3.y;

  float2 uvp = (uv * frame - 0.5) / (frame - 1.0);
  uvp = (2.0 * uvp - 1.0);

  float a = atan2(uvp.y, uvp.x) / TAU + 0.5;
  float r = ((radius + kExpansion) / radius) * length(uvp);
  float2 d = float2(radius * abs(r - 0.25), TAU * (0.5 - abs(abs(a - phase) - 0.5)));
  float dist = length(float2(1.0, r * radius) * max(d - float2(rs - kRound, as * TAU), 0.0)) - kRound;
  float mult = 0;
  mult += lerp(exp(-max(0.0, dist)), exp(-1.5 * max(0.0, abs(dist) - 0.5)), 0.75);
  mult += 0.2 * exp(-pow(abs(0.2 * max(0.0, dist)), 0.75));
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
