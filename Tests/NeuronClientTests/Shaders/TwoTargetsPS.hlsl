// Tests/NeuronClientTests/Shaders/TwoTargetsPS.hlsl
//
// With SolidVS.hlsl, a program that writes two targets, as liblt's write their colour and their
// linear depth: the colour to the first, and the colour reversed to the second.

float4 color;

struct Targets
{
  float4 first : SV_Target0;
  float4 second : SV_Target1;
};

Targets main()
{
  Targets targets;
  targets.first = color;
  targets.second = color.wzyx;
  return targets;
}
