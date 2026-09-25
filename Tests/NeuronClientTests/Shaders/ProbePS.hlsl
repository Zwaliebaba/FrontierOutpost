// Tests/NeuronClientTests/Shaders/ProbePS.hlsl
//
// What ShaderReflection.cpp reflects: loose globals, which FXC gathers into $Globals, a texture and
// a sampler, the shape liblt's shaders take once they are HLSL (Design/ADR/ADR-008).

float4 tint;
float gain;
Texture2D image : register(t0);
SamplerState imageSampler : register(s0);

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
  return image.Sample(imageSampler, uv) * tint * gain;
}
