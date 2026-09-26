// FrontierOutpost/src/liblt/Shaders/CubemapMultiplyPS.hlsl
//
// GameData/shader/fragment/cubemap/multiply.jsl, in HLSL.

#include "Common.hlsli"
#include "Color.hlsli"
#include "Cube.hlsli"

TEXTURECUBE(texture1);
TEXTURECUBE(texture2);

void Shade() {
  float3 p = GetCubePosition(uv, true);
  float4 c1 = textureCube(texture1, p);
  float4 c2 = textureCube(texture2, p);
  // RETURN(vec4((c1 * c2).xyz, c1.w));
  c1.xyz = 0.15 * (c1.xyz / lum(c2.xyz));
  RETURN(c1);
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
