// NeuronClient/FontFace.cpp
#include "pch.h"

#include <dwrite_1.h>

#include "FontFace.h"
#include "Unicode.h"

#include <array>
#include <cstddef>
#include <format>
#include <utility>

#pragma comment(lib, "dwrite.lib")

namespace Neuron
{

using Microsoft::WRL::ComPtr;

struct FontFace::Native
{
  ComPtr<IDWriteFactory> factory;
  ComPtr<IDWriteFontFace1> face;
  float emSizePixels = 0.0f;
  float pixelsPerDesignUnit = 0.0f;
};

namespace
{

constexpr std::size_t SUBPIXELS_PER_PIXEL = 3;

bool Succeeded(HRESULT _result, const char* _step, std::string& _error)
{
  if (SUCCEEDED(_result))
  {
    return true;
  }
  _error = std::format("DirectWrite: {} failed with 0x{:08X}", _step, static_cast<unsigned long>(_result));
  return false;
}

} // namespace

FontFace::FontFace() = default;
FontFace::~FontFace() = default;
FontFace::FontFace(FontFace&& _other) noexcept = default;
FontFace& FontFace::operator=(FontFace&& _other) noexcept = default;

bool FontFace::Open(std::string_view _pathUtf8, float _emSizePixels, FontFace& _outFace, std::string& _error)
{
  auto native = std::make_unique<Native>();
  const std::wstring path = Utf8ToUtf16(_pathUtf8);
  ComPtr<IDWriteFontFile> file;
  BOOL supported = FALSE;
  DWRITE_FONT_FILE_TYPE fileType = DWRITE_FONT_FILE_TYPE_UNKNOWN;
  DWRITE_FONT_FACE_TYPE faceType = DWRITE_FONT_FACE_TYPE_UNKNOWN;
  UINT32 faceCount = 0;
  // An isolated factory: faces come from their files, and the system's font cache is not involved.
  if (!Succeeded(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(native->factory.GetAddressOf())),
                 "creating the factory", _error) ||
      !Succeeded(native->factory->CreateFontFileReference(path.c_str(), nullptr, &file), "opening the file", _error) ||
      !Succeeded(file->Analyze(&supported, &fileType, &faceType, &faceCount), "reading the file", _error))
  {
    return false;
  }
  if (!supported || faceCount == 0)
  {
    _error = "DirectWrite: the file holds no face it can read";
    return false;
  }

  ComPtr<IDWriteFontFace> face;
  if (!Succeeded(native->factory->CreateFontFace(faceType, 1, file.GetAddressOf(), 0, DWRITE_FONT_SIMULATIONS_NONE, &face),
                 "creating the face", _error) ||
      !Succeeded(face.As(&native->face), "reaching IDWriteFontFace1", _error))
  {
    return false;
  }
  DWRITE_FONT_METRICS1 metrics{};
  native->face->GetMetrics(&metrics);
  native->emSizePixels = _emSizePixels;
  native->pixelsPerDesignUnit = _emSizePixels / static_cast<float>(metrics.designUnitsPerEm);
  _outFace.m_native = std::move(native);
  return true;
}

std::uint16_t FontFace::GlyphIndex(char32_t _codePoint) const
{
  if (!m_native)
  {
    return 0;
  }
  const UINT32 codePoint = _codePoint;
  UINT16 index = 0;
  if (FAILED(m_native->face->GetGlyphIndices(&codePoint, 1, &index)))
  {
    return 0;
  }
  return index;
}

bool FontFace::RenderGlyph(std::uint16_t _glyphIndex, GlyphBitmap& _outBitmap, std::string& _error) const
{
  if (!m_native)
  {
    _error = "DirectWrite: no face is open";
    return false;
  }
  const UINT16 index = _glyphIndex;
  DWRITE_GLYPH_METRICS glyphMetrics{};
  if (!Succeeded(m_native->face->GetDesignGlyphMetrics(&index, 1, &glyphMetrics, FALSE), "reading the glyph's metrics", _error))
  {
    return false;
  }

  // One glyph with its pen on the origin, so the bounds are measured from the baseline.
  const FLOAT advance = 0.0f;
  const DWRITE_GLYPH_OFFSET offset{};
  DWRITE_GLYPH_RUN run{};
  run.fontFace = m_native->face.Get();
  run.fontEmSize = m_native->emSizePixels;
  run.glyphCount = 1;
  run.glyphIndices = &index;
  run.glyphAdvances = &advance;
  run.glyphOffsets = &offset;
  // ClearType's 3x1 texture, averaged per pixel, is grayscale coverage on every Windows.
  ComPtr<IDWriteGlyphRunAnalysis> analysis;
  RECT bounds{};
  if (!Succeeded(m_native->factory->CreateGlyphRunAnalysis(&run, 1.0f, nullptr, DWRITE_RENDERING_MODE_CLEARTYPE_NATURAL_SYMMETRIC,
                                                           DWRITE_MEASURING_MODE_NATURAL, 0.0f, 0.0f, &analysis),
                 "analyzing the glyph", _error) ||
      !Succeeded(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds), "measuring the glyph", _error))
  {
    return false;
  }

  GlyphBitmap bitmap;
  bitmap.advancePixels = static_cast<float>(glyphMetrics.advanceWidth) * m_native->pixelsPerDesignUnit;
  if (bounds.right > bounds.left && bounds.bottom > bounds.top)
  {
    const auto widthPixels = static_cast<std::uint32_t>(bounds.right - bounds.left);
    const auto heightPixels = static_cast<std::uint32_t>(bounds.bottom - bounds.top);
    const std::size_t pixels = static_cast<std::size_t>(widthPixels) * heightPixels;
    std::vector<BYTE> subpixels(pixels * SUBPIXELS_PER_PIXEL);
    if (!Succeeded(
          analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1, &bounds, subpixels.data(), static_cast<UINT32>(subpixels.size())),
          "rendering the glyph", _error))
    {
      return false;
    }
    bitmap.widthPixels = widthPixels;
    bitmap.heightPixels = heightPixels;
    bitmap.leftPixels = bounds.left;
    bitmap.topPixels = -bounds.top;
    bitmap.coverage.resize(pixels);
    for (std::size_t pixel = 0; pixel < pixels; ++pixel)
    {
      const std::size_t first = pixel * SUBPIXELS_PER_PIXEL;
      const unsigned sum = subpixels[first] + subpixels[first + 1] + subpixels[first + 2];
      bitmap.coverage[pixel] = static_cast<std::uint8_t>((sum + 1) / SUBPIXELS_PER_PIXEL);
    }
  }
  _outBitmap = std::move(bitmap);
  return true;
}

float FontFace::KerningPixels(std::uint16_t _leftGlyph, std::uint16_t _rightGlyph) const
{
  if (!m_native || !m_native->face->HasKerningPairs())
  {
    return 0.0f;
  }
  const std::array<UINT16, 2> pair = {_leftGlyph, _rightGlyph};
  std::array<INT32, 2> adjustments = {};
  if (FAILED(m_native->face->GetKerningPairAdjustments(static_cast<UINT32>(pair.size()), pair.data(), adjustments.data())))
  {
    return 0.0f;
  }
  return static_cast<float>(adjustments[0]) * m_native->pixelsPerDesignUnit;
}

} // namespace Neuron
