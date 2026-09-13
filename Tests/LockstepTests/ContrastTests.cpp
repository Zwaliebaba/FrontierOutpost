// ContrastTests.cpp -- the palette, measured rather than eyeballed.
//
// **A colour that reads well on the author's monitor is not evidence.** Every text token on these
// screens is an alpha over a dark ground, and alpha is exactly where a palette drifts below legible
// without anyone noticing: the ink still looks like the ink, it is just fainter, and the person who
// chose it is the person least able to see that.
//
// So the floor is asserted. WCAG 2.1's contrast ratio, over the two grounds this game actually
// paints text on -- the app background and the dialog card -- at 4.5:1, which is the AA threshold
// for body text. Anything the design deliberately puts below it says so here by being excluded, and
// there is nothing excluded today.

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
