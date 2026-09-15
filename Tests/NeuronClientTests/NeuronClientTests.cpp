// NeuronClientTests.cpp -- the canvas: its named colours, its one size, and the arithmetic that puts
// it on a surface at a whole-number scale (ADR-011, ADR-075).
//
// The rest of the library is one suite file per subject beside this one: `FontTests`, `RendererTests`,
// `PointerInputTests`, `GestureTests`, `OrbitCameraTests`, `TextFieldTests`, `StarfieldTests`.

#include "pch.h"
#include "CppUnitTest.h"

#include <algorithm>
#include <array>
#include <cmath>

// NeuronClient.h, not NeuronCore.h: it is the umbrella the library's own translation units
// compile against, so a suite that includes anything else is testing a header in a configuration
// nothing else ever builds it in.
#include "NeuronClient.h"

#include "Color.h"
#include "FontRenderer.h"
#include "MeshRenderer.h"
#include "OrbitCamera.h"
#include "PointerInput.h"
#include "Presentation.h"
#include "SceneTarget.h"
#include "ShapeRenderer.h"
#include "Starfield.h"
#include "TextField.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

// The named colors are a wire format in all but name: the numbers every pixel on the screen is
// one of. A typo in one of them is not something anybody would spot by looking at the screen --
// dark blue and a slightly different dark blue look alike -- so they are pinned here by value
// rather than spot-checked.
//
// Pack() is pinned harder than any of them, because the one bug this whole scheme can have is a
// byte order that disagrees with DXGI_FORMAT_R8G8B8A8_UNORM: red and blue swapped is a picture
// that is entirely plausible and entirely wrong (ADR-011).
TEST_CLASS(ColorTests)
{
public:
  TEST_METHOD(PackPutsRedInTheLowByte)
  {
    // R8G8B8A8_UNORM reads the four bytes of a vertex attribute in memory order, and x86 is
    // little-endian, so red has to be the LOW byte of the uint32 -- not the high one a person
    // writing 0xRRGGBB by hand would produce.
    constexpr Neuron::Color SAMPLE = {0x12, 0x34, 0x56, 0x78};
    Assert::AreEqual(0x78563412u, Neuron::Pack(SAMPLE));

    Assert::AreEqual(0xFF0000FFu, Neuron::Pack(Neuron::BRIGHT_RED) & 0xFF0000FFu, L"alpha high, red low");
    Assert::AreEqual(0xFFFFFFFFu, Neuron::Pack(Neuron::WHITE));
    Assert::AreEqual(0xFF000000u, Neuron::Pack(Neuron::BLACK));
  }

  TEST_METHOD(TheNamedColorsAreTheValuesTheyHaveAlwaysBeen)
  {
    // The EGA default 16 the game was drawn against, listed here as the RGB triples they have
    // been since 2026-09-09. ADR-011 widened the format; it did not repaint anything.
    struct Expected
    {
      Neuron::Color color;
      std::uint8_t red;
      std::uint8_t green;
      std::uint8_t blue;
    };

    constexpr std::array<Expected, 16> EXPECTED = {{
      {Neuron::BLACK, 0x00, 0x00, 0x00},
      {Neuron::BLUE, 0x00, 0x00, 0xAA},
      {Neuron::GREEN, 0x00, 0xAA, 0x00},
      {Neuron::CYAN, 0x00, 0xAA, 0xAA},
      {Neuron::RED, 0xAA, 0x00, 0x00},
      {Neuron::MAGENTA, 0xAA, 0x00, 0xAA},
      {Neuron::BROWN, 0xAA, 0x55, 0x00},
      {Neuron::LIGHT_GRAY, 0xAA, 0xAA, 0xAA},
      {Neuron::DARK_GRAY, 0x55, 0x55, 0x55},
      {Neuron::BRIGHT_BLUE, 0x55, 0x55, 0xFF},
      {Neuron::BRIGHT_GREEN, 0x55, 0xFF, 0x55},
      {Neuron::BRIGHT_CYAN, 0x55, 0xFF, 0xFF},
      {Neuron::BRIGHT_RED, 0xFF, 0x55, 0x55},
      {Neuron::BRIGHT_MAGENTA, 0xFF, 0x55, 0xFF},
      {Neuron::YELLOW, 0xFF, 0xFF, 0x55},
      {Neuron::WHITE, 0xFF, 0xFF, 0xFF},
    }};

    for (std::size_t index = 0; index < EXPECTED.size(); ++index)
    {
      const std::wstring which = std::wstring(L"named color ") + std::to_wstring(index);
      Assert::AreEqual(EXPECTED[index].red, EXPECTED[index].color.red, which.c_str());
      Assert::AreEqual(EXPECTED[index].green, EXPECTED[index].color.green, which.c_str());
      Assert::AreEqual(EXPECTED[index].blue, EXPECTED[index].color.blue, which.c_str());
      Assert::AreEqual(Neuron::OPAQUE_ALPHA, EXPECTED[index].color.alpha, L"every named color is opaque");
    }
  }

  // ADR-012 shades a face between two authored tones. Nothing in the type system says which of a
  // ColorPair is which, so the pairs the game actually uses are checked here -- the same claim
  // PaletteTests::BrightHalfIsBrighterThanDarkHalf used to make about indices n and n+8.
  TEST_METHOD(TheBrightHalfOfEachHueIsBrighter)
  {
    constexpr std::array<Neuron::ColorPair, 8> PAIRS = {{
      {Neuron::BLACK, Neuron::DARK_GRAY},
      {Neuron::BLUE, Neuron::BRIGHT_BLUE},
      {Neuron::GREEN, Neuron::BRIGHT_GREEN},
      {Neuron::CYAN, Neuron::BRIGHT_CYAN},
      {Neuron::RED, Neuron::BRIGHT_RED},
      {Neuron::MAGENTA, Neuron::BRIGHT_MAGENTA},
      {Neuron::BROWN, Neuron::YELLOW},
      {Neuron::LIGHT_GRAY, Neuron::WHITE},
    }};

    for (std::size_t index = 0; index < PAIRS.size(); ++index)
    {
      Assert::IsTrue(Neuron::Luminance(PAIRS[index].lit) > Neuron::Luminance(PAIRS[index].shaded),
                     (std::wstring(L"pair ") + std::to_wstring(index) + L" is the wrong way round").c_str());
    }
  }

  // `Mix` is how a station's dark tone is made from its lit one (ADR-103), and it is the one place
  // a third colour is ever computed from two -- so what it computes is pinned by value.
  TEST_METHOD(MixWalksEveryChannelFromOneColorToTheOther)
  {
    constexpr Neuron::Color FROM = {200, 100, 0, 255};
    constexpr Neuron::Color TO = {0, 100, 200, 55};

    const Neuron::Color start = Neuron::Mix(FROM, TO, 0.0F);
    Assert::IsTrue(start.red == 200 && start.green == 100 && start.blue == 0 && start.alpha == 255,
                   L"nought of the way is the first colour");

    const Neuron::Color end = Neuron::Mix(FROM, TO, 1.0F);
    Assert::IsTrue(end.red == 0 && end.green == 100 && end.blue == 200 && end.alpha == 55, L"all of the way is the second");

    const Neuron::Color half = Neuron::Mix(FROM, TO, 0.5F);
    Assert::IsTrue(half.red == 100 && half.green == 100 && half.blue == 100 && half.alpha == 155, L"halfway is halfway on every channel");

    const Neuron::Color past = Neuron::Mix(FROM, TO, 3.0F);
    Assert::IsTrue(past.red == 0 && past.blue == 200, L"past the end is clamped rather than extrapolated");
  }
};

