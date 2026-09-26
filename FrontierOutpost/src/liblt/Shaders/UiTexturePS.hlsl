// FrontierOutpost/src/liblt/Shaders/UiTexturePS.hlsl
//
// GameData/shader/fragment/ui/texture.jsl, in HLSL.

#include "Common.hlsli"

TEXTURE2D(texture_);
float alpha;

void Shade() {
  RETURN(float4(texture2DLod(texture_, uv, 0.0).xyz, alpha));
}

float4 main(Varyings input) : SV_Target0 {
  ReadVaryings(input);
  Shade();
  return fragment_color0;
}
