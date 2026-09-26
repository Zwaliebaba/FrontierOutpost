// FrontierOutpost/src/liblt/Shaders/UiCompositePS.hlsl
//
// GameData/shader/fragment/ui/composite.jsl, in HLSL.

#include "Common.hlsli"
#include "Noise.hlsli"

TEXTURE2D(source);
TEXTURE2D(layer);

void Shade() {
  float4 c1 = texture2D(source, uv);
  float4 c2 = texture2D(layer, uv);
  c2.xyz = max(c2.xyz, ((float3)(0.0)));
  // c2.w = 1.5 * pow(c2.w, 0.75);
  c2.w = saturate(c2.w);
  float3 final = c1.xyz * (1.0 - c2.w) + c2.xyz;
  RETURN(float4(final, 1.0));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
