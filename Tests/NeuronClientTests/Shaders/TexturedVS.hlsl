// Tests/NeuronClientTests/Shaders/TexturedVS.hlsl
//
// What TexturedDraws.cpp draws with: positions as SolidVS.hlsl takes them, and texture coordinates,
// which GL puts at the bottom left of an image, where the core keeps them (plan §5.5).

struct Varyings
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

Varyings main(float3 position : POSITION, float2 uv : TEXCOORD0)
{
  Varyings varyings;
  varyings.position = float4(position.x, -position.y, (position.z + 1.0) * 0.5, 1.0);
  varyings.uv = uv;
  return varyings;
}
