// Tests/NeuronClientTests/Shaders/NamesCS.hlsl
//
// The compute program ProgramReflection.cpp reflects: a constant, a texture it reads and a 3D
// texture it writes, at registers other than the first (Design/ADR/ADR-008).

uint3 size;
Texture2D<float4> source : register(t1);
RWTexture3D<float> field : register(u2);

[numthreads(4, 4, 4)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (all(id < size))
  {
    field[id] = source.Load(int3(int2(id.xy), 0)).r;
  }
}
