// FrontierOutpost/src/liblt/Shaders/ShieldPS.hlsl
//
// GameData/shader/fragment/shield.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"

static const int kMaxHits = 16;

static float3 position = 0.0;
static float3 normal = 0.0;

float3 hitPos[kMaxHits];
float hitAge[kMaxHits];
float time;
int activeHits;

void Shade() {
  float alpha = 0.;
  for (int i = 0; i < activeHits; ++i) {
    float dist = length(hitPos[i] - position);
    alpha +=
      exp(-hitAge[i] * 10) *
      exp(-dist * .25) *
      lerp(0.2, 1.0, pow(abs(cos(2*dist - 10*hitAge[i])), 1. + dist) / (.1 + dist));
  }
  RETURN(float4(0.3, 0.6, 1.8, 1.0) * alpha);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  position = input.position;
  normal = input.normal;
  Shade();
  return fragment_color0;
}
