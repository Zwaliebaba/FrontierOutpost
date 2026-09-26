// FrontierOutpost/src/liblt/Shaders/PostTonemapPS.hlsl
//
// GameData/shader/fragment/post/tonemap.jsl, in HLSL.

#include "Common.hlsli"
#include "Color.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

TEXTURE2D(texture_);

static const float kA = 1.50; // 1.50
static const float kB = 1.25; // 1.25
//const float kS = 0.00;

float3 Levels(float3 c, float3 lower, float3 upper) {
  return (c - lower) / (upper - lower);
}

float Vignette(float p) {
  return (1.0 - exp(-p * (1.0 - abs(2.0 * uv.x - 1.0)))) *
         (1.0 - exp(-p * (1.0 - abs(2.0 * uv.y - 1.0))));
}

void Shade() {
  float3 c = texture2D(texture_, uv).xyz;
  // c *= max(vec3(0.0), (1.0 + kS * (c / avg(c) - 1.0)));
  c = 1.0 - exp(-kA * pow(abs(c), ((float3)(kB))));
  // c = Levels(c, vec3(2.0 / 256.0), vec3(254.0 / 256.0));
  RETURN(float4(saturate(c), 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
