// FrontierOutpost/src/liblt/Shaders/UiLinePS.hlsl
//
// GameData/shader/fragment/ui/line.jsl, in HLSL.

#include "Common.hlsli"
#include "Ui.hlsli"

static float4 attrib1 = 0.0;
static float4 attrib2 = 0.0;
static float4 attrib3 = 0.0;
static float4 attrib4 = 0.0;

void Shade() {
  float4 color = attrib1;
  float2 uv = attrib2.xy;
  float2 p1 = attrib3.xy;
  float2 dir = attrib3.zw;
  float2 origin = attrib4.xy;
  float2 size = attrib4.zw;

  float2 uvp = (uv * frame - 0.5) / (frame - 1.0);
  float2 p = origin - p1 + uvp * size;
  float2 n = normalize(dir);
  float l = length(dir);
  if (l <= 1e-6)
    RETURN(((float4)(0.0)));

  float projLength = clamp(dot(n, p), 0.0, l);
  // projLength = floor(projLength / 4.0 + 0.5) * 4.0;
  float2 proj = n * projLength;
  float d = length(p - proj);
  float t = saturate(1.0 - projLength / l);
  float m =
    0.7 * exp(-2.0 * max(0.0, d - 0.5)) +
    0.2 * exp(-sqrt(0.7 * max(0.0, d - 0.5)));
  RETURN(color * m);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  attrib1 = input.attrib1;
  attrib2 = input.attrib2;
  attrib3 = input.attrib3;
  attrib4 = input.attrib4;
  Shade();
  return fragment_color0;
}
