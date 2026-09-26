// Tests/NeuronClientTests/Shaders/SolidPS.hlsl
//
// One colour for everything SolidVS.hlsl draws.

float4 color;

float4 main() : SV_Target
{
  return color;
}
