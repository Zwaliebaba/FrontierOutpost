// Tests/NeuronClientTests/Shaders/NamedBufferPS.hlsl
//
// A pixel shader ProgramReflection.cpp expects a program to be refused for: it declares a cbuffer
// of its own, where the core binds only $Globals, at b0 (Design/ADR/ADR-008).

cbuffer Lighting : register(b1)
{
  float4 ambient;
};

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0) : SV_Target
{
  return ambient * color;
}
