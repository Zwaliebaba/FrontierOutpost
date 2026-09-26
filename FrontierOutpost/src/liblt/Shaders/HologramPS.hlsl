// FrontierOutpost/src/liblt/Shaders/HologramPS.hlsl
//
// GameData/shader/fragment/hologram.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"
#include "Texturing.hlsli"

static float3 normal = 0.0;
static float3 position = 0.0;
static float3 scale = 0.0;

float seed;
float3 baseColor;
float baseAlpha;
float time;

void Shade() {
  float3 c = baseColor;
  float3 n = normalize(normal);
  float dn = length(normal - normalize(normal));
  float spec = abs(dot(normalize(position), n));
  float alpha = 0.25;
  alpha += 1.000 * exp(-1024.0 * dn);
  alpha += 64.00 * dn;
  alpha += 2.000 * exp(-4.0 * abs(spec - 0.25));
  alpha += 2.000 * exp(-4.0 * abs(spec - 0.50));
  alpha += 2.000 * exp(-4.0 * abs(spec - 0.00));
  alpha *= 0.075;
  alpha *= 1.0 - 0.15 * log(1.0 - noise_(19.0 * gl_FragCoord.xy + seed));
  alpha *= 0.5 + 0.5 * sin(alpha);
  alpha *= baseAlpha;
  RETURN(float4(c, 1.0) * alpha);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  normal = input.normal;
  position = input.position;
  scale = input.scale;
  Shade();
  return fragment_color0;
}
