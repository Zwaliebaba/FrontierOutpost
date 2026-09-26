// Tests/NeuronClientTests/Shaders/TexturedPS.hlsl
//
// With TexturedVS.hlsl, a program that samples a 2D texture at t0 through the sampler at s0.

Texture2D image : register(t0);
SamplerState imageSampler : register(s0);

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
  return image.Sample(imageSampler, uv);
}
