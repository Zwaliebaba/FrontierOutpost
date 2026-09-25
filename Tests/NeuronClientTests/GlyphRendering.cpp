// Tests/NeuronClientTests/GlyphRendering.cpp
//
// Neuron::FontFace renders a game font's glyphs as coverage with their metrics, and reads kerning
// from a kern table (Design/ADR/ADR-010). The game's own fonts have no kern table, so the kerning
// test uses Windows' Arial, which has one.
#include "pch.h"

#include "Check.h"
#include "FontFace.h"
#include "Repository.h"
#include "Unicode.h"

#include <algorithm>
#include <filesystem>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

constexpr float EM_SIZE_PIXELS = 64.0f;

std::filesystem::path WindowsFonts()
{
  std::wstring windows(MAX_PATH, L'\0');
  const UINT length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
  windows.resize(length);
  return std::filesystem::path(windows) / L"Fonts";
}

Neuron::FontFace Open(const std::filesystem::path& _path)
{
  Neuron::FontFace face;
  std::string error;
  Assert::IsTrue(Neuron::FontFace::Open(Neuron::Utf16ToUtf8(_path.wstring()), EM_SIZE_PIXELS, face, error), Widen(error).c_str());
  return face;
}

Neuron::FontFace GameFont()
{
  return Open(Repository() / L"GameData" / L"font" / L"Rajdhani" / L"Medium.ttf");
}

Neuron::GlyphBitmap Render(const Neuron::FontFace& _face, char32_t _codePoint)
{
  const std::uint16_t glyph = _face.GlyphIndex(_codePoint);
  Assert::AreNotEqual(0, static_cast<int>(glyph), L"the face has no glyph for it");
  Neuron::GlyphBitmap bitmap;
  std::string error;
  Assert::IsTrue(_face.RenderGlyph(glyph, bitmap, error), Widen(error).c_str());
  return bitmap;
}

} // namespace

TEST_CLASS(GlyphRendering)
{
public:
  TEST_METHOD(RendersCoverageWithTheGlyphsMetrics)
  {
    const Neuron::GlyphBitmap a = Render(GameFont(), U'A');
    Assert::IsTrue(a.widthPixels > 15 && a.widthPixels < 64, L"A's width is not plausible at 64 px");
    Assert::IsTrue(a.heightPixels > 30 && a.heightPixels < 64, L"A's height is not plausible at 64 px");
    Assert::AreEqual(static_cast<std::size_t>(a.widthPixels) * a.heightPixels, a.coverage.size());
    Assert::AreEqual(0, static_cast<int>(*std::ranges::min_element(a.coverage)), L"no pixel is empty");
    Assert::AreEqual(255, static_cast<int>(*std::ranges::max_element(a.coverage)), L"no pixel is fully covered");
    // A sits on the baseline, and the pen moves on by about its width.
    Assert::IsTrue(a.topPixels > 30 && a.topPixels <= static_cast<int>(a.heightPixels) + 1, L"A does not sit on the baseline");
    Assert::IsTrue(a.leftPixels > -8 && a.leftPixels < 16, L"A's left bearing is not plausible");
    Assert::IsTrue(a.advancePixels > 15.0f && a.advancePixels < 64.0f, L"A's advance is not plausible");

    // g reaches below the baseline.
    const Neuron::GlyphBitmap g = Render(GameFont(), U'g');
    Assert::IsTrue(g.topPixels > 0 && g.topPixels < static_cast<int>(g.heightPixels), L"g does not reach below the baseline");
  }

  TEST_METHOD(RendersNothingForASpace)
  {
    const Neuron::GlyphBitmap space = Render(GameFont(), U' ');
    Assert::AreEqual(0u, space.widthPixels);
    Assert::AreEqual(0u, space.heightPixels);
    Assert::IsTrue(space.coverage.empty(), L"a space has coverage");
    Assert::IsTrue(space.advancePixels > 0.0f, L"a space does not advance");
  }

  TEST_METHOD(KnowsWhichCodePointsItLacks)
  {
    Assert::AreEqual(0, static_cast<int>(GameFont().GlyphIndex(U'\u4E16')), L"Rajdhani has a CJK glyph");
  }

  TEST_METHOD(ReadsKerningFromTheKernTable)
  {
    const Neuron::FontFace arial = Open(WindowsFonts() / L"arial.ttf");
    Assert::IsTrue(arial.KerningPixels(arial.GlyphIndex(U'A'), arial.GlyphIndex(U'V')) < 0.0f, L"A and V do not close up");
    Assert::AreEqual(0.0f, arial.KerningPixels(arial.GlyphIndex(U'H'), arial.GlyphIndex(U'H')));
  }

  TEST_METHOD(FindsNoKerningInTheGamesFonts)
  {
    // Rajdhani kerns through GPOS only, which neither FreeType's FT_Get_Kerning nor this reads.
    const Neuron::FontFace face = GameFont();
    Assert::AreEqual(0.0f, face.KerningPixels(face.GlyphIndex(U'A'), face.GlyphIndex(U'V')));
  }

  TEST_METHOD(RefusesAFileThatIsNotThere)
  {
    Neuron::FontFace face;
    std::string error;
    Assert::IsFalse(Neuron::FontFace::Open(Neuron::Utf16ToUtf8((Repository() / L"NoSuchFont.ttf").wstring()), EM_SIZE_PIXELS, face, error));
    Assert::IsFalse(error.empty());
  }
};

} // namespace NeuronClientTests
