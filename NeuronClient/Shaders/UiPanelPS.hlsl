// FrontierOutpost/src/liblt/Shaders/UiPanelPS.hlsl
//
// GameData/shader/fragment/ui/panel.jsl, in HLSL.

#include "Common.hlsli"
#include "Ui.hlsli"

static float4 attrib1 = 0.0;
static float4 attrib2 = 0.0;
static float4 attrib3 = 0.0;

float shadowSize;

static const float kOpacity = 1.0;

float dbox(float2 p, float2 s, float b) {
  return length(max(((float2)(0.0)), abs(p) - (s - 2.0 * ((float2)(b))))) - b;
}

void Shade() {
  float2 uv = attrib1.xy;
  float2 size = attrib1.zw;
  float4 color = attrib2;
  float innerAlpha = attrib3.x;
  float bevel = attrib3.y;

  float2 uvp = uv;
  float x = size.x * (2.0 * uvp.x - 1.0);
  float y = size.y * (2.0 * uvp.y - 1.0);

  float dist = dbox(float2(x, y), size + bevel - 2.0 * shadowSize, bevel);
  float k = exp(-max(0.0, dist));
  float mult =
    innerAlpha * kOpacity * k +
    0.75 * saturate(exp(-pow(abs(0.2 * max(0.0, dist)), 0.75)) - k);

  /* Future / curved */
  color.xyz *= lerp(exp(-uvp.y), 0.5, 0.5);

  color.xyz += 0.17 * float3(0.5, 0.5, 0.5) * exp(-3.0 * length(uvp - float2(0.5, 0.0)));

  color.xyz = lerp(color.xyz, ((float3)(0.005)), 1.0 - exp(-2.0 * max(0.0, dist)));
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
