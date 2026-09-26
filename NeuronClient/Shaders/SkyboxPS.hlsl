// FrontierOutpost/src/liblt/Shaders/SkyboxPS.hlsl
//
// GameData/shader/fragment/skybox.jsl, in HLSL. It writes two targets.

#include "Common.hlsli"
#include "Math.hlsli"

TEXTURECUBE(envMap);
TEXTURECUBE(irMap);

void Shade() {
  float3 c = textureCube(envMap, vertpos).xyz;
  c = toLinear(c);
  outputAlbedo(c);
  outputAlpha(1.0);
  outputMaterial(MATERIAL_NOSHADE);
}

TwoTargets main(Varyings input) {
  ReadVaryings(input);
  Shade();
  TwoTargets targets;
  targets.color0 = fragment_color0;
  targets.color1 = fragment_color1;
  return targets;
}
