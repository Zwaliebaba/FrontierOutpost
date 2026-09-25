// Tests/NeuronClientTests/Shaders/NamesVS.hlsl
//
// With NamesPS.hlsl, the graphics program ProgramReflection.cpp reflects: constants in each stage's
// $Globals, one of them in both at different offsets, and an input signature with a system value
// in it (Design/ADR/ADR-008).

float4x4 transform;
float4 tint; // in NamesPS.hlsl too, where it comes first

struct Varyings
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
  float4 color : COLOR0;
};

Varyings main(float3 position : POSITION, float2 uv : TEXCOORD0, float4 color : COLOR0, float2 offset : TEXCOORD3,
              uint vertex : SV_VertexID)
{
  Varyings varyings;
  varyings.position = mul(transform, float4(position, 1.0));
  varyings.uv = uv + (offset * (float)(vertex & 1u));
  varyings.color = color * tint;
  return varyings;
}
