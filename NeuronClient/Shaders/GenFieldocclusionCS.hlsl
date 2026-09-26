// FrontierOutpost/src/liblt/Shaders/GenFieldocclusionCS.hlsl
//
// GameData/shader/fragment/gen/fieldocclusion.jsl, as a compute shader (Design/ADR/ADR-009).
// Each thread is one vertex of a level's mesh: texel x, y of the position and normal textures,
// row by row. It writes the vertex's ambient occlusion to texel x, y of the one slice of
// occlusion, for the columns from firstColumn up to endColumn.

#include "Math.hlsli"
#include "Field.hlsli"

static const uint ITERATIONS = 32u;

Texture2D<float4> positions : register(t0);
Texture2D<float4> normals : register(t1);
Texture2D<float4> directions : register(t2);
Texture3D<float> fieldTexture : register(t3);
RWTexture3D<float> occlusion : register(u0);

uint samples;
uint dimension;
uint firstColumn;
uint endColumn;
float3 fieldOrigin;
float3 fieldExtent;
float3 fieldStep;
uint3 fieldResolution;

float Field(float3 p) {
  return SampleField(fieldTexture, fieldResolution, (p - fieldOrigin) / fieldExtent);
}

float Occlusion(float3 p, float3 n) {
  float3 tangent = abs(n.x) < 0.5 ? normalize(cross(n, float3(1, 0, 0))) : normalize(cross(n, float3(0, 1, 0)));
  float3 cotangent = cross(n, tangent);
  float occluded = 0.0;
  [loop] for (uint i = 0u; i < samples; ++i) {
    float3 dir = directions.Load(int3(i, 0, 0)).xyz;
    float3 samplePos = p + (dir.x * n + dir.y * tangent + dir.z * cotangent);
    occluded += exp(-1000.0 * max(Field(samplePos), 0.0));
  }
  return pow4(1.0 - sqrt(occluded / float(samples - 1u)));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  uint2 texel = uint2(id.x + firstColumn, id.y);
  if (texel.x >= endColumn || texel.y >= dimension)
    return;
  float3 p = positions.Load(int3(texel, 0)).xyz;
  float3 n = normals.Load(int3(texel, 0)).xyz;

  // From inside the field, the vertex steps out along its normal, as fieldocclusion.jsl's did.
  float value = Field(p);
  float3 iterationStep = n * fieldStep / float(ITERATIONS - 1u);
  [loop] for (uint i = 0u; i < ITERATIONS && value <= 1e-5; ++i) {
    p += iterationStep;
    value = Field(p);
  }
  occlusion[uint3(texel, 0)] = Occlusion(p, n);
}
