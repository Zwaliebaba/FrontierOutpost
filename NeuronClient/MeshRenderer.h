#pragma once

#include "IsometricCamera.h"

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

/// Draws one authored mesh with a world matrix, through a camera.
///
/// It knows nothing about ships (R9). What it takes is vertices, indices and two matrices; what
/// a mesh means is the game's business, and the ship lives in FrontierOutpost/ShipMesh.h.
class MeshRenderer
{
public:
  /// The light, in world space, as a unit direction from the surface towards it. Fixed for the
  /// MVP: a single hard key light is what gives flat-shaded faces two clean tones (ADR-002).
  ///
  /// It is raked in from above and from the port quarter rather than pointed straight down, and
  /// that is the whole difference between a ship and a silhouette. The camera looks along
  /// (1, 1, 1), so everything it can see faces broadly up and towards +X or +Z; a light from
  /// directly above lights all of it and the hull comes out one flat tone. Coming across the
  /// hull, it puts the deck and the nose and tail slopes in the bright tone and every flank the
  /// camera can see in the dark one.
  static constexpr float LIGHT_DIRECTION_X = 0.249F;
  static constexpr float LIGHT_DIRECTION_Y = 0.549F;
  static constexpr float LIGHT_DIRECTION_Z = -0.798F;

  /// A face is lit when its normal points within this much of the light. Zero would put the
  /// boundary exactly at grazing incidence, where a face that is edge-on to the light flickers
  /// between the two tones as the ship turns. With the hull above, the lit faces come out between
  /// 0.40 and 0.59 and the shaded ones between -0.70 and -0.15, so 0.20 sits in a gap with room
  /// on both sides rather than on top of anything.
  static constexpr float LIT_THRESHOLD = 0.20F;

  void Create(ID3D12Device* _device, std::span<const MeshVertex> _vertices, std::span<const std::uint16_t> _indices);

  void Draw(ID3D12GraphicsCommandList* _commandList, const IsometricCamera& _camera, const std::array<float, 16>& _worldMatrix);

private:
  /// Two matrices and the light: 16 + 16 + 4 DWORDs of the root signature's 64. Root constants
  /// rather than a constant buffer, for the same reason as the resolve pass -- no upload heap, no
  /// per-frame versioning, nothing to keep alive (ADR-001).
  static constexpr std::uint32_t CONSTANT_COUNT = 36;

  void CreatePipeline(ID3D12Device* _device);
  void CreateBuffers(ID3D12Device* _device, std::span<const MeshVertex> _vertices, std::span<const std::uint16_t> _indices);

  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
  winrt::com_ptr<ID3D12Resource> m_vertices;
  winrt::com_ptr<ID3D12Resource> m_indices;
  D3D12_VERTEX_BUFFER_VIEW m_vertexView = {};
  D3D12_INDEX_BUFFER_VIEW m_indexView = {};
  std::uint32_t m_indexCount = 0;
};

} // namespace Neuron
