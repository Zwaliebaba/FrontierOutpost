#pragma once

namespace Neuron
{

/// One vertex of an authored mesh. R8: a public aggregate handed to the GPU, so plain fields, and
/// the layout is load-bearing -- MeshRenderer's input layout describes exactly these offsets.
///
/// The normal is a face normal and is NOT unit length: meshes are authored as faces and the
/// normal is the cross product of two edges, which a constexpr can compute and a square root
/// cannot. MeshVS normalizes.
struct MeshVertex
{
  float positionX;
  float positionY;
  float positionZ;
  float normalX;
  float normalY;
  float normalZ;
  /// The dark half of the palette pair, 0-7; the light adds 8 (ADR-002).
  std::uint32_t paletteIndex;
};

/// The GPU buffers for one authored mesh.
///
/// Split from MeshRenderer the moment a second mesh existed, which is what R2 asks for: the
/// pipeline is a property of HOW meshes are drawn and the buffers are a property of WHICH mesh,
/// and one MeshRenderer now draws many Meshes rather than every mesh carrying a duplicate root
/// signature and pipeline state around with it.
///
/// It knows nothing about ships or stations (R9). What it takes is vertices and indices.
class Mesh
{
public:
  void Create(ID3D12Device* _device, std::span<const MeshVertex> _vertices, std::span<const std::uint16_t> _indices);

  [[nodiscard]] const D3D12_VERTEX_BUFFER_VIEW& VertexView() const noexcept
  {
    return m_vertexView;
  }
  [[nodiscard]] const D3D12_INDEX_BUFFER_VIEW& IndexView() const noexcept
  {
    return m_indexView;
  }
  [[nodiscard]] std::uint32_t IndexCount() const noexcept
  {
    return m_indexCount;
  }

private:
  winrt::com_ptr<ID3D12Resource> m_vertices;
  winrt::com_ptr<ID3D12Resource> m_indices;
  D3D12_VERTEX_BUFFER_VIEW m_vertexView = {};
  D3D12_INDEX_BUFFER_VIEW m_indexView = {};
  std::uint32_t m_indexCount = 0;
};

/// Where to put a mesh: a rotation about Y by a heading, then a translation. Row-major, for
/// `mul(float4(position, 1), matrix)` in HLSL, which is the convention IsometricCamera uses too.
///
/// Heading zero points along +X and increases towards +Z -- the same sense the server's turns16
/// heading uses, so a heading can cross from the simulation to here without being reinterpreted.
[[nodiscard]] inline std::array<float, 16> WorldMatrix(float _headingRadians, float _x, float _y, float _z) noexcept
{
  const float cosine = std::cos(_headingRadians);
  const float sine = std::sin(_headingRadians);

  return std::array<float, 16>{
    cosine, 0.0F, sine,   0.0F, //
    0.0F,   1.0F, 0.0F,   0.0F, //
    -sine,  0.0F, cosine, 0.0F, //
    _x,     _y,   _z,     1.0F,
  };
}

} // namespace Neuron
