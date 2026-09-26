// FrontierOutpost/src/liblt/Shaders/Raytracing.hlsli
//
// GameData/shader/common/raytracing.jsl, in HLSL.

#ifndef RAYTRACING_HLSLI
#define RAYTRACING_HLSLI

float2 interSphere(float4 sphere, float3 ro, float3 rd) {
  float3 v = sphere.xyz - ro;
  float b = dot(rd, v);
  float d = b*b - dot(v, v) + sphere.w * sphere.w;
  return d >= 0.0 ? float2(b - sqrt(d), b + sqrt(d)) : ((float2)(1.0e30));
}

#endif
