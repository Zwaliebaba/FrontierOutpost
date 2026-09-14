// ContrastTests.cpp -- the palette, measured rather than eyeballed.
//
// **A colour that reads well on the author's monitor is not evidence.** Every text token on these
// screens is an alpha over a dark ground, and alpha is exactly where a palette drifts below legible
// without anyone noticing: the ink still looks like the ink, it is just fainter, and the person who
// chose it is the person least able to see that.
//
// So the floor is asserted. WCAG 2.1's contrast ratio, over the three grounds this game actually
// paints text on -- the app background, the dialog card, and a committed build tile's blue wash
// (ADR-107) -- at 4.5:1, which is the AA threshold for body text.
//
// **One thing is deliberately below it and is asserted to BE below it**, so that raising it is a
// decision rather than a drive-by: `TILE_BLOCKED_INK`, the ink of a build tile that is inert
// because something else on its system is rising. Its declaration in `DesignTokens.h` carries the
// reason, which is that the tile is not read.

#include "pch.h"
#include "CppUnitTest.h"

#include "DesignTokens.h"

#include <cmath>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

namespace
{

/// One channel of an sRGB colour, linearized. The curve is the specification's, not an approximation
/// of it: the whole point of this file is to be the arithmetic rather than an opinion about it.
[[nodiscard]] double Linear(double _channel) noexcept
{
  return _channel <= 0.03928 ? _channel / 12.92 : std::pow((_channel + 0.055) / 1.055, 2.4);
}

/// Named for the specification rather than for the idea, because `Neuron::Luminance` already exists
/// and means the 8-bit perceptual weight the palette baker uses. Two different questions.
[[nodiscard]] double RelativeLuminance(const Neuron::Color& _color) noexcept
{
  return 0.2126 * Linear(_color.red / 255.0) + 0.7152 * Linear(_color.green / 255.0) + 0.0722 * Linear(_color.blue / 255.0);
}

/// `_ink` composited over `_ground` at the ink's own alpha. Text is drawn with the renderer's
/// straight-alpha blend, so this is what lands in the framebuffer.
[[nodiscard]] Neuron::Color Over(const Neuron::Color& _ink, const Neuron::Color& _ground) noexcept
{
  const double alpha = _ink.alpha / 255.0;
  const auto mix = [alpha](std::uint8_t _over, std::uint8_t _under)
  { return static_cast<std::uint8_t>(std::lround(_under + (static_cast<double>(_over) - _under) * alpha)); };
  return Neuron::Color{mix(_ink.red, _ground.red), mix(_ink.green, _ground.green), mix(_ink.blue, _ground.blue), 255};
}

[[nodiscard]] double Contrast(const Neuron::Color& _ink, const Neuron::Color& _ground) noexcept
{
  const double a = RelativeLuminance(Over(_ink, _ground));
  const double b = RelativeLuminance(_ground);
  const double lighter = a > b ? a : b;
  const double darker = a > b ? b : a;
  return (lighter + 0.05) / (darker + 0.05);
}

/// The AA floor for body text. Every one of these strings is body text: there is no large-text
/// exemption to claim while the whole screen is one size (ADR-014, and UI-01 2.1 is the second).
constexpr double FLOOR = 4.5;

void AssertReadable(const wchar_t* _name, const Neuron::Color& _ink)
{
  for (const auto& [ground, where] :
       {std::pair{Lockstep::Ink::APP_BACKGROUND, L"the app background"}, std::pair{Lockstep::Ink::DIALOG_FILL, L"a dialog card"}})
  {
    const double ratio = Contrast(_ink, ground);
    if (ratio < FLOOR)
    {
      Assert::Fail((std::wstring{_name} + L" is " + std::to_wstring(ratio) + L":1 over " + where + L", under the 4.5:1 floor").c_str());
    }
  }
}

} // namespace

