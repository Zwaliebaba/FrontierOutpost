// FrontierOutpost/src/liblt/Shaders/Softparticle.hlsli
//
// GameData/shader/common/softparticle.jsl, in HLSL.

#ifndef SOFTPARTICLE_HLSLI
#define SOFTPARTICLE_HLSLI

TEXTURE2D(depthBuffer);
static float4 ndcPos = 0.0;

// The varying this file reads, which a shader that includes it copies in.
void ReadSoftparticleVaryings(Varyings varyings) {
  ndcPos = varyings.ndcPos;
}

float2 GetSSPosition() {
  return 0.5 * (ndcPos.xy / ndcPos.w) + 0.5;
}

float GetDepthDifference() {
  return saturate((texture2D(depthBuffer, GetSSPosition()).r - linearDepth) / farPlane);
}

#endif
