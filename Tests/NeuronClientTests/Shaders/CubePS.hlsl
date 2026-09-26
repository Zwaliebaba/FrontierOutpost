// Tests/NeuronClientTests/Shaders/CubePS.hlsl
//
// With TexturedVS.hlsl, a program that samples a cube in one direction everywhere.

float3 direction;
TextureCube cube : register(t0);
SamplerState cubeSampler : register(s0);

float4 main() : SV_Target
{
  return cube.Sample(cubeSampler, direction);
}
