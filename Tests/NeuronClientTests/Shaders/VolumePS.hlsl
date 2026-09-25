// Tests/NeuronClientTests/Shaders/VolumePS.hlsl
//
// With TexturedVS.hlsl, a program that samples a 3D texture at one coordinate everywhere.

float3 coordinate;
Texture3D volume : register(t0);
SamplerState volumeSampler : register(s0);

float4 main() : SV_Target
{
  return volume.Sample(volumeSampler, coordinate);
}
