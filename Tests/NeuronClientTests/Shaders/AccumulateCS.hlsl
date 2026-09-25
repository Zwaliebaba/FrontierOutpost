// Tests/NeuronClientTests/Shaders/AccumulateCS.hlsl
//
// A dispatch that reads what it writes, so that two in a row need the UAV barrier the context puts
// between them.

uint2 size;
float amount;

RWTexture2D<float> total : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
  if (all(id.xy < size))
  {
    total[id.xy] = total[id.xy] + amount;
  }
}
