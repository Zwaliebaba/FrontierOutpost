#pragma once

#include "Color.h"
#include "IsometricCamera.h"

namespace Neuron
{

/// The backdrop: three parallax layers of procedurally placed stars (ADR-010).
///
/// Drawn first, into the back buffer, before anything with depth. It writes SPACE_COLOR where
/// there is no star, so it costs nothing over the clear it replaces.
///
/// Nothing is stored. Space is unbounded, so a list of stars would either run out or repeat; a
/// hash of the pixel's position does neither and needs no memory, which is also the only shape
/// compatible with R13. See StarfieldPS.hlsl for the hash and why it does not shimmer.
class Starfield
{
public:
  static constexpr std::size_t LAYER_COUNT = 3;

  /// One pixel in (mask + 1) is a star. Powers of two less one, so the shader's test is an AND
  /// rather than a modulo. Sparser as the layers come nearer, which is what makes the near ones
  /// read as closer rather than as noise.
  ///
  /// ADR-010 set these to {2047, 4095, 8191} against a 640x400 screen, which put 125, 62 and 31
  /// stars of each layer on it. At 1280x720 the same masks would put 450, 225 and 112 there --
  /// 3.6 times as many, because that is how much bigger the screen got (ADR-013). Multiplying
  /// each by four restores the count: 921600 / 8192 is 112 stars in the far layer against the old
  /// 125, and the near layers follow.
  static constexpr std::array<std::uint32_t, LAYER_COUNT> LAYER_DENSITY_MASKS = {8191, 16383, 32767};

  /// Dim far, bright near. All three are grays: a colored star competes with the ship, and the
  /// ship is the thing the eye has to find.
  ///
  /// These live here rather than in the shader because Color.h is where this game's colors are
  /// named (ADR-011). Before that they were three palette indices the shader could write as
  /// literals; a color is four bytes and belongs with the other four-byte colors.
  static constexpr std::array<Color, LAYER_COUNT> LAYER_COLORS = {DARK_GRAY, LIGHT_GRAY, WHITE};

  /// What a pixel with no star in it is. The clear color of empty space, restated here because
  /// this pass covers every pixel and therefore overwrites the clear.
  static constexpr Color SPACE_COLOR = BLACK;

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

  /// The scroll offset of one layer, in pixels, for a camera at a given snapped pixel position.
  ///
  /// Exposed because it is the whole of the arithmetic worth testing: it is a floor division, and
  /// a truncating one would make every layer jump a pixel as the ship crossed the origin.
  /// Not noexcept: a layer index out of range is a broken invariant, and a broken invariant
  /// throws in this tree (Debug.h).
  [[nodiscard]] static std::int32_t LayerOffset(float _snappedCameraPixels, std::size_t _layer);

private:
  /// Three layers of int4 -- the offset in xy, the density mask in z and the packed color in w.
  /// One int4 a layer, because a constant buffer pads an array element to sixteen bytes however
  /// small it is, so the mask and the color ride along in padding that was there anyway.
  static constexpr std::uint32_t VALUES_PER_LAYER = 4;
  static constexpr std::uint32_t CONSTANT_COUNT = LAYER_COUNT * VALUES_PER_LAYER + 4;

  winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
  winrt::com_ptr<ID3D12PipelineState> m_pipeline;
};

} // namespace Neuron
