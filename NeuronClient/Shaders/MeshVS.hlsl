// MeshVS.hlsl -- world-space solids, through the map's camera.
//
// This is the one vertex shader that takes a matrix (ADR-103). The interface passes arrive in
// canvas pixels because they are drawn in canvas pixels; a solid is in the world and has a back
// and a depth, and the camera that used to be applied one point at a time on the CPU is applied
// here to every vertex instead -- the day OrbitCamera.h said it would grow a matrix.

cbuffer MeshConstants : register(b0)
{
  // Row-major, so the sixteen floats the backend writes are four rows and the vertex is a row
  // vector on the left: OrbitCamera::ViewProjection builds it that way round.
  row_major float4x4 g_viewProjection;
  float3 g_lightDirection;
  float g_lightPadding;
  float3 g_eyePosition;
  float g_eyePadding;
};

struct VertexIn
{
  float3 position : POSITION;
  float3 normal : NORMAL;
  // R8G8B8A8_UNORM, so the four bytes Pack() wrote arrive as 0-1 floats. Five of them: the band
  // the light finds, the band it grazes, the shadow, the silhouette it gets past, and the glint it
  // bounces straight back (ADR-105, ADR-106).
  float4 litColor : COLOR0;
  float4 halfLitColor : COLOR1;
  float4 darkColor : COLOR2;
  float4 rimColor : COLOR3;
  float4 glintColor : COLOR4;
};

struct VertexOut
{
  float4 position : SV_Position;
  // INTERPOLATED, and the only two things here that are. The pixel shader decides which tone a
  // pixel gets from the normal, so on a sphere the boundaries between them are curves through the
  // triangles rather than sets of triangle edges; and it needs the world position to know which
  // way the eye is from THIS pixel, which is what makes the rim a property of the silhouette
  // rather than of the pane.
  float3 normal : NORMAL;
  float3 worldPosition : POSITION;
  // NOT interpolated. A tone that interpolated would be a gradient between two authored colours,
  // which is exactly the value ADR-012 says nobody chose.
  nointerpolation float4 litColor : COLOR0;
  nointerpolation float4 halfLitColor : COLOR1;
  nointerpolation float4 darkColor : COLOR2;
  nointerpolation float4 rimColor : COLOR3;
  nointerpolation float4 glintColor : COLOR4;
};

VertexOut main(VertexIn _input)
{
  VertexOut output;

  output.position = mul(float4(_input.position, 1.0), g_viewProjection);
  output.normal = _input.normal;
  output.worldPosition = _input.position;
  output.litColor = _input.litColor;
  output.halfLitColor = _input.halfLitColor;
  output.darkColor = _input.darkColor;
  output.rimColor = _input.rimColor;
  output.glintColor = _input.glintColor;

  return output;
}
