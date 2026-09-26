// FrontierOutpost/src/liblt/Shaders/ExplosionPS.hlsl
//
// GameData/shader/fragment/explosion.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"
#include "Softparticle.hlsli"

float3 color1;
float3 color2;
float opacity;
float age;

static const float animSpeed = .2f;

void Shade() {
  float dx = uv.x;
  float dy = uv.y;
  float realAge = animSpeed * age;
  float radius = sqrt(saturate(dx*dx + dy*dy));
  float r = radius;
  float alpha =
    10.0 * exp(-abs(128.0 * r)) +
     1.0 * exp(-abs(64.0 * r)) +
     0.2 * exp(-sqrt(16.0 * r));
  alpha *= exp(-5.0 * max(0.0, realAge));
  alpha *= 1.0 - exp(-15.0 * max(0.0, realAge));
  alpha *= exp(-max(0.0, realAge));
  alpha *= opacity;

  //float dDepth = GetDepthDifference();
  //const float thresh = .00005;
  //if (dDepth < thresh)
  //  glare *= dDepth / thresh;
  float4 c = float4(2.8, 1.0, 0.5, 1.0) * alpha;

  RETURN(c);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  ReadSoftparticleVaryings(input);
  Shade();
  return fragment_color0;
}
