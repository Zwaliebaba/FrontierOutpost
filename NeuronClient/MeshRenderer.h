#pragma once

#include "IsometricCamera.h"
#include "Mesh.h"

namespace Neuron
{

/// Draws authored meshes with a world matrix each, through a camera.
///
/// It owns the pipeline and nothing else: one MeshRenderer draws every Mesh in the scene. What a
/// mesh MEANS is the game's business (R9) -- the ship and the station live in FrontierOutpost/,
/// and what crosses the boundary is Neuron::MeshVertex and nothing ship- or station-shaped.
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
  /// between the two tones as the ship turns. With the hull in ShipMesh.h, the lit faces come out
  /// between 0.40 and 0.59 and the shaded ones between -0.70 and -0.15, so 0.20 sits in a gap
  /// with room on both sides rather than on top of anything.
  static constexpr float LIT_THRESHOLD = 0.20F;

  void Create(ID3D12Device* _device);

  /// One mesh, one world matrix. The root signature, the pipeline and the camera constants are
  /// re-set per call rather than hoisted into a BeginFrame: at two meshes a frame that costs
  /// nothing measurable, and a Draw that is complete in itself is a Draw that cannot be called in
  /// the wrong order.
  void Draw(ID3D12GraphicsCommandList* _commandList, const Mesh& _mesh, const IsometricCamera& _camera,
            const std::array<float, 16>& _worldMatrix);

private:
  /// Two matrices and the light: 16 + 16 + 4 DWORDs of the root signature's 64. Root constants
  /// rather than a constant buffer, for the same reason as the resolve pass -- no upload heap, no
  /// per-frame versioning, nothing to keep alive (ADR-001).
  static constexpr std::uint32_t CONSTANT_COUNT = 36;

  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
};

} // namespace Neuron
