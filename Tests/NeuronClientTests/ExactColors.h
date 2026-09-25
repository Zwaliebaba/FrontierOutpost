// Tests/NeuronClientTests/ExactColors.h
//
// A colour each colour format stores exactly, and the bytes it stores it as, for the tests that
// clear or draw and read back what they wrote. Included after pch.h.
#pragma once

#include "Texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace NeuronClientTests
{

inline constexpr std::array<Neuron::TextureFormat, 7> COLOR_FORMATS = {
  Neuron::TextureFormat::R8,      Neuron::TextureFormat::Rg8,  Neuron::TextureFormat::Rgba8,  Neuron::TextureFormat::R16F,
  Neuron::TextureFormat::Rgba16F, Neuron::TextureFormat::R32F, Neuron::TextureFormat::Rgba32F};

/// 51, 102, 153 and 204 in a UNORM format: each k / 255, which rounds back to k exactly.
inline constexpr std::array<float, 4> UNORM_COLOR = {0.2f, 0.4f, 0.6f, 0.8f};
inline constexpr std::array<std::uint8_t, 4> UNORM_BYTES = {51, 102, 153, 204};

/// Exact in every float format, halves included, and outside [0, 1], which a float target keeps.
inline constexpr std::array<float, 4> FLOAT_COLOR = {0.25f, -0.5f, 2.0f, 1024.0f};
inline constexpr std::array<std::uint16_t, 4> FLOAT_HALVES = {0x3400, 0xB800, 0x4000, 0x6400};

inline bool IsFloat(Neuron::TextureFormat _format) noexcept
{
  return _format == Neuron::TextureFormat::R16F || _format == Neuron::TextureFormat::Rgba16F || _format == Neuron::TextureFormat::R32F ||
         _format == Neuron::TextureFormat::Rgba32F;
}

/// A colour every channel of _format stores exactly.
inline const std::array<float, 4>& ExactColor(Neuron::TextureFormat _format) noexcept
{
  return IsFloat(_format) ? FLOAT_COLOR : UNORM_COLOR;
}

/// _texels texels of _format holding ExactColor, packed as ReadTexture gives them.
inline std::vector<std::byte> ExactTexels(Neuron::TextureFormat _format, std::size_t _texels)
{
  const std::uint32_t texelBytes = Neuron::TexelBytes(_format);
  std::array<std::byte, 16> texel{};
  switch (_format)
  {
  case Neuron::TextureFormat::R16F:
  case Neuron::TextureFormat::Rgba16F:
    std::memcpy(texel.data(), FLOAT_HALVES.data(), texelBytes);
    break;
  case Neuron::TextureFormat::R32F:
  case Neuron::TextureFormat::Rgba32F:
    std::memcpy(texel.data(), FLOAT_COLOR.data(), texelBytes);
    break;
  default:
    std::memcpy(texel.data(), UNORM_BYTES.data(), texelBytes);
    break;
  }
  std::vector<std::byte> texels(_texels * texelBytes);
  for (std::size_t index = 0; index < _texels; ++index)
  {
    std::memcpy(&texels[index * texelBytes], texel.data(), texelBytes);
  }
  return texels;
}

} // namespace NeuronClientTests