// The screen, which is now one number rather than a virtual resolution and a scale factor
// (ADR-011). It is asserted here because everything else in this suite is arithmetic against it.
TEST_CLASS(SceneTargetTests)
{
public:
  TEST_METHOD(TheScreenIsTwelveEightyBySevenTwenty)
  {
    Assert::AreEqual(1280u, Neuron::SceneTarget::WIDTH_PIXELS);
    Assert::AreEqual(720u, Neuron::SceneTarget::HEIGHT_PIXELS);
  }
};

// Presentation is the arithmetic that puts a 1280x720 canvas on a surface of some other size, and
// it is the one piece of ADR-075 that a second platform reuses with no edit at all -- so it is
// also the piece worth testing away from a device, which is what these do. There is no D3D12
// here and no window: the type is five integers and two functions over them.
//
// The numbers below are the three cases that actually occur. Scale 1 in a window that IS the
// canvas is the desktop today; scale 2 in 2560x1440 is a 4K monitor at 200%; 1920x1080 at scale 1
// is the letterboxed case, and its offsets are the ones an off-by-one would live in.
TEST_CLASS(PresentationTests)
{
public:
  TEST_METHOD(TheCanvasIsTwelveEightyBySevenTwenty)
  {
    Assert::AreEqual(1280u, Neuron::Presentation::CANVAS_WIDTH_PIXELS);
    Assert::AreEqual(720u, Neuron::Presentation::CANVAS_HEIGHT_PIXELS);
  }

  // The scale a display gets, which is the one piece of ADR-076 this machine cannot run: it has a
  // 1080p monitor, so borderless fullscreen there is scale 1 and the interesting rows below have
  // no hardware here to be checked against. They are checked here instead.
  //
  // The windowed answer and the FULLSCREEN answer differ on exactly one panel, and that difference
  // is the whole reason borderless fullscreen ships: at 2560x1440 a window has to give up rows to
  // a caption and a taskbar and lands on 1, where the monitor itself has room for 2 exactly.
  TEST_METHOD(TheLargestScaleAPanelHasRoomFor)
  {
    // Fullscreen: the whole monitor, nothing subtracted.
    Assert::AreEqual(1u, Neuron::Presentation::LargestScaleFor(1920, 1080));
    Assert::AreEqual(2u, Neuron::Presentation::LargestScaleFor(2560, 1440));
    Assert::AreEqual(3u, Neuron::Presentation::LargestScaleFor(3840, 2160));

    // Windowed on the same panels, with a work area and a frame taken off. 2560x1440 is the row
    // that changes: 1418 rows of canvas fit twice, 1440 minus a taskbar and a caption do not.
    Assert::AreEqual(1u, Neuron::Presentation::LargestScaleFor(1920 - 18, 1020 - 47));
    Assert::AreEqual(1u, Neuron::Presentation::LargestScaleFor(2560 - 18, 1380 - 47));
    Assert::AreEqual(2u, Neuron::Presentation::LargestScaleFor(3840 - 18, 2100 - 47));
  }

  // Never zero, whatever it is asked. A scale of zero is a division by zero in ToCanvas.
  TEST_METHOD(ADisplayTooSmallForTheCanvasStillGetsScaleOne)
  {
    Assert::AreEqual(1u, Neuron::Presentation::LargestScaleFor(800, 600));
    Assert::AreEqual(1u, Neuron::Presentation::LargestScaleFor(0, 0));
    Assert::AreEqual(1u, Neuron::Presentation::LargestScaleFor(1280, 719));
  }

  // An exact fit is a fit. 1280x720 is scale 1 and not scale 0, and 2560x1440 is 2 and not 1.
  TEST_METHOD(AnExactFitCounts)
  {
    Assert::AreEqual(1u, Neuron::Presentation::LargestScaleFor(1280, 720));
    Assert::AreEqual(2u, Neuron::Presentation::LargestScaleFor(2560, 1440));
  }

  TEST_METHOD(ASurfaceThatIsTheCanvasHasNoLetterbox)
  {
    constexpr Neuron::Presentation EXACT = Neuron::Presentation::For(1280, 720, 1);

    Assert::AreEqual(1u, EXACT.scale);
    Assert::AreEqual(0u, EXACT.offsetXPixels);
    Assert::AreEqual(0u, EXACT.offsetYPixels);
  }

  TEST_METHOD(ADoubledSurfaceHasNoLetterboxEither)
  {
    constexpr Neuron::Presentation DOUBLED = Neuron::Presentation::For(2560, 1440, 2);

    Assert::AreEqual(2u, DOUBLED.scale);
    Assert::AreEqual(0u, DOUBLED.offsetXPixels);
    Assert::AreEqual(0u, DOUBLED.offsetYPixels);
  }

  TEST_METHOD(TenEightyPCentersTheCanvasAtScaleOne)
  {
    constexpr Neuron::Presentation LETTERBOXED = Neuron::Presentation::For(1920, 1080, 1);

    // (1920 - 1280) / 2 and (1080 - 720) / 2: a 320x180 border on each side.
    Assert::AreEqual(320u, LETTERBOXED.offsetXPixels);
    Assert::AreEqual(180u, LETTERBOXED.offsetYPixels);
  }

  // An unsigned subtraction that underflows here would put the offset at two billion and the
  // canvas nowhere. Nothing produces this today, which is exactly why it is pinned.
  TEST_METHOD(ASurfaceTooSmallForTheCanvasGetsAZeroOffset)
  {
    constexpr Neuron::Presentation TOO_SMALL = Neuron::Presentation::For(800, 600, 1);

    Assert::AreEqual(0u, TOO_SMALL.offsetXPixels);
    Assert::AreEqual(0u, TOO_SMALL.offsetYPixels);
  }

  // ToCanvas divides by the scale, so a zero would be a division by zero rather than a small
  // mistake. For() is the only producer and it is where the invariant is established.
  TEST_METHOD(AScaleOfZeroIsNotAThing)
  {
    Assert::AreEqual(1u, Neuron::Presentation::For(1280, 720, 0).scale);
  }

  TEST_METHOD(AtScaleOneToCanvasIsTheIdentity)
  {
    constexpr Neuron::Presentation EXACT = Neuron::Presentation::For(1280, 720, 1);

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(EXACT.ToCanvas(640.0F, 360.0F, x, y));
    Assert::AreEqual(640.0F, x);
    Assert::AreEqual(360.0F, y);
  }

  // Half a canvas pixel, and it is deliberate: the pages take float positions, so a drag at scale
  // 2 moves by one surface pixel rather than jumping two canvas pixels at a time.
  TEST_METHOD(AtScaleTwoASurfacePixelIsHalfACanvasPixel)
  {
    constexpr Neuron::Presentation DOUBLED = Neuron::Presentation::For(2560, 1440, 2);

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(DOUBLED.ToCanvas(2559.0F, 1439.0F, x, y));
    Assert::AreEqual(1279.5F, x);
    Assert::AreEqual(719.5F, y);
  }

  TEST_METHOD(APointInTheLetterboxIsOutsideTheCanvas)
  {
    constexpr Neuron::Presentation LETTERBOXED = Neuron::Presentation::For(1920, 1080, 1);

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(LETTERBOXED.ToCanvas(10.0F, 10.0F, x, y));

    // The canvas's first pixel, which is where the letterbox stops.
    Assert::IsTrue(LETTERBOXED.ToCanvas(320.0F, 180.0F, x, y));
    Assert::AreEqual(0.0F, x);
    Assert::AreEqual(0.0F, y);
  }

  // The far edge is exclusive. 1280 is the first column that is NOT the canvas, and a test that
  // said otherwise would be pinning an off-by-one that puts a tap on a control one pixel wide.
  TEST_METHOD(TheFarEdgeOfTheCanvasIsExclusive)
  {
    constexpr Neuron::Presentation LETTERBOXED = Neuron::Presentation::For(1920, 1080, 1);

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(LETTERBOXED.ToCanvas(320.0F + 1279.0F, 180.0F + 719.0F, x, y));
    Assert::IsFalse(LETTERBOXED.ToCanvas(320.0F + 1280.0F, 180.0F + 719.0F, x, y));
    Assert::IsFalse(LETTERBOXED.ToCanvas(320.0F + 1279.0F, 180.0F + 720.0F, x, y));
  }
};

} // namespace NeuronClientTests
