// FrontierOutpost/src/liblt/Shaders/NpmVS.hlsl
//
// GameData/shader/vertex/npm.jsl, in HLSL. ndcPos is taken, as it was, from GL's clip space.

#include "Common.hlsli"

float4x4 WORLD;
float4x4 WVP;
float4x4 WORLDIT;

Varyings main(float3 vertex_position : ATTRIB0, float3 vertex_normal : ATTRIB1, float2 vertex_uv : ATTRIB2) {
  VS_PROLOGUE;

  float3 scale = float3(length(mul(WORLD, float4(1, 0, 0, 0)).xyz),
                        length(mul(WORLD, float4(0, 1, 0, 0)).xyz),
                        length(mul(WORLD, float4(0, 0, 1, 0)).xyz));

  float3 origin = mul(WORLD, float4(0.0, 0.0, 0.0, 1.0)).xyz;

  gl_Position = mul(WVP, vp);
  linearDepth = gl_Position.z;
  gl_Position.z = LogDepth(gl_Position.z, gl_Position.w);

  float3 position = worldPos.xyz;
  float4 ndcPos = gl_Position;

  float3 normal = normalize(mul(WORLDIT, vn).xyz);

  vertpos = vp.xyz;
  vertnormal = vn.xyz;
  float3 vertposscaled = vertpos * scale;

  Varyings varyings = WriteVaryings();
  varyings.normal = normal;
  varyings.position = position;
  varyings.ndcPos = ndcPos;
  varyings.vertposscaled = vertposscaled;
  varyings.origin = origin;
  varyings.scale = scale;
  return varyings;
}
