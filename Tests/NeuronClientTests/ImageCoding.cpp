// Tests/NeuronClientTests/ImageCoding.cpp
//
// Neuron::ImageFile decodes PNG and JPEG to RGBA8 with the top row first, and a PNG it encodes
// decodes to the same bytes (Design/ADR/ADR-011).
#include "pch.h"

#include "Check.h"
#include "ImageFile.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

// A 2x2 PNG, written byte by byte: red and green on the top row, and blue and half-transparent
// white below it.
constexpr std::array<std::uint8_t, 76> PNG_2X2 = {137, 80, 78,  71,  13,  10, 26,  10,  0,   0,   0,   13,  73,  72, 68, 82,  0,  0,  0,
                                                  2,   0,  0,   0,   2,   8,  6,   0,   0,   0,   114, 182, 13,  36, 0,  0,   0,  19, 73,
                                                  68,  65, 84,  120, 218, 99, 248, 207, 192, 240, 31,  12,  129, 52, 8,  52,  0,  0,  73,
                                                  73,  9,  120, 156, 81,  23, 146, 0,   0,   0,   0,   73,  69,  78, 68, 174, 66, 96, 130};
constexpr std::array<std::uint8_t, 16> PNG_2X2_RGBA = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128};

// An 8x8 JPEG of one color, (200, 100, 50), at quality 95 and without chroma subsampling.
constexpr std::array<std::uint8_t, 285> JPEG_8X8 = {
  255, 216, 255, 224, 0,   16,  74, 70, 73, 70, 0,  1,   1,   0,   0,   1,  0,  1,  0,   0,   255, 219, 0,   67, 0,  2,   1,   1,  1,
  1,   1,   2,   1,   1,   1,   2,  2,  2,  2,  2,  4,   3,   2,   2,   2,  2,  5,  4,   4,   3,   4,   6,   5,  6,  6,   6,   5,  6,
  6,   6,   7,   9,   8,   6,   7,  9,  7,  6,  6,  8,   11,  8,   9,   10, 10, 10, 10,  10,  6,   8,   11,  12, 11, 10,  12,  9,  10,
  10,  10,  255, 219, 0,   67,  1,  2,  2,  2,  2,  2,   2,   5,   3,   3,  5,  10, 7,   6,   7,   10,  10,  10, 10, 10,  10,  10, 10,
  10,  10,  10,  10,  10,  10,  10, 10, 10, 10, 10, 10,  10,  10,  10,  10, 10, 10, 10,  10,  10,  10,  10,  10, 10, 10,  10,  10, 10,
  10,  10,  10,  10,  10,  10,  10, 10, 10, 10, 10, 10,  10,  255, 192, 0,  17, 8,  0,   8,   0,   8,   3,   1,  17, 0,   2,   17, 1,
  3,   17,  1,   255, 196, 0,   20, 0,  1,  0,  0,  0,   0,   0,   0,   0,  0,  0,  0,   0,   0,   0,   0,   0,  5,  255, 196, 0,  20,
  16,  1,   0,   0,   0,   0,   0,  0,  0,  0,  0,  0,   0,   0,   0,   0,  0,  0,  255, 196, 0,   20,  1,   1,  0,  0,   0,   0,  0,
  0,   0,   0,   0,   0,   0,   0,  0,  0,  0,  8,  255, 196, 0,   20,  17, 1,  0,  0,   0,   0,   0,   0,   0,  0,  0,   0,   0,  0,
  0,   0,   0,   0,   255, 218, 0,  12, 3,  1,  0,  2,   17,  3,   17,  0,  63, 0,  60,  87,  54,  31,  255, 217};
constexpr std::array<int, 3> JPEG_8X8_RGB = {200, 100, 50};
// JPEG decoders round differently; this much is agreement.
constexpr int JPEG_TOLERANCE = 3;

} // namespace

TEST_CLASS(ImageCoding)
{
public:
  TEST_METHOD(DecodesAPngTopRowFirst)
  {
    Neuron::ImageFile image;
    std::string error;
    Assert::IsTrue(Neuron::ImageFile::Decode(std::as_bytes(std::span(PNG_2X2)), image, error), Widen(error).c_str());
    Assert::AreEqual(2u, image.WidthPixels());
    Assert::AreEqual(2u, image.HeightPixels());
    Assert::IsTrue(std::ranges::equal(image.Pixels(), std::as_bytes(std::span(PNG_2X2_RGBA))), L"the pixels differ");
  }

  TEST_METHOD(DecodesAJpeg)
  {
    Neuron::ImageFile image;
    std::string error;
    Assert::IsTrue(Neuron::ImageFile::Decode(std::as_bytes(std::span(JPEG_8X8)), image, error), Widen(error).c_str());
    Assert::AreEqual(8u, image.WidthPixels());
    Assert::AreEqual(8u, image.HeightPixels());
    const std::span<const std::byte> pixels = image.Pixels();
    for (std::size_t pixel = 0; pixel < pixels.size(); pixel += 4)
    {
      for (std::size_t channel = 0; channel < JPEG_8X8_RGB.size(); ++channel)
      {
        const int value = std::to_integer<int>(pixels[pixel + channel]);
        Assert::IsTrue(value >= JPEG_8X8_RGB[channel] - JPEG_TOLERANCE && value <= JPEG_8X8_RGB[channel] + JPEG_TOLERANCE,
                       L"a pixel is not the JPEG's color");
      }
      Assert::AreEqual(255, std::to_integer<int>(pixels[pixel + 3]));
    }
  }

  TEST_METHOD(EncodesAPngThatDecodesToTheSamePixels)
  {
    // 3x2, and every byte different, alpha included.
    std::array<std::uint8_t, 24> rgba{};
    for (std::size_t i = 0; i < rgba.size(); ++i)
    {
      rgba[i] = static_cast<std::uint8_t>(10 * i + 5);
    }
    std::vector<std::byte> file;
    std::string error;
    Assert::IsTrue(Neuron::ImageFile::EncodePng(3, 2, std::as_bytes(std::span(rgba)), file, error), Widen(error).c_str());
    Assert::IsTrue(file.size() > 8 && file[1] == std::byte{'P'} && file[2] == std::byte{'N'} && file[3] == std::byte{'G'},
                   L"the encoder did not write a PNG");

    Neuron::ImageFile image;
    Assert::IsTrue(Neuron::ImageFile::Decode(file, image, error), Widen(error).c_str());
    Assert::AreEqual(3u, image.WidthPixels());
    Assert::AreEqual(2u, image.HeightPixels());
    Assert::IsTrue(std::ranges::equal(image.Pixels(), std::as_bytes(std::span(rgba))), L"the pixels differ");
  }

  TEST_METHOD(RefusesBytesThatAreNoImage)
  {
    constexpr std::array<std::uint8_t, 4> NOT_AN_IMAGE = {1, 2, 3, 4};
    Neuron::ImageFile image;
    std::string error;
    Assert::IsFalse(Neuron::ImageFile::Decode(std::as_bytes(std::span(NOT_AN_IMAGE)), image, error));
    Assert::IsFalse(error.empty());
  }

  TEST_METHOD(RefusesPixelsOfTheWrongSize)
  {
    const std::array<std::uint8_t, 12> rgba{};
    std::vector<std::byte> file;
    std::string error;
    Assert::IsFalse(Neuron::ImageFile::EncodePng(2, 2, std::as_bytes(std::span(rgba)), file, error));
    Assert::IsFalse(error.empty());
  }
};

} // namespace NeuronClientTests
