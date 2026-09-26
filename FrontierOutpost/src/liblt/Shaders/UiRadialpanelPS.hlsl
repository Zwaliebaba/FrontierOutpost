// FrontierOutpost/src/liblt/Shaders/UiRadialpanelPS.hlsl
//
// GameData/shader/fragment/ui/radialpanel.jsl, in HLSL.

#include "Common.hlsli"
#include "Ui.hlsli"

static float4 attrib1 = 0.0;
static float4 attrib2 = 0.0;
static float4 attrib3 = 0.0;

float shadowSize;

static const float kOpacity = 1.0;

void Shade() {
  float2 uv = attrib1.xy;
  float r1 = attrib1.z;
  float r2 = attrib1.w;
  float4 color = attrib2;
  float innerAlpha = attrib3.x;
  float bevel = attrib3.y;
  float phase = attrib3.z;
  float angle = attrib3.w;

  float2 uvp = (uv * frame - 0.5) / (frame - 1.0);
  float x = (r2 + 2.0 * shadowSize) * (2.0 * uvp.x - 1.0);
  float y = (r2 + 2.0 * shadowSize) * (2.0 * uvp.y - 1.0);

  float r = length(float2(x, y));
  float a = atan2(y, x) / TAU;

  float dr = max(0.0, abs(r - 0.5 * (r1 + r2)) - 0.5 * (r2 - r1) + bevel);
  float da = r * TAU * (min(abs(a - phase), abs(1.0 + a - phase)) - angle / 2.0);
  da = max(0.0, da + bevel);
  float dist = length(max(((float2)(0.0)), float2(dr, da))) - bevel;
  float k = exp(-1.5 * max(0.0, dist));
  float mult =
    innerAlpha * kOpacity * k +
    0.5 * exp(-pow(abs(0.25 * max(0.0, dist)), 0.75));

  color.xyz = lerp(((float3)(0.0)), color.xyz, exp(-1.0 * max(0.0, dist)));

  /* Flat / metro */
  color.xyz *= 1.0;

  color.xyz += 0.25 * float3(0.4, 0.7, 1.0) * exp(-4.0 * length(uvp - float2(0.5, 0.0)));

  mult = saturate(mult);
  RETURN(float4(color.xyz, color.w * mult));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  attrib1 = input.attrib1;
  attrib2 = input.attrib2;
  attrib3 = input.attrib3;
  Shade();
  return fragment_color0;
}
