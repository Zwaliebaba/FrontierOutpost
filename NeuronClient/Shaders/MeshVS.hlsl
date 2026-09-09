// MeshVS.hlsl -- an authored mesh through the isometric camera, flat-shaded to two palette
// indices (ADR-002, ADR-003).
//
// The lighting decision is made here rather than in the pixel shader, and that is not an
// optimization. A face's normal is the same at all three of its vertices, so the choice between
// the dark and the bright variant of its color is a property of the triangle, not of the pixel.
// Deciding it once per vertex and marking it nointerpolation makes that structural: there is no
// arithmetic anywhere that could produce a third tone.

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
  // The DARK half of the palette pair, 0-7. The light adds 8 (ADR-002).
  uint paletteIndex : TEXCOORD0;
};

struct VertexOut
{
  float4 position : SV_Position;
  nointerpolation uint paletteIndex : TEXCOORD0;
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
  output.paletteIndex = _input.paletteIndex + (lit ? 8u : 0u);

  return output;
}
