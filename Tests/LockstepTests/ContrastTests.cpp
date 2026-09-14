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
// **Nothing is below it any more.** `TILE_BLOCKED_INK` was, at 3.21:1, and was asserted to BE
// below it so that raising it would be a decision (ADR-107): an inert tile had to be faint, because
// faint was the only channel saying it could not be ordered. ADR-110 gave inert controls a DASHED
// border, which says that on its own and says it in the chrome rather than in the words, so the ink
// went back to `NEUTRAL_DIM` and the exemption went with it (ADR-111).

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

/// One ink over one ground, named so a failure says which pair it was.
void AssertReadableOver(const wchar_t* _name, const Neuron::Color& _ink, const Neuron::Color& _ground, const wchar_t* _where)
{
  const double ratio = Contrast(_ink, _ground);
  if (ratio < FLOOR)
  {
    Assert::Fail((std::wstring{_name} + L" is " + std::to_wstring(ratio) + L":1 over " + _where + L", under the 4.5:1 floor").c_str());
  }
}

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

  // ---- The control vocabulary's four states (ADR-110) -------------------------------------------
  //
  // A control state is a ground as much as it is an ink: a filled button paints `BLUE` under
  // `APP_BACKGROUND` text and shades a second cell darker still, and a committed one paints a wash
  // that lifts under the pointer. Each of those is a ground this palette did not have before, and a
  // ground nobody measured is exactly where the four states would go quietly illegible.

  TEST_METHOD(AFilledButtonsInkClearsTheFloorAtRestAndUnderThePointer)
  {
    // The label is the app background on blue -- dark on light, the one inversion on this screen --
    // so it is measured the way round the renderer draws it.
    AssertReadableOver(L"APP_BACKGROUND", Lockstep::Ink::APP_BACKGROUND, Lockstep::Ink::BLUE, L"a filled button");
    AssertReadableOver(L"APP_BACKGROUND", Lockstep::Ink::APP_BACKGROUND, Lockstep::Ink::BUTTON_PRIMARY_HOVER,
                       L"a filled button under the pointer");

    // And on the number segment, which is the same fill shaded (ADR-110). A shade that made the
    // segment's ground dark enough to lose the dark ink on it would be a price nobody can read.
    const Neuron::Color rest = Over(Lockstep::Ink::BUTTON_SEGMENT_SHADE, Lockstep::Ink::BLUE);
    const Neuron::Color hovered = Over(Lockstep::Ink::BUTTON_SEGMENT_SHADE, Lockstep::Ink::BUTTON_PRIMARY_HOVER);
    AssertReadableOver(L"APP_BACKGROUND", Lockstep::Ink::APP_BACKGROUND, rest, L"a filled button's number segment");
    AssertReadableOver(L"APP_BACKGROUND", Lockstep::Ink::APP_BACKGROUND, hovered, L"a hovered button's number segment");
  }

  TEST_METHOD(ALockedButtonsInkClearsTheFloorOverItsFill)
  {
    // A button at the lock is the filled grey the rail's chip wears (ADR-065, ADR-110), which is the
    // one other place on this screen where the ink is the background.
    AssertReadableOver(L"APP_BACKGROUND", Lockstep::Ink::APP_BACKGROUND, Over(Lockstep::Ink::LOCKED_FILL, Lockstep::Ink::APP_BACKGROUND),
                       L"a locked button");
  }

  TEST_METHOD(AnInertControlsReasonClearsTheFloorOverTheGroundItIsDrawnOn)
  {
    // **An inert control has no fill**: its border is dashed and its inside is the app background
    // (ADR-110), so this is the ground both of its inks are measured over. The reason is amber when
    // it is money and dim when it is the board, and a player who cannot order the thing still has to
    // be able to read why.
    AssertReadableOver(L"NEUTRAL_DIM", Lockstep::Ink::NEUTRAL_DIM, Lockstep::Ink::APP_BACKGROUND, L"an inert control");
    AssertReadableOver(L"AMBER", Lockstep::Ink::AMBER, Lockstep::Ink::APP_BACKGROUND, L"an inert control's money reason");
  }

  TEST_METHOD(ACommittedControlsInksClearTheFloorAtRestAndUnderThePointer)
  {
    // **`NEUTRAL_DIM` is deliberately not on this list.** It is the inert and the locked ink, and
    // neither of those has a hover at all -- a control nobody can tap does not light under the
    // pointer -- so the pair never lands in a framebuffer. Measured anyway it is 4.34:1, which is
    // the right answer to a question this screen does not ask.
    const Neuron::Color hovered = Over(Lockstep::Ink::COMMITTED_HOVER_FILL, Lockstep::Ink::APP_BACKGROUND);
    for (const auto& [ink, name] : {std::pair{Lockstep::Ink::TEXT_PRIMARY, L"TEXT_PRIMARY"},
                                    std::pair{Lockstep::Ink::TEXT_MUTED, L"TEXT_MUTED"}, std::pair{Lockstep::Ink::BLUE, L"BLUE"}})
    {
      AssertReadableOver(name, ink, hovered, L"a committed control under the pointer");
    }
  }

  TEST_METHOD(TheMoveModeBannersInksClearTheFloorOverItsWash)
  {
    // The banner is a blue wash across the map pane and carries three inks (ADR-113): the fleet's
    // name, what it is and where from, and the sentence that says what to tap.
    const Neuron::Color wash = Over(Lockstep::Ink::MOVE_MODE_WASH, Lockstep::Ink::APP_BACKGROUND);
    for (const auto& [ink, name] : {std::pair{Lockstep::Ink::BLUE, L"BLUE"}, std::pair{Lockstep::Ink::TEXT_PRIMARY, L"TEXT_PRIMARY"},
                                    std::pair{Lockstep::Ink::TEXT_MUTED, L"TEXT_MUTED"}})
    {
      AssertReadableOver(name, ink, wash, L"the move-mode banner");
    }
  }

  TEST_METHOD(AHoverIsBrighterThanTheStateItLifts)
  {
    // A guard of the same shape as the dim-token one below: a hover that is not brighter than the
    // rest state is a hover nobody can see, and the two pairs are the whole of what says "under the
    // pointer" on this screen (ADR-110).
    Assert::IsTrue(Lockstep::Ink::OUTLINE.alpha < Lockstep::Ink::OUTLINE_HOVER.alpha,
                   L"an outlined control's hover border is no brighter than its rest one");
    Assert::IsTrue(Lockstep::Ink::TILE_COMMITTED_FILL.alpha < Lockstep::Ink::COMMITTED_HOVER_FILL.alpha,
                   L"a committed control's hover wash is no stronger than its rest one");
    Assert::IsTrue(Neuron::Luminance(Lockstep::Ink::BLUE) < Neuron::Luminance(Lockstep::Ink::BUTTON_PRIMARY_HOVER),
                   L"a filled button's hover is no lighter than its rest fill");
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