TEST_CLASS(ContrastTests)
{
public:
  TEST_METHOD(EveryTextTokenClearsTheFloor)
  {
    // The four that carry sentences and labels. `TEXT_PRIMARY` has never been in doubt and is here
    // so that the list is the palette rather than the subset somebody worried about.
    AssertReadable(L"TEXT_PRIMARY", Lockstep::Ink::TEXT_PRIMARY);
    AssertReadable(L"TEXT_DETAIL", Lockstep::Ink::TEXT_DETAIL);
    AssertReadable(L"TEXT_MUTED", Lockstep::Ink::TEXT_MUTED);
    AssertReadable(L"NEUTRAL_DIM", Lockstep::Ink::NEUTRAL_DIM);
  }

  TEST_METHOD(EveryMeaningColorClearsTheFloor)
  {
    // Blue, amber and red carry text as often as they carry a border -- a verdict line, a countdown,
    // a concede row -- so they are held to the same floor as the greys.
    AssertReadable(L"BLUE", Lockstep::Ink::BLUE);
    AssertReadable(L"AMBER", Lockstep::Ink::AMBER);
    AssertReadable(L"RED", Lockstep::Ink::RED);
    AssertReadable(L"PURPLE", Lockstep::Ink::PURPLE);
  }

  // A build tile the player has committed to is filled with `BLUE` at 15/255, and four strings are
  // drawn ON that fill (ADR-107). It is the only place on the main page where text sits over
  // anything but the ink, so it is the only ground the two tests above do not already cover.
  TEST_METHOD(EveryInkOnACommittedTileClearsTheFloorOverItsWash)
  {
    const Neuron::Color wash = Over(Lockstep::Ink::TILE_COMMITTED_FILL, Lockstep::Ink::APP_BACKGROUND);

    for (const auto& [ink, name] :
         {std::pair{Lockstep::Ink::TEXT_PRIMARY, L"TEXT_PRIMARY"}, std::pair{Lockstep::Ink::TEXT_DETAIL, L"TEXT_DETAIL"},
          std::pair{Lockstep::Ink::TEXT_MUTED, L"TEXT_MUTED"}, std::pair{Lockstep::Ink::NEUTRAL_DIM, L"NEUTRAL_DIM"},
          std::pair{Lockstep::Ink::BLUE, L"BLUE"}})
    {
      const double ratio = Contrast(ink, wash);
      if (ratio < FLOOR)
      {
        Assert::Fail(
          (std::wstring{name} + L" is " + std::to_wstring(ratio) + L":1 over a committed tile's wash, under the 4.5:1 floor").c_str());
      }
    }
  }

  // **The one exemption, asserted as one.** A blocked tile is drawn at 90/255 white and is meant to
  // be: the sheet's help line above it says why nothing there can be ordered, and an ink that
  // cleared the floor would put three inert tiles in competition with the build that is actually
  // happening (ADR-107). If somebody raises it, this fails and they read the declaration.
  TEST_METHOD(TheBlockedTilesInkIsBelowTheFloorOnPurpose)
  {
    Assert::IsTrue(Contrast(Lockstep::Ink::TILE_BLOCKED_INK, Lockstep::Ink::APP_BACKGROUND) < FLOOR,
                   L"the blocked tile's ink now clears the floor, so the exemption in DesignTokens.h is stale");
    Assert::IsTrue(Contrast(Lockstep::Ink::TILE_BLOCKED_INK, Lockstep::Ink::APP_BACKGROUND) <
                     Contrast(Lockstep::Ink::NEUTRAL_DIM, Lockstep::Ink::APP_BACKGROUND),
                   L"the blocked tile's ink is no fainter than the dim one, so one of the two is pointless");
  }

  TEST_METHOD(TheDimmestTokenIsTheOneThatDefinesTheFloor)
  {
    // A guard against the floor being cleared by raising everything to white. `NEUTRAL_DIM` is the
    // faintest thing on the screen by design -- an inert control, a row that is not a target -- and
    // it must stay distinguishable from the ink beside it while clearing 4.5:1.
    Assert::IsTrue(Lockstep::Ink::NEUTRAL_DIM.alpha < Lockstep::Ink::TEXT_MUTED.alpha,
                   L"the dim token is no longer dimmer than the muted one, so one of them is pointless");
    Assert::IsTrue(Lockstep::Ink::TEXT_MUTED.alpha <= Lockstep::Ink::TEXT_PRIMARY.alpha);
  }
};

} // namespace LockstepTests
