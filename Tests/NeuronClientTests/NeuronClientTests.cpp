#include "pch.h"
#include "CppUnitTest.h"

// NeuronClient.h, not NeuronCore.h: it is the umbrella the library's own translation units
// compile against, so a suite that includes anything else is testing a header in a configuration
// nothing else ever builds it in.
#include "NeuronClient.h"

#include "FontRenderer.h"
#include "Palette.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

// The palette is a wire format in all but name: sixteen numbers that every pixel on the screen
// goes through, decided once by the owner (Design/Plans MVP-01 section 2) and never negotiated
// again. A typo in one of them is not something anybody would spot by looking at the screen --
// dark blue and a slightly different dark blue look alike -- so all sixteen are pinned here by
// value rather than spot-checked.
TEST_CLASS(PaletteTests)
{
public:
  TEST_METHOD(HasSixteenEntries)
  {
    Assert::AreEqual(static_cast<size_t>(16), Neuron::EGA_PALETTE.size());
    Assert::AreEqual(static_cast<size_t>(Neuron::PALETTE_SIZE), Neuron::EGA_PALETTE.size());
  }

  TEST_METHOD(IsTheEgaDefaultSixteen)
  {
    constexpr std::array<std::uint32_t, 16> EXPECTED = {
      0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
      0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
    };

    for (std::size_t index = 0; index < EXPECTED.size(); ++index)
    {
      Assert::AreEqual(EXPECTED[index], Neuron::EGA_PALETTE[index],
                       (std::wstring(L"palette entry ") + std::to_wstring(index) + L" is not the EGA default").c_str());
    }
  }

  TEST_METHOD(HasNoHighBitsSet)
  {
    // 0x00RRGGBB, not 0xAARRGGBB. The resolve shader writes opaque alpha itself; an alpha byte
    // smuggled into the table would come out as a wrong red channel after the >> 16.
    for (const std::uint32_t entry : Neuron::EGA_PALETTE)
    {
      Assert::AreEqual(0u, entry >> 24, L"palette entries are 0x00RRGGBB");
    }
  }

  // ADR-002 shades a face between palette index n and index n+8. That only works because the
  // bottom eight entries and the top eight are the dark and bright halves of the same eight hues,
  // which is a property of this specific table -- so it is worth asserting rather than assuming.
  TEST_METHOD(BrightHalfIsBrighterThanDarkHalf)
  {
    auto luminance = [](std::uint32_t _packed) { return ((_packed >> 16) & 0xFFu) + ((_packed >> 8) & 0xFFu) + (_packed & 0xFFu); };

    for (std::size_t dark = 0; dark < 8; ++dark)
    {
      Assert::IsTrue(
        luminance(Neuron::EGA_PALETTE[dark + 8]) > luminance(Neuron::EGA_PALETTE[dark]),
        (std::wstring(L"index ") + std::to_wstring(dark + 8) + L" should be brighter than index " + std::to_wstring(dark)).c_str());
    }
  }

  TEST_METHOD(NamedIndicesMatchTheTable)
  {
    Assert::AreEqual(0x000000u, Neuron::EGA_PALETTE[Neuron::ToIndex(Neuron::PaletteIndex::Black)]);
    Assert::AreEqual(0x0000AAu, Neuron::EGA_PALETTE[Neuron::ToIndex(Neuron::PaletteIndex::Blue)]);
    Assert::AreEqual(0xFFFFFFu, Neuron::EGA_PALETTE[Neuron::ToIndex(Neuron::PaletteIndex::White)]);
    Assert::AreEqual(0xAA5500u, Neuron::EGA_PALETTE[Neuron::ToIndex(Neuron::PaletteIndex::Brown)]);
  }
};

