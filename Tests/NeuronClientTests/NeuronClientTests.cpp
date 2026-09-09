#include "pch.h"
#include "CppUnitTest.h"

// NeuronClient.h, not NeuronCore.h: it is the umbrella the library's own translation units
// compile against, so a suite that includes anything else is testing a header in a configuration
// nothing else ever builds it in.
#include "NeuronClient.h"

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

} // namespace NeuronClientTests
