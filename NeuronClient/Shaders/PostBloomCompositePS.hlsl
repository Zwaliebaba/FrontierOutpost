// FrontierOutpost/src/liblt/Shaders/PostBloomCompositePS.hlsl
//
// GameData/shader/fragment/post/bloom_composite.jsl, in HLSL.

#include "Common.hlsli"
#include "Color.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

TEXTURE2D(texture1);
TEXTURE2D(texture2);
float2 rcpFrame;

static const float weightBright = 0.1;
static const float weightDark = 0.5;

void Shade() {
  float3 c1 = texture2D(texture1, uv).xyz;
  float3 c2 = texture2D(texture2, uv).xyz;
  c1 = max(((float3)(0)), c1);
  c2 = max(((float3)(0)), c2);

  float l1 = pow(abs(c1.x * c1.y * c1.z), 1.0 / 3.0);
  float l2 = (c2.x + c2.y + c2.z) / 3.0;

  float3 c = c1;
  float3 bright = sqrt(0.5 * (pow2(c) + pow2(c2)));
  c = lerp(c, bright, saturate(weightBright * l2));

  float3 dark = sqrt(c * c2);
  c = lerp(c, dark, saturate(weightDark * (1.0 - l1)));

  RETURN(float4(c, 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