// The atlas FontRenderer uploads is built from GlyphRow, so these assertions are assertions about
// what ends up on the screen. They are worth having because the two things that would break the
// font are both silent: an off-by-one in the character-to-glyph mapping shifts the whole alphabet
// by one and still draws letters, and a bit order flipped left-to-right still draws something
// glyph-shaped.
TEST_CLASS(FontTests)
{
public:
  TEST_METHOD(TableIsNinetySixGlyphsOfEightBytes)
  {
    Assert::AreEqual(static_cast<size_t>(768), FONT_DATA.size());
    Assert::AreEqual(static_cast<size_t>(768),
                     static_cast<size_t>(Neuron::FontRenderer::GLYPH_COUNT) * Neuron::FontRenderer::GLYPH_HEIGHT_TEXELS);
  }

  TEST_METHOD(SpaceIsTheFirstGlyph)
  {
    Assert::AreEqual(0u, Neuron::FontRenderer::GlyphIndex(' '));
    for (std::uint32_t row = 0; row < Neuron::FontRenderer::GLYPH_HEIGHT_TEXELS; ++row)
    {
      Assert::AreEqual(static_cast<std::uint8_t>(0), Neuron::FontRenderer::GlyphRow(' ', row), L"a space has no lit pixels");
    }
  }

  // 'A' is 0x41, so it is glyph 33 and its eight bytes start at offset 264. Written out as bits
  // they are the letter, which is the point of pinning them:
  //
  //     ..####..   0x3C
  //     .##..##.   0x66
  //     .##..##.   0x66
  //     .######.   0x7E
  //     .##..##.   0x66
  //     .##..##.   0x66
  //     .##..##.   0x66
  //     ........   0x00
  TEST_METHOD(CapitalAIsTheExpectedEightBytes)
  {
    Assert::AreEqual(33u, Neuron::FontRenderer::GlyphIndex('A'));

    constexpr std::array<std::uint8_t, 8> EXPECTED = {0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00};
    for (std::uint32_t row = 0; row < EXPECTED.size(); ++row)
    {
      Assert::AreEqual(EXPECTED[row], Neuron::FontRenderer::GlyphRow('A', row),
                       (std::wstring(L"row ") + std::to_wstring(row) + L" of 'A'").c_str());
    }
  }

  // The atlas builder reads bit (7 - column), so the most significant bit is the leftmost pixel.
  // Row 0 of 'A' is 0x3C: columns 2..5 lit, 0, 1, 6 and 7 dark.
  TEST_METHOD(MostSignificantBitIsTheLeftmostPixel)
  {
    const std::uint8_t topRow = Neuron::FontRenderer::GlyphRow('A', 0);
    constexpr std::array<bool, 8> EXPECTED_LIT = {false, false, true, true, true, true, false, false};

    for (std::uint32_t column = 0; column < EXPECTED_LIT.size(); ++column)
    {
      const bool lit = ((topRow >> (Neuron::FontRenderer::GLYPH_WIDTH_TEXELS - 1 - column)) & 1U) != 0;
      Assert::AreEqual(EXPECTED_LIT[column], lit, (std::wstring(L"column ") + std::to_wstring(column) + L" of the top row of 'A'").c_str());
    }
  }

  TEST_METHOD(CharactersOutsideTheTableDrawAsSpace)
  {
    Assert::AreEqual(0u, Neuron::FontRenderer::GlyphIndex('\0'));
    Assert::AreEqual(0u, Neuron::FontRenderer::GlyphIndex('\n'));
    Assert::AreEqual(0u, Neuron::FontRenderer::GlyphIndex(static_cast<char>(31)), L"one below the first glyph");
    Assert::AreEqual(0u, Neuron::FontRenderer::GlyphIndex(static_cast<char>(0x80)), L"a high byte must not index past the table");
    Assert::AreEqual(0u, Neuron::FontRenderer::GlyphIndex(static_cast<char>(0xE9)), L"nor a UTF-8 continuation byte");
  }

  // The table runs from space to 0x7F inclusive: 96 glyphs. '~' is 0x7E and therefore glyph 94,
  // and the 96th slot -- where DEL would be -- holds the solid block Font.h ends with. Both are
  // in range, which is the boundary worth pinning: an off-by-one here reads past a 768-byte array.
  TEST_METHOD(LastGlyphsAreInRange)
  {
    Assert::AreEqual(94u, Neuron::FontRenderer::GlyphIndex('~'));
    Assert::AreEqual(Neuron::FontRenderer::GLYPH_COUNT - 1, Neuron::FontRenderer::GlyphIndex(static_cast<char>(0x7F)));

    for (std::uint32_t row = 0; row < Neuron::FontRenderer::GLYPH_HEIGHT_TEXELS; ++row)
    {
      Assert::AreEqual(static_cast<std::uint8_t>(0xFF), Neuron::FontRenderer::GlyphRow(static_cast<char>(0x7F), row),
                       L"the last glyph is a solid 8x8 block");
    }
  }
};

} // namespace NeuronClientTests
