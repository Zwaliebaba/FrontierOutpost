// FrontierOutpost/src/liblt/Shaders/ShieldExplosionPS.hlsl
//
// GameData/shader/fragment/shield_explosion.jsl, in HLSL. Its colorMask, a varying that no vertex
// shader writes and this one never reads, is left out.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"
#include "Softparticle.hlsli"

float3 harmonics;
float3 color1;
float3 color2;
float opacity;
float age;
float ring;

static const float animSpeed = .2f;

void Shade() {
  float dx = uv.x; float dy = uv.y;
  float realAge = animSpeed * age;
  float radius = sqrt(saturate(dx*dx + dy*dy));
  float noisyRadius = radius - .6 *
      sin(uv.x * 9.21 * harmonics.x - realAge * 4.83) *
      cos(uv.x * 3.88 * harmonics.y - realAge * 1.3) *
      sin(uv.y * 13.32 * harmonics.z - realAge * 1.63) *
      cos(uv.y * 7.1313 * harmonics.x - realAge * 11.93);

  float alpha = .5 * (exp(-saturate(noisyRadius - .3) * 4.) + exp(-saturate(noisyRadius - .3) * 2.));
  alpha *= exp(-realAge * 5.);
  alpha += .5 * ring * exp(-abs(noisyRadius - realAge) * 5.);
  alpha *= saturate(1. - radius);
  alpha *= 1. - exp(-realAge * 15.);
  alpha *= exp(-realAge);
  alpha *= opacity;
  RETURN(float4(lerp(color1, color2, alpha), 1.0) * alpha);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  ReadSoftparticleVaryings(input);
  Shade();
  return fragment_color0;
}
