// FrontierOutpost/src/liblt/Shaders/UiTextureadditivePS.hlsl
//
// GameData/shader/fragment/ui/textureadditive.jsl, in HLSL.

#include "Common.hlsli"

TEXTURE2D(texture_);
float alpha;

void Shade() {
  RETURN(((float4)(texture2D(texture_, uv) * alpha)));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
