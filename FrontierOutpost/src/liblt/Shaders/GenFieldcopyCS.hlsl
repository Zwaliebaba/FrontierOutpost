// FrontierOutpost/src/liblt/Shaders/GenFieldcopyCS.hlsl
//
// GameData/shader/fragment/gen/fieldcopy.jsl, as a compute shader (Design/ADR/ADR-009): it
// resamples the field GenFieldCS.hlsl filled into one level of detail's grid, which
// LTE/SDFMesh.cpp reads back for the marching cubes. Voxel i of n takes the field i / (n - 1) of
// the way across it, as fieldcopy.jsl's texel did.

#include "Field.hlsli"

Texture3D<float> fieldTexture : register(t0);
RWTexture3D<float> level : register(u0);

uint3 fieldResolution;
uint3 resolution;

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id >= resolution))
    return;
  level[id] = SampleField(fieldTexture, fieldResolution, float3(id) / float3(resolution - 1));
}
