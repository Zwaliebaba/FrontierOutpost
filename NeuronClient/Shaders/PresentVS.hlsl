// NeuronClient/Shaders/PresentVS.hlsl
//
// The present pass's triangle, which covers the back buffer, made from the vertex index alone. It
// is in Direct3D's clip space and not GL's: the pass is the core's own, and liblt's conventions end
// at the image it presents (plan §5.5).

float4 main(uint vertex : SV_VertexID) : SV_Position
{
  // (-1, -1), (3, -1) and (-1, 3).
  const float2 corner = float2(float((vertex << 1) & 2), float(vertex & 2));
  return float4((corner * 2.0) - 1.0, 0.0, 1.0);
}
