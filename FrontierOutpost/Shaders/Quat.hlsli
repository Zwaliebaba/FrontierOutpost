// FrontierOutpost/src/liblt/Shaders/Quat.hlsli
//
// GameData/shader/common/quat.jsl, in HLSL.

#ifndef QUAT_HLSLI
#define QUAT_HLSLI

float3 quatMul(float4 q, float3 v) {
  float3 t = 2.0 * cross(q.xyz, v);
  return v + q.w * t + cross(q.xyz, t);
}

float4 quatRotation(float3 axis, float angle) {
  float ca = cos(angle);
  float sa = sin(angle);
  return float4(sa * axis, ca);
}

#endif
