// FrontierOutpost/src/liblt/Shaders/GenRockPS.hlsl
//
// GameData/shader/fragment/gen/rock.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

float seed;
float3 colorPower;

static const float4 coef = float4(.6, .5, .9, .9);

float genHeight(float3 p) {
  float4 z = float4(p / 4. + .75, .3);
  float a = 0., l = 0., w = 1.;
  for (int i = 0; i < 32; ++i) {
    float m = dot(z, z);
    z = abs(z) / m - float4(.4, .5, .6, .3);
    z += .1 * log(1.e-10 + noise4(float(i) + seed));
    z *= 1. + .25 * noise_(float(i) + seed * 2. + 32.);
    z = z.yzwx;
    m = coef.x*z.x*z.x + coef.y*z.y*z.y + coef.z*z.z*z.z + coef.w*z.w*z.w;
    if (i > 0) {
      a += w * exp(-abs(m - l));
      w *= 0.8 + 0.25 * (2.0 * noise_(seed + float(i) * 3.3) - 1.0);
    }
    l = m;
  }
  return 0.5 + 0.5 * sin(3.0 * a);
}

float3 color(float2 uv) {
  float t = 0.15 + 0.25 * exp(-4.0 * abs(genHeight(float3(uv, 0.5)) - 0.5));
  float3 c = pow(abs(((float3)(t))), colorPower);
  return c;
}

void Shade() {
  float3 c = emix(
    emix(color(uv - float2(0.0, 0.0)), color(uv - float2(1.0, 0.0)), uv.x),
    emix(color(uv - float2(0.0, 1.0)), color(uv - float2(1.0, 1.0)), uv.x), uv.y);
  RETURN(float4(c, 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
