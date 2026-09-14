// MeshPS.hlsl -- two tones, decided per pixel, nothing in between.
//
// THIS IS THE LIGHTING AND IT IS ALL OF IT (ADR-103). The GPU decides WHICH of the two authored
// tones a pixel gets and never invents a third: there is no Lambert ramp, no ambient term, no
// product of a colour and a number anywhere in this file, because every face on this screen is one
// of two colours somebody chose (ADR-002, ADR-012) and a value between them is a colour nobody did
// (ADR-011, ADR-014). What moved from those ADRs is only WHERE the choice is made -- per pixel
// rather than per face -- so that on a sphere the terminator is a hard curve that turns as the
// camera orbits rather than a staircase of facets.

cbuffer MeshConstants : register(b0)
{
  row_major float4x4 g_viewProjection;
  float3 g_lightDirection;
  float g_padding;
};

struct VertexOut
{
  float4 position : SV_Position;
  float3 normal : NORMAL;
  nointerpolation float4 litColor : COLOR0;
  nointerpolation float4 darkColor : COLOR1;
};

// Where the lit side ends. Not zero: at grazing incidence a normal turning slowly through the
// boundary would flicker between the tones frame to frame, and a little way past it the lit side
// reads as a cap rather than a hemisphere, which is what makes a ball read as a ball (ADR-002's
// threshold, kept for its reason).
static const float TERMINATOR = 0.15;

float4 main(VertexOut _input) : SV_Target
{
  float facing = dot(normalize(_input.normal), g_lightDirection);
  return facing > TERMINATOR ? _input.litColor : _input.darkColor;
}
