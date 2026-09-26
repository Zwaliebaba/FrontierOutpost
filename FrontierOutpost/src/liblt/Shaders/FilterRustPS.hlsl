// FrontierOutpost/src/liblt/Shaders/FilterRustPS.hlsl
//
// GameData/shader/fragment/filter_rust.jsl, in HLSL.

#include "Common.hlsli"
#include "Math.hlsli"
#include "Noise.hlsli"

TEXTURE2D(texture_);

float3 Toroidal(float2 st, float r1, float r2) {
  st *= TAU;
  float3 fromCenter = float3(cos(st.x), sin(st.x), 0.0);
  float3 outerPoint =
    -cos(st.y) * fromCenter +
     sin(st.y) * float3(0.0, 0.0, 1.0);
  return r1 * fromCenter + r2 * outerPoint;
}

void Shade() {
  float2 uvp = uv;
  uvp.y = 1.0 - uvp.y;
  float3 p = 3.0 * Toroidal(uvp, 5.0, 1.0);
  float c = lerp(0.8, 1.3, pow(abs(fsnoise(p, 8, 1.5)), 2.0));
  c -= 0.1 * noise_(p.xy);
  c = saturate(c);
  RETURN(float4(c, c, c, 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
