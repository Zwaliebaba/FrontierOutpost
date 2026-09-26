// Tests/NeuronClientTests/Shaders/NamesPS.hlsl
//
// The pixel stage of NamesVS.hlsl's program. HLSL reserves texture and sample, and has saturate and
// noise as intrinsics, so liblt's names for them end in an underscore here, and the program's
// lookups map them back (Design/ADR/ADR-008). It writes two targets.

float4 tint;     // in NamesVS.hlsl too, after its transform
float saturate_; // liblt's saturate

Texture2D texture_ : register(t0); // liblt's texture
Texture3D noise_ : register(t3);   // liblt's noise
SamplerState clamped : register(s0);
SamplerState sample_ : register(s1); // liblt's sample

struct Targets
{
  float4 color : SV_Target0;
  float4 glow : SV_Target1;
};

Targets main(float4 position : SV_Position, float2 uv : TEXCOORD0, float4 color : COLOR0)
{
  const float4 base = texture_.Sample(clamped, uv) * color * tint;
  const float gray = dot(base.rgb, float3(0.299, 0.587, 0.114));
  Targets targets;
  targets.color = float4(lerp(gray.xxx, base.rgb, saturate_), base.a);
  targets.glow = noise_.Sample(sample_, float3(uv, position.z));
  return targets;
}
