// Tests/NeuronClientTests/Shaders/AttributeVS.hlsl
//
// With AttributePS.hlsl, what DrawCalls.cpp draws to see what an input reads when the layout gives
// it no attribute, or fewer channels than it takes: GL's current attribute, (0, 0, 0, 1)
// (plan §5.5). Positions are in GL's clip space, taken to Direct3D's as SolidVS.hlsl takes them.

struct Varyings
{
  float4 position : SV_Position;
  float4 color : COLOR;
};

Varyings main(float3 position : POSITION, float4 color : COLOR)
{
  Varyings varyings;
  varyings.position = float4(position.x, -position.y, (position.z + 1.0) * 0.5, 1.0);
  varyings.color = color;
  return varyings;
}
