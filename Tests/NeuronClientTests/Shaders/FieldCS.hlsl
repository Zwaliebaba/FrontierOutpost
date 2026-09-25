// Tests/NeuronClientTests/Shaders/FieldCS.hlsl
//
// What ComputeDispatches.cpp dispatches: a 3D field written through a UAV, as the SDF interpreter
// will write one (Design/ADR/ADR-009), from a 2D texture it reads and a constant, so that each of
// the compute root signature's parameters is used (Design/ADR/ADR-007).

uint3 size;        // the field's texels in each direction
float sliceStride; // added for each slice

Texture2D<float> offsets : register(t0);
RWTexture3D<float> field : register(u0);

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (all(id < size))
  {
    field[id] = offsets.Load(int3(int2(id.xy), 0)) + (sliceStride * (float)id.z);
  }
}
