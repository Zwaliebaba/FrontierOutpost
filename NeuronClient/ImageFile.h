// NeuronClient/ImageFile.h
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Neuron
{

/// An image as RGBA8 pixels, four bytes each, the top row first. WIC decodes and encodes it
/// (Design/ADR/ADR-011); reading and writing the files is the caller's.
class ImageFile
{
public:
  /// Decodes a PNG or JPEG file's bytes into _outImage. On failure returns false and says why in
  /// _error.
  [[nodiscard]] static bool Decode(std::span<const std::byte> _fileBytes, ImageFile& _outImage, std::string& _error);

  /// Encodes RGBA8 pixels, the top row first, as a PNG file's bytes. _rgba holds
  /// _widthPixels * _heightPixels * 4 bytes. On failure returns false and says why in _error.
  [[nodiscard]] static bool EncodePng(std::uint32_t _widthPixels, std::uint32_t _heightPixels, std::span<const std::byte> _rgba,
                                      std::vector<std::byte>& _outFileBytes, std::string& _error);

  [[nodiscard]] std::uint32_t WidthPixels() const noexcept
  {
    return m_widthPixels;
  }

  [[nodiscard]] std::uint32_t HeightPixels() const noexcept
  {
    return m_heightPixels;
  }

  [[nodiscard]] std::span<const std::byte> Pixels() const noexcept
  {
    return m_pixels;
  }

private:
  std::uint32_t m_widthPixels = 0;
  std::uint32_t m_heightPixels = 0;
  std::vector<std::byte> m_pixels;
};

} // namespace Neuron
