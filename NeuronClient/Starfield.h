#pragma once

#include "IsometricCamera.h"

namespace Neuron
{

/// The backdrop: three parallax layers of procedurally placed stars (ADR-010).
///
/// Drawn first, into the index target, before anything with depth. It writes index 0 where there
/// is no star, so it costs nothing over the clear it replaces.
///
/// Nothing is stored. Space is unbounded, so a list of stars would either run out or repeat; a
/// hash of the texel's position does neither and needs no memory, which is also the only shape
/// compatible with R13. See StarfieldPS.hlsl for the hash and why it does not shimmer.
class Starfield
{
public:
  static constexpr std::size_t LAYER_COUNT = 3;

  /// How much slower each layer scrolls than the camera. A divisor of 8 is far away and barely
  /// moves; 2 is near and moves at half the camera's rate. Nothing scrolls at the camera's full
  /// rate, because that would put the star in the same plane as the ship.
  ///
  /// These have to divide a whole number of pixels into a whole number of pixels, which they do
  /// because the camera's offset is already snapped -- the divisions happen on the CPU, in
  /// integers, with a floor rather than a truncation so that the layers do not stutter as the
  /// ship crosses the origin.
  static constexpr std::array<std::int32_t, LAYER_COUNT> LAYER_PARALLAX_DIVISORS = {8, 4, 2};

  void Create(ID3D12Device* _device);

  void Draw(ID3D12GraphicsCommandList* _commandList, const IsometricCamera& _camera);

  /// The scroll offset of one layer, in texels, for a camera at a given snapped pixel position.
  ///
  /// Exposed because it is the whole of the arithmetic worth testing: it is a floor division, and
  /// a truncating one would make every layer jump a pixel as the ship crossed the origin.
  /// Not noexcept: a layer index out of range is a broken invariant, and a broken invariant
  /// throws in this tree (Debug.h).
  [[nodiscard]] static std::int32_t LayerOffset(float _snappedCameraPixels, std::size_t _layer);

private:
  /// Three layers of int4, because a constant buffer pads an array element to sixteen bytes
  /// however small it is.
  static constexpr std::uint32_t CONSTANT_COUNT = LAYER_COUNT * 4;

  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
};

} // namespace Neuron
