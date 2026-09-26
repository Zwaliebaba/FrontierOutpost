// FrontierOutpost/src/liblt/Shaders/ParticleRadialPS.hlsl
//
// GameData/shader/fragment/particle_radial.jsl, in HLSL.

#include "Common.hlsli"
#include "Color.hlsli"

TEXTURE2D(texture_);
float3 baseColor;
float opacity;
float falloff;

static float opacityMult = 0.0;
static float3 attrib = 0.0;

void Shade() {
  float r = falloff * length(uv);
  float alpha = opacityMult * opacity * (
    1.0 * exp(-12.0 * r) +
    0.3 * exp(-sqrt(6.0 * r)));
  float3 color = baseColor * attrib;
  RETURN(float4(color * alpha, alpha));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  opacityMult = input.opacityMult;
  attrib = input.attrib;
  Shade();
  return fragment_color0;
}
