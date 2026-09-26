// FrontierOutpost/src/liblt/Shaders/Deferred.hlsli
//
// GameData/shader/common/deferred.jsl, in HLSL.

#ifndef DEFERRED_HLSLI
#define DEFERRED_HLSLI

TEXTURE2D(albedoSampler);
TEXTURE2D(depthBuffer);

static float3 worldRayO = 0.0;
static float3 worldRayD = 0.0;

// The varyings this file reads, which a shader that includes it copies in.
void ReadDeferredVaryings(Varyings varyings) {
  worldRayO = varyings.worldRayO;
  worldRayD = varyings.worldRayD;
}

float GetDepth() {
  return texture2D(depthBuffer, uv).x;
}

float GetDepthNormalized() {
  return texture2D(depthBuffer, uv).x / farPlane;
}

float3 GetWorldPos() {
  return worldRayO + worldRayD * GetDepth();
}

#endif
