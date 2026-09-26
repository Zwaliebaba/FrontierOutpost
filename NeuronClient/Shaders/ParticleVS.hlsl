// FrontierOutpost/src/liblt/Shaders/ParticleVS.hlsl
//
// GameData/shader/vertex/particle.jsl, in HLSL. It writes no linear depth, as the .jsl wrote none.

#include "Common.hlsli"
#include "Math.hlsli"

float4x4 WORLD;
float4x4 VIEW;
float4x4 PROJ;

float3 eye;
float3 camUp;
float fadeIn;
float fadeOut;

static const float zOffset = 6.0;

Varyings main(float3 vertex_position : ATTRIB0, float3 vertex_normal : ATTRIB1, float2 vertex_uv : ATTRIB2,
              float3 vertex_color : ATTRIB3) {
  VS_PROLOGUE;

  float3 position = vertex_position;
  float3 toCam = eye - position;
  float dist = length(toCam);
  toCam /= dist;
  float3 right = normalize(cross(camUp, toCam));
  float3 up = cross(right, toCam);

  float size = vertex_normal.x;
  float age = vertex_normal.y;

  float opacityMult = saturate((1. - age) / fadeOut) * saturate(age / fadeIn);
  float3 attrib = vertex_color;

  position += size * u * right;
  position += size * v * up;
  position += toCam * min(zOffset, dist * 0.25);

  gl_Position = mul(PROJ, mul(VIEW, float4(position, 1.0)));
  gl_Position.z = LogDepth(gl_Position.z, gl_Position.w);

  Varyings varyings = WriteVaryings();
  varyings.opacityMult = opacityMult;
  varyings.attrib = attrib;
  return varyings;
}
