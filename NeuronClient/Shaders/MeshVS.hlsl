// MeshVS.hlsl -- an authored mesh through the isometric camera, flat-shaded to one of the two
// colors it was authored with (ADR-012, ADR-003).
//
// The lighting decision is made here rather than in the pixel shader, and that is not an
// optimization. A face's normal is the same at all three of its vertices, so the choice between
// its shaded and its lit color is a property of the triangle, not of the pixel. Deciding it once
// per vertex and marking it nointerpolation makes that structural: the only two colors this pass
// can emit are the two that arrived on the vertex.

cbuffer MeshConstants : register(b0)
{
  row_major float4x4 g_viewProjection;
  row_major float4x4 g_world;
  // Points from the surface towards the light, in world space.
  float3 g_lightDirection;
  float g_litThreshold;
};

struct VertexIn
{
  float3 position : POSITION;
  // Not unit length: the mesh is authored as faces and the normal is the cross product of two of
  // the edges, computed at compile time. Normalizing needs a square root, which is not something
  // to ask of a constexpr in C++23, and the shader has to normalize anyway.
  float3 normal : NORMAL;
  // R8G8B8A8_UNORM on both, so they arrive as 0-1 floats and go out unchanged. The framebuffer is
  // the same format and is not _SRGB, so a channel authored as 0xAA is written back as 0xAA.
  float4 shadedColor : COLOR0;
  float4 litColor : COLOR1;
};

struct VertexOut
{
  float4 position : SV_Position;
  nointerpolation float4 color : COLOR0;
};

VertexOut main(VertexIn _input)
{
  VertexOut output;

  float4 worldPosition = mul(float4(_input.position, 1.0), g_world);
  output.position = mul(worldPosition, g_viewProjection);

  // w = 0: a direction is rotated by the world matrix but not translated by it. The ship only
  // ever rotates and translates, so there is no non-uniform scale to need an inverse transpose.
  float3 worldNormal = normalize(mul(float4(_input.normal, 0.0), g_world).xyz);

  bool lit = dot(worldNormal, g_lightDirection) > g_litThreshold;
  output.color = lit ? _input.litColor : _input.shadedColor;

  return output;
}
