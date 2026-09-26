// FrontierOutpost/src/liblt/Shaders/ParticleRadialtexturedPS.hlsl
//
// GameData/shader/fragment/particle_radialtextured.jsl, in HLSL.

#include "Common.hlsli"

TEXTURE2D(texture_);
float3 baseColor;
float opacity;
float falloff;

static float opacityMult = 0.0;
static float3 attrib = 0.0;

void Shade() {
  float2 uvp = uv;
  uvp = float2( cos(attrib.x) * uvp.x + sin(attrib.x) * uvp.y,
             -sin(attrib.x) * uvp.x + cos(attrib.x) * uvp.y);
  float4 texMask = texture2D(texture_, 0.5 + 0.5 * uvp);
  float r = dot(uvp, uvp);
  float alpha = opacityMult * opacity * exp(-falloff*r);
  float4 final = texMask * float4(baseColor * alpha, alpha);
  RETURN(final);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  opacityMult = input.opacityMult;
  attrib = input.attrib;
  Shade();
  return fragment_color0;
}
