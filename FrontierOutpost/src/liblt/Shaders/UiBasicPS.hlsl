// FrontierOutpost/src/liblt/Shaders/UiBasicPS.hlsl
//
// GameData/shader/fragment/ui/basic.jsl, in HLSL.

#include "Common.hlsli"
#include "Bezier.hlsli"
#include "Color.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

TEXTURE2D(layer);
float age;
float3 gradeBlue;
float linesMag;
float noiseMag;
float seed;
float2 size;

static const float kA = 2.00;
static const float kB = 1.25;

void Shade() {
  float offset =
    (0.002 * exp(-48.0 * snoise(uv.y + age))) *
    sign(2.0 * noise_(uv.y + 5.0 * age) - 1.0);

  float4 c = texture2D(layer, uv + float2(offset, 0.0));
  float x = size.x * (2.0 * uv.x - 1.0);
  float y = size.y * (2.0 * uv.y - 1.0);
  c.xyz *= 1.0 - noiseMag * log(1.0 - pow2(noise_(float3(128.0 * uv, seed))));
  c.xyz *= 1.0 + linesMag * exp(-2.0 * (0.5 + 0.5 * sin(2.0 * uv.y * size.y)));
  // c.xyz = 1.0 - exp(-kA * (c.xyz / avg(c.xyz)) * pow(avg(c.xyz), kB));
  c.xyz = 1.0 - exp(-kA * pow(abs(c.xyz), ((float3)(kB))));
  // c.x = bezier(c.x, 0.0, 0.10, 0.50, 0.75, 1.00);
  // c.y = bezier(c.y, 0.0, 0.25, 0.50, 0.75, 1.00);
  c.z = bezier(c.z, 0.0, gradeBlue.x, gradeBlue.y, gradeBlue.z, 1.0);
  // c.w *= 1.0 - exp(-pow2(64.0 * (1.0 - abs(2.0 * uv.x - 1.0))));
  // c.w *= 1.0 - exp(-pow2(64.0 * (1.0 - abs(2.0 * uv.y - 1.0))));

  c.xyz -= noise3(noise_(uv + 5.0)) / 128.0;
  c.w = saturate(c.w);
  c.xyz /= max(0.00001, c.w);
  c.xyz = max(((float3)(0.0)), c.xyz);
  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
