// Tests/NeuronClientTests/Shaders/SolidVS.hlsl
//
// What DrawCalls.cpp draws with: positions in GL's clip space, taken to Direct3D's as liblt's
// shaders will take them, by negating y and mapping z from [-w, w] to [0, w] (Design/ADR/ADR-007;
// plan §5.5).

float4 offset; // added to every position, so that one mesh can be drawn in several places

float4 main(float3 position : POSITION) : SV_Position
{
  const float4 clip = float4(position, 1.0) + offset;
  return float4(clip.x, -clip.y, (clip.z + clip.w) * 0.5, clip.w);
}
