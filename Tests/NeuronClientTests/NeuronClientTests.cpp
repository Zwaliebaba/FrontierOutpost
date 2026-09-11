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
#include "OrbitCamera.h"
#include "PointerInput.h"
#include "SceneTarget.h"
#include "ShapeRenderer.h"
#include "Starfield.h"

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

// The 8x8 font is the whole of this game's typography, so how many characters fit in a rail is
// not a detail -- it is what decides whether the design's copy can be shown at all (ADR-014).
// These are the arithmetic every right-aligned and centred thing on the main page is laid out
// against.
TEST_CLASS(FontMetricsTests)
{
public:
  TEST_METHOD(AGlyphIsEightPixelsAtOneTimes)
  {
    Assert::AreEqual(8u, Neuron::FontRenderer::AdvancePixels());
    Assert::AreEqual(8u, Neuron::FontRenderer::GlyphHeightPixels());
    Assert::AreEqual(16u, Neuron::FontRenderer::AdvancePixels(Neuron::FontRenderer::COUNTDOWN_SCALE));
    Assert::AreEqual(16u, Neuron::FontRenderer::GlyphHeightPixels(Neuron::FontRenderer::COUNTDOWN_SCALE));
  }

  TEST_METHOD(MeasuringIsTheAdvanceTimesTheLength)
  {
    Assert::AreEqual(64u, Neuron::FontRenderer::MeasurePixels("02:14:09"));
    Assert::AreEqual(128u, Neuron::FontRenderer::MeasurePixels("02:14:09", Neuron::FontRenderer::COUNTDOWN_SCALE));
    Assert::AreEqual(0u, Neuron::FontRenderer::MeasurePixels(""));
  }

  // The digest rail is 300 wide and spends 14 + 8 + 10 + 14 on margins, the dot and the gap,
  // which leaves 254 -- and 254 / 8 is 31, not 32. An off-by-one here is copy running under the
  // map.
  TEST_METHOD(TheDigestRailHoldsThirtyOneCharacters)
  {
    Assert::AreEqual(static_cast<size_t>(31), Neuron::FontRenderer::FitCharacters(254));
    Assert::AreEqual(static_cast<size_t>(31), Neuron::FontRenderer::FitCharacters(255), L"a part-character does not fit");
    Assert::AreEqual(static_cast<size_t>(0), Neuron::FontRenderer::FitCharacters(7));
  }
};

// Wrapping is the piece that had to exist because the reference's copy is longer than an 8px
// rail can hold, and it is the piece most likely to be quietly wrong (ADR-014).
TEST_CLASS(TextWrapTests)
{
public:
  TEST_METHOD(ShortTextIsOneLine)
  {
    const std::vector<std::string> lines = Neuron::FontRenderer::Wrap("Sealed region opens T60", 31);
    Assert::AreEqual(static_cast<size_t>(1), lines.size());
    Assert::AreEqual(std::string("Sealed region opens T60"), lines[0]);
  }

  TEST_METHOD(ItBreaksOnSpacesAndNeverExceedsTheWidth)
  {
    // The first digest event, at the width the digest rail actually has.
    const std::vector<std::string> lines = Neuron::FontRenderer::Wrap("Outpost present. Their fleet ETA T47. Ours ETA T47.", 31);
    Assert::IsTrue(lines.size() >= 2, L"this line does not fit in 31 characters");
    for (const std::string& line : lines)
    {
      Assert::IsTrue(line.size() <= 31, L"a wrapped line is wider than the rail");
      Assert::IsTrue(line.front() != ' ' && line.back() != ' ', L"a wrapped line carries no edge space");
    }
  }

  TEST_METHOD(NoWordIsLost)
  {
    constexpr const char* SOURCE = "Lane income foregone: 12/tick. Shipyard Idris idle.";
    std::string rejoined;
    for (const std::string& line : Neuron::FontRenderer::Wrap(SOURCE, 31))
    {
      if (!rejoined.empty())
      {
        rejoined.push_back(' ');
      }
      rejoined.append(line);
    }
    Assert::AreEqual(std::string(SOURCE), rejoined, L"wrapping must not drop or duplicate a word");
  }

  // A system name from a server is not something the screen gets to assume anything about.
  TEST_METHOD(AWordLongerThanTheLineIsHardBroken)
  {
    const std::vector<std::string> lines = Neuron::FontRenderer::Wrap("ABCDEFGHIJ", 4);
    Assert::AreEqual(static_cast<size_t>(3), lines.size());
    Assert::AreEqual(std::string("ABCD"), lines[0]);
    Assert::AreEqual(std::string("EFGH"), lines[1]);
    Assert::AreEqual(std::string("IJ"), lines[2]);
  }

  TEST_METHOD(AZeroWidthWrapsToNothingRatherThanLoopingForever)
  {
    Assert::AreEqual(static_cast<size_t>(0), Neuron::FontRenderer::Wrap("anything", 0).size());
  }
};

// The interface renderer tessellates on the CPU, so how many segments a circle gets is a real
// decision: too few facets a node, too many spends the frame's vertex budget on the star field.
TEST_CLASS(ShapeRendererTests)
{
public:
  TEST_METHOD(SegmentCountsAreClampedAndRiseWithRadius)
  {
    Assert::AreEqual(12u, Neuron::ShapeRenderer::SegmentsForRadius(0.7F), L"a star does not need more than the floor");
    Assert::AreEqual(12u, Neuron::ShapeRenderer::SegmentsForRadius(5.0F));
    Assert::AreEqual(40u, Neuron::ShapeRenderer::SegmentsForRadius(20.0F));
    Assert::AreEqual(64u, Neuron::ShapeRenderer::SegmentsForRadius(1000.0F), L"and never more than the ceiling");
  }
};

// The half of the click that is not the camera: a WM_POINTERDOWN carrying SCREEN coordinates
// becoming a point on the 1280x720 screen. The camera turns that into a world point and is tested
// above; between them they are the whole of what a tap does.
//
// This needs a real window, because the conversion is ScreenToClient and that is a property of a
// window rather than arithmetic. It is created off-screen and never shown.
TEST_CLASS(PointerInputTests)
{
public:
  static constexpr int WINDOW_LEFT = 300;
  static constexpr int WINDOW_TOP = 200;

  TEST_METHOD_INITIALIZE(CreateHostWindow)
  {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"NeuronClientTestsPointerHost";
    RegisterClassExW(&windowClass);

    // WS_POPUP, so the client area starts exactly at the window's top-left and the expected
    // numbers below are arithmetic rather than a guess about how thick a caption is.
    m_window = CreateWindowExW(0, L"NeuronClientTestsPointerHost", L"", WS_POPUP, WINDOW_LEFT, WINDOW_TOP, 1280, 720, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    Assert::IsNotNull(m_window, L"the test needs a window to convert screen coordinates against");
  }

  TEST_METHOD_CLEANUP(DestroyHostWindow)
  {
    if (m_window != nullptr)
    {
      DestroyWindow(m_window);
      m_window = nullptr;
    }
  }

  /// Packs a screen point the way Windows packs it into WM_POINTERDOWN's lParam.
  [[nodiscard]] static LPARAM PackScreenPoint(int _screenX, int _screenY)
  {
    return static_cast<LPARAM>((static_cast<std::uint32_t>(_screenY & 0xFFFF) << 16) | static_cast<std::uint32_t>(_screenX & 0xFFFF));
  }

  TEST_METHOD(NothingIsPendingBeforeAPointerDown)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y));
  }

  TEST_METHOD(APointerDownBecomesAScreenPixel)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    // The window's client area starts at (300, 200) on screen, so this screen point is client
    // (400, 300) -- and, with the client area now exactly the framebuffer, screen pixel (400, 300)
    // too. Before ADR-011 this arrived as virtual texel (200, 150).
    Assert::IsTrue(input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300)));
    Assert::IsTrue(input.HandleMessage(WM_POINTERUP, 0, PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(400.0F, x, 0.0F);
    Assert::AreEqual(300.0F, y, 0.0F);
  }

  TEST_METHOD(TheTopLeftOfTheClientAreaIsTheOrigin)
  {
    Neuron::PointerInput input;
    input.Create(m_window);
    Assert::IsTrue(input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT, WINDOW_TOP)));
    Assert::IsTrue(input.HandleMessage(WM_POINTERUP, 0, PackScreenPoint(WINDOW_LEFT, WINDOW_TOP)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(0.0F, x, 0.0F);
    Assert::AreEqual(0.0F, y, 0.0F);
  }

  TEST_METHOD(TakingAClickClearsIt)
  {
    Neuron::PointerInput input;
    input.Create(m_window);
    input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100));
    input.HandleMessage(WM_POINTERUP, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::IsFalse(input.TakeClick(x, y), L"a click is delivered once");
  }

  // Only the most recent tap survives: the ship goes where the player last pointed.
  TEST_METHOD(ASecondTapReplacesAnUnreadFirst)
  {
    Neuron::PointerInput input;
    input.Create(m_window);
    input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100));
    input.HandleMessage(WM_POINTERUP, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100));
    input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 900, WINDOW_TOP + 500));
    input.HandleMessage(WM_POINTERUP, 0, PackScreenPoint(WINDOW_LEFT + 900, WINDOW_TOP + 500));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(900.0F, x, 0.0F);
    Assert::AreEqual(500.0F, y, 0.0F);
  }

  // There is no keyboard control in this game and no mouse-button handler either: with
  // EnableMouseInPointer on, a mouse click arrives as WM_POINTERDOWN and a wheel notch as
  // WM_POINTERWHEEL. Anything outside the pointer family is ignored, and asserting that is what
  // stops a second input path appearing by accident.
  TEST_METHOD(MessagesOutsideThePointerFamilyAreIgnored)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    Assert::IsFalse(input.HandleMessage(WM_LBUTTONDOWN, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100)));
    Assert::IsFalse(input.HandleMessage(WM_KEYDOWN, VK_SPACE, 0));
    Assert::IsFalse(input.HandleMessage(WM_RBUTTONDOWN, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y));
    Assert::AreEqual(0, input.TakeZoomSteps());
  }

private:
  HWND m_window = nullptr;
};

// The camera the map is seen through (ADR-017). It is the piece of this screen whose bugs are
// hardest to see and easiest to talk yourself out of -- a mirrored axis or an inverted pitch
// still draws a plausible picture -- so the properties below are asserted rather than eyeballed.
TEST_CLASS(OrbitCameraTests)
{
public:
  static constexpr float PANE_X = 300.0F;
  static constexpr float PANE_Y = 48.0F;
  static constexpr float PANE_WIDTH = 650.0F;
  static constexpr float PANE_HEIGHT = 672.0F;

  static Neuron::OrbitCamera MakeCamera(float _yaw = 0.0F, float _pitch = 0.6F, float _distance = 1000.0F)
  {
    Neuron::OrbitCamera camera;
    camera.SetViewport(PANE_X, PANE_Y, PANE_WIDTH, PANE_HEIGHT);
    camera.SetTarget({0.0F, 0.0F, 0.0F});
    camera.SetDistance(_distance);
    camera.SetOrientation(_yaw, _pitch);
    return camera;
  }

  TEST_METHOD(TheTargetProjectsToTheCenterOfThePane)
  {
    for (const float yaw : {0.0F, 1.0F, -2.5F, 4.0F})
    {
      const Neuron::OrbitCamera camera = MakeCamera(yaw);
      const Neuron::OrbitCamera::ScreenPoint center = camera.Project({0.0F, 0.0F, 0.0F});

      Assert::IsTrue(center.visible);
      Assert::AreEqual(PANE_X + PANE_WIDTH * 0.5F, center.xPixels, 0.01F);
      Assert::AreEqual(PANE_Y + PANE_HEIGHT * 0.5F, center.yPixels, 0.01F);
    }
  }

  TEST_METHOD(TheEyeIsAtTheRequestedDistanceAndHeight)
  {
    const Neuron::OrbitCamera camera = MakeCamera(0.0F, 0.6F, 1000.0F);
    const Neuron::OrbitCamera::WorldPoint eye = camera.Position();

    Assert::AreEqual(1000.0F, std::sqrt(eye.x * eye.x + eye.y * eye.y + eye.z * eye.z), 0.01F);
    Assert::IsTrue(eye.y > 0.0F, L"the camera is above the plane it is looking at");
    // Yaw zero looks along -z, so the eye sits on +z.
    Assert::IsTrue(eye.z > 0.0F);
    Assert::AreEqual(0.0F, eye.x, 0.01F);
  }

  // The axis check that a wrong cross product would fail while still drawing a plausible map: at
  // yaw zero, world +x has to be screen RIGHT and world -z has to be further away.
  TEST_METHOD(AtYawZeroTheAxesAreNotMirrored)
  {
    const Neuron::OrbitCamera camera = MakeCamera();
    const Neuron::OrbitCamera::ScreenPoint center = camera.Project({0.0F, 0.0F, 0.0F});
    const Neuron::OrbitCamera::ScreenPoint right = camera.Project({100.0F, 0.0F, 0.0F});
    const Neuron::OrbitCamera::ScreenPoint away = camera.Project({0.0F, 0.0F, -100.0F});
    const Neuron::OrbitCamera::ScreenPoint up = camera.Project({0.0F, 100.0F, 0.0F});

    Assert::IsTrue(right.xPixels > center.xPixels, L"world +x is screen right");
    Assert::IsTrue(away.depth > center.depth, L"world -z is further from the eye");
    Assert::IsTrue(away.yPixels < center.yPixels, L"and further away is higher up the screen");
    Assert::IsTrue(up.yPixels < center.yPixels, L"world +y is screen up");
  }

  // Perspective, not orthographic: the same object is bigger when it is nearer. This is the whole
  // reason the projection was replaced.
  TEST_METHOD(NearerIsLarger)
  {
    const Neuron::OrbitCamera camera = MakeCamera();
    // `close`/`distant`, not `near`/`far`: <windows.h> still defines both of those to nothing.
    const float close = camera.PixelsPerWorldUnitAt(500.0F);
    const float distant = camera.PixelsPerWorldUnitAt(1500.0F);

    Assert::IsTrue(close > distant);
    Assert::AreEqual(3.0F, close / distant, 0.001F, L"three times nearer is three times bigger");
  }

  TEST_METHOD(PitchIsClampedAndYawIsNot)
  {
    Neuron::OrbitCamera camera = MakeCamera();

    camera.SetOrientation(0.0F, 100.0F);
    Assert::AreEqual(Neuron::OrbitCamera::MAX_PITCH_RADIANS, camera.PitchRadians(), 0.0001F);
    camera.SetOrientation(0.0F, -100.0F);
    Assert::AreEqual(Neuron::OrbitCamera::MIN_PITCH_RADIANS, camera.PitchRadians(), 0.0001F);

    camera.SetOrientation(50.0F, 0.6F);
    Assert::AreEqual(50.0F, camera.YawRadians(), 0.0001F, L"yaw runs as far as the player drags");
  }

  // Orbiting must not move the thing being looked at, at any angle. If it does, the map drifts
  // under the player as they turn it.
  TEST_METHOD(OrbitingKeepsTheTargetPutAndTheDistanceFixed)
  {
    Neuron::OrbitCamera camera = MakeCamera();

    for (std::int32_t step = 0; step < 24; ++step)
    {
      camera.Orbit(0.31F, 0.07F);

      const Neuron::OrbitCamera::WorldPoint eye = camera.Position();
      Assert::AreEqual(1000.0F, std::sqrt(eye.x * eye.x + eye.y * eye.y + eye.z * eye.z), 0.05F);

      const Neuron::OrbitCamera::ScreenPoint center = camera.Project({0.0F, 0.0F, 0.0F});
      Assert::AreEqual(PANE_X + PANE_WIDTH * 0.5F, center.xPixels, 0.05F);
      Assert::AreEqual(PANE_Y + PANE_HEIGHT * 0.5F, center.yPixels, 0.05F);
    }
  }

  // A point level with or behind the eye has no projection. Drawing one anyway mirrors it through
  // the camera, which puts a lane straight across the pane.
  TEST_METHOD(PointsBehindTheEyeAreNotVisible)
  {
    const Neuron::OrbitCamera camera = MakeCamera(0.0F, 0.6F, 1000.0F);
    const Neuron::OrbitCamera::WorldPoint eye = camera.Position();

    const Neuron::OrbitCamera::ScreenPoint behind = camera.Project({eye.x, eye.y + 10.0F, eye.z + 500.0F});
    Assert::IsFalse(behind.visible);

    const Neuron::OrbitCamera::ScreenPoint front = camera.Project({0.0F, 0.0F, 0.0F});
    Assert::IsTrue(front.visible);
  }

  // Yawing by a full turn is the same camera. Worth pinning because the yaw is deliberately left
  // unwrapped, and an implementation that accumulated error would drift over a long session.
  TEST_METHOD(AFullTurnComesBackToWhereItStarted)
  {
    const Neuron::OrbitCamera before = MakeCamera(0.4F);
    const Neuron::OrbitCamera after = MakeCamera(0.4F + 2.0F * 3.14159265358979323846F);

    const Neuron::OrbitCamera::ScreenPoint a = before.Project({120.0F, 30.0F, -80.0F});
    const Neuron::OrbitCamera::ScreenPoint b = after.Project({120.0F, 30.0F, -80.0F});

    Assert::AreEqual(a.xPixels, b.xPixels, 0.05F);
    Assert::AreEqual(a.yPixels, b.yPixels, 0.05F);
  }
};

// Tap and drag are the same gesture until they are not, and telling them apart is the whole of
// what this suite is about. Getting it wrong is not subtle: a tap that fires on press opens a
// panel every time the player tries to rotate the map, and a drag that swallows short movements
// makes the screen feel stuck (ADR-016).
TEST_CLASS(DragInputTests)
{
public:
  static constexpr int WINDOW_LEFT = 300;
  static constexpr int WINDOW_TOP = 200;

  TEST_METHOD_INITIALIZE(CreateHostWindow)
  {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"NeuronClientTestsDragHost";
    RegisterClassExW(&windowClass);

    m_window = CreateWindowExW(0, L"NeuronClientTestsDragHost", L"", WS_POPUP, WINDOW_LEFT, WINDOW_TOP, 1280, 720, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    Assert::IsNotNull(m_window);
  }

  TEST_METHOD_CLEANUP(DestroyHostWindow)
  {
    if (m_window != nullptr)
    {
      DestroyWindow(m_window);
      m_window = nullptr;
    }
  }

  [[nodiscard]] static LPARAM PackScreenPoint(int _screenX, int _screenY)
  {
    return static_cast<LPARAM>((static_cast<std::uint32_t>(_screenY & 0xFFFF) << 16) | static_cast<std::uint32_t>(_screenX & 0xFFFF));
  }

  [[nodiscard]] static WPARAM PackPointer(std::uint32_t _pointerId)
  {
    return static_cast<WPARAM>(_pointerId);
  }

  // A press and a lift in the same place is a tap, and it arrives on the LIFT. Before the map
  // could be rotated this fired on the press; it cannot any more, because at press time there is
  // no way to know the finger is not about to drag.
  TEST_METHOD(APressAndLiftIsATapAndArrivesOnTheLift)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y), L"a press alone is not yet a tap");

    input.HandleMessage(WM_POINTERUP, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(500.0F, x, 0.0F);
    Assert::AreEqual(300.0F, y, 0.0F);
  }

  // Movement inside the slop is a finger that did not hold still, not a gesture.
  TEST_METHOD(AWobbleWithinTheSlopIsStillATap)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 502, WINDOW_TOP + 301));
    input.HandleMessage(WM_POINTERUP, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 502, WINDOW_TOP + 301));

    Neuron::PointerInput::Drag drag = {};
    Assert::IsFalse(input.TakeDrag(drag), L"a wobble is not a drag");

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(500.0F, x, 0.0F, L"and the tap is where the finger went down, not where it wandered to");
  }

  TEST_METHOD(MovingPastTheSlopIsADragAndCancelsTheTap)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 560, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUP, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 560, WINDOW_TOP + 300));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y), L"a drag must not also tap whatever it started on");
  }

  // The movement made BEFORE the slop was crossed counts, or a quick flick loses its first few
  // pixels and the map lags the finger.
  TEST_METHOD(ADragReportsEveryPixelIncludingTheSlop)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 560, WINDOW_TOP + 320));

    Neuron::PointerInput::Drag drag = {};
    Assert::IsTrue(input.TakeDrag(drag));
    Assert::AreEqual(60.0F, drag.deltaXPixels, 0.0F);
    Assert::AreEqual(20.0F, drag.deltaYPixels, 0.0F);
    Assert::AreEqual(500.0F, drag.originXPixels, 0.0F, L"the drag belongs to where it began");
    Assert::AreEqual(300.0F, drag.originYPixels, 0.0F);
  }

  // Movement accumulates between frames and is consumed once. A frame that took longer than usual
  // must rotate by everything the finger did during it.
  TEST_METHOD(MovementAccumulatesAndIsConsumedOnce)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 540, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 570, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 600, WINDOW_TOP + 300));

    Neuron::PointerInput::Drag drag = {};
    Assert::IsTrue(input.TakeDrag(drag));
    Assert::AreEqual(100.0F, drag.deltaXPixels, 0.0F, L"40 + 30 + 30");

    Assert::IsFalse(input.TakeDrag(drag), L"and it is gone once taken");
  }

  // A second finger is a pinch. It must cancel both of the one-finger gestures, or a zoom would
  // also spin the map and tap whatever the first finger landed on.
  TEST_METHOD(ASecondContactCancelsTheDragAndTheTap)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERDOWN, PackPointer(2), PackScreenPoint(WINDOW_LEFT + 700, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300));

    Neuron::PointerInput::Drag drag = {};
    Assert::IsFalse(input.TakeDrag(drag), L"a pinch is not a drag");

    input.HandleMessage(WM_POINTERUP, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300));
    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y), L"nor a tap");
  }

  // Losing capture abandons the gesture. A press the window never saw the end of is not a tap.
  TEST_METHOD(LosingCaptureAbandonsThePress)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERCAPTURECHANGED, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 500, WINDOW_TOP + 300));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y));
  }

private:
  HWND m_window = nullptr;
};

// Zoom has two producers -- the wheel and a pinch -- and one intent (ADR-008). These test the
// producers; nothing consumes the intent yet -- the camera that did was removed with the MVP-01
// scene (ADR-015), and the map on the main page has no zoom wired to it.
TEST_CLASS(ZoomInputTests)
{
public:
  static constexpr int WINDOW_LEFT = 300;
  static constexpr int WINDOW_TOP = 200;
  static constexpr int CENTER_X = static_cast<int>(Neuron::SceneTarget::WIDTH_PIXELS) / 2;
  static constexpr int CENTER_Y = static_cast<int>(Neuron::SceneTarget::HEIGHT_PIXELS) / 2;

  TEST_METHOD_INITIALIZE(CreateHostWindow)
  {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"NeuronClientTestsZoomHost";
    RegisterClassExW(&windowClass);

    m_window = CreateWindowExW(0, L"NeuronClientTestsZoomHost", L"", WS_POPUP, WINDOW_LEFT, WINDOW_TOP, 1280, 720, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
    Assert::IsNotNull(m_window);
  }

  TEST_METHOD_CLEANUP(DestroyHostWindow)
  {
    if (m_window != nullptr)
    {
      DestroyWindow(m_window);
      m_window = nullptr;
    }
  }

  [[nodiscard]] static LPARAM PackScreenPoint(int _screenX, int _screenY)
  {
    return static_cast<LPARAM>((static_cast<std::uint32_t>(_screenY & 0xFFFF) << 16) | static_cast<std::uint32_t>(_screenX & 0xFFFF));
  }

  /// WM_POINTERWHEEL packs the pointer id in the low word and the wheel delta in the high word.
  [[nodiscard]] static WPARAM PackWheel(std::uint32_t _pointerId, int _delta)
  {
    return static_cast<WPARAM>((static_cast<std::uint32_t>(_delta & 0xFFFF) << 16) | (_pointerId & 0xFFFF));
  }

  [[nodiscard]] static WPARAM PackPointer(std::uint32_t _pointerId)
  {
    return static_cast<WPARAM>(_pointerId);
  }

  TEST_METHOD(NoInputMeansNoZoom)
  {
    Neuron::PointerInput input;
    input.Create(m_window);
    Assert::AreEqual(0, input.TakeZoomSteps());
  }

  TEST_METHOD(AWheelNotchIsOneStep)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    Assert::IsTrue(input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, WHEEL_DELTA), 0));
    Assert::AreEqual(1, input.TakeZoomSteps());

    Assert::IsTrue(input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, -WHEEL_DELTA), 0));
    Assert::AreEqual(-1, input.TakeZoomSteps(), L"scrolling back zooms back out");
  }

  // The classic mouse-wheel message is NOT handled, and that is the decision rather than an
  // oversight: with EnableMouseInPointer on, a real wheel was measured arriving as
  // WM_POINTERWHEEL (ADR-009), so a WM_MOUSEWHEEL case would be a second input path that never
  // runs -- exactly what MVP-01 section 2 rules out.
  TEST_METHOD(TheClassicMouseWheelMessageIsNotHandled)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    Assert::IsFalse(input.HandleMessage(WM_MOUSEWHEEL, PackWheel(0, WHEEL_DELTA), 0));
    Assert::AreEqual(0, input.TakeZoomSteps());
  }

  TEST_METHOD(TakingTheZoomClearsIt)
  {
    Neuron::PointerInput input;
    input.Create(m_window);
    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, WHEEL_DELTA), 0);

    Assert::AreEqual(1, input.TakeZoomSteps());
    Assert::AreEqual(0, input.TakeZoomSteps(), L"a zoom is delivered once");
  }

  TEST_METHOD(NotchesInOneFrameAddUp)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, WHEEL_DELTA), 0);
    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, WHEEL_DELTA), 0);
    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, WHEEL_DELTA * 2), 0);

    Assert::AreEqual(4, input.TakeZoomSteps());
  }

  // A high-resolution wheel sends deltas smaller than a notch. Throwing the remainder away would
  // make such a wheel do nothing at all, however far it is turned -- which is the bug this keeps
  // out.
  TEST_METHOD(SubNotchWheelDeltasAccumulate)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    constexpr int THIRD_OF_A_NOTCH = WHEEL_DELTA / 3;
    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, THIRD_OF_A_NOTCH), 0);
    Assert::AreEqual(0, input.TakeZoomSteps(), L"a third of a notch is not a step yet");

    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, THIRD_OF_A_NOTCH), 0);
    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, THIRD_OF_A_NOTCH), 0);
    Assert::AreEqual(1, input.TakeZoomSteps(), L"three thirds are");
  }

  /// The two contacts of a pinch a given distance apart, horizontally, centered on the client
  /// area. The separation is in screen pixels, and so is everything else: there is no present
  /// scale to convert through any more (ADR-011).
  ///
  /// The right-hand contact is placed at left + separation rather than at center + half, so the
  /// distance between them is EXACTLY the requested one whether or not it is even. Halving it and
  /// mirroring would lose a pixel on an odd separation, which is enough to leave a test's
  /// separation a hair under the ratio it was supposed to cross -- and that says nothing about
  /// the code under test.
  static void PlaceContacts(Neuron::PointerInput& _input, UINT _message, float _separationPixels)
  {
    const auto separation = static_cast<int>(std::lround(_separationPixels));
    const int left = WINDOW_LEFT + CENTER_X - separation / 2;
    const int y = WINDOW_TOP + CENTER_Y;

    _input.HandleMessage(_message, PackPointer(1), PackScreenPoint(left, y));
    _input.HandleMessage(_message, PackPointer(2), PackScreenPoint(left + separation, y));
  }

  static void StartPinch(Neuron::PointerInput& _input, float _separationPixels)
  {
    PlaceContacts(_input, WM_POINTERDOWN, _separationPixels);
  }

  static void MovePinch(Neuron::PointerInput& _input, float _separationPixels)
  {
    PlaceContacts(_input, WM_POINTERUPDATE, _separationPixels);
  }

  TEST_METHOD(SpreadingTwoContactsZoomsIn)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    StartPinch(input, 100.0F);
    Assert::AreEqual(0, input.TakeZoomSteps(), L"putting two fingers down is not yet a zoom");

    MovePinch(input, 100.0F * Neuron::PointerInput::PINCH_STEP_RATIO);
    Assert::AreEqual(1, input.TakeZoomSteps());
  }

  TEST_METHOD(PinchingTwoContactsTogetherZoomsOut)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    StartPinch(input, 200.0F);
    MovePinch(input, 200.0F / Neuron::PointerInput::PINCH_STEP_RATIO);

    Assert::AreEqual(-1, input.TakeZoomSteps());
  }

  // One update can carry a large jump, from a fast pinch or a frame that was missed. Banking a
  // single step for it would make the zoom lag the fingers.
  TEST_METHOD(ALargeSpreadInOneUpdateBanksSeveralSteps)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    // 64 to 125 is exactly 1.25 cubed, and every value on the way -- 64, 80, 100, 125 -- lands on
    // a whole screen pixel and is exact in a float.
    StartPinch(input, 64.0F);
    MovePinch(input, 125.0F);

    Assert::AreEqual(3, input.TakeZoomSteps());
  }

  // The first finger of a pinch is indistinguishable from a tap until the second lands. Without
  // this, every pinch would also order the ship to wherever that first finger touched down.
  //
  // Since ADR-016 the tap is decided on the LIFT, so what the second contact cancels is the
  // pending PRESS -- the lift that follows must not produce a tap either.
  TEST_METHOD(ASecondContactCancelsThePendingTap)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 200, WINDOW_TOP + 300));
    float x = 0.0F;
    float y = 0.0F;

    input.HandleMessage(WM_POINTERDOWN, PackPointer(2), PackScreenPoint(WINDOW_LEFT + 600, WINDOW_TOP + 300));
    Assert::IsFalse(input.TakeClick(x, y), L"a pinch must not also fly the ship somewhere");
  }

  TEST_METHOD(OneContactStillTapsNormally)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300));
    input.HandleMessage(WM_POINTERUP, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(400.0F, x, 0.0F);
    Assert::AreEqual(300.0F, y, 0.0F);
    Assert::AreEqual(0, input.TakeZoomSteps(), L"and does not zoom");
  }

  // Lifting a finger ends the pinch. Putting it back down starts a new one from where the fingers
  // now are, rather than resuming against a baseline from before the gap.
  TEST_METHOD(LiftingAContactEndsThePinch)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    StartPinch(input, 100.0F);
    input.HandleMessage(WM_POINTERUP, PackPointer(2), PackScreenPoint(WINDOW_LEFT + 690, WINDOW_TOP + 400));

    // A lone contact moving a long way is a drag, not a pinch.
    input.HandleMessage(WM_POINTERUPDATE, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 400));
    Assert::AreEqual(0, input.TakeZoomSteps());
  }

  // A third finger is not a bigger pinch. Resting a hand on the glass must not make the zoom jump.
  TEST_METHOD(AThirdContactIsIgnored)
  {
    Neuron::PointerInput input;
    input.Create(m_window);

    StartPinch(input, 100.0F);
    input.HandleMessage(WM_POINTERDOWN, PackPointer(3), PackScreenPoint(WINDOW_LEFT + 1200, WINDOW_TOP + 700));
    Assert::AreEqual(0, input.TakeZoomSteps());

    MovePinch(input, 100.0F * Neuron::PointerInput::PINCH_STEP_RATIO);
    Assert::AreEqual(1, input.TakeZoomSteps(), L"the original two still drive the pinch");
  }

private:
  HWND m_window = nullptr;
};

// ---- The sky ------------------------------------------------------------------------------------
//
// ADR-032. Every test here is one the star field this replaced would have failed, which is the point
// of writing them: the old one was thirty dots slid sideways by a constant, and nothing in the suite
// touched it, so a sky moving at a tenth of the right rate looked exactly like a sky that worked.

TEST_CLASS(StarfieldTests)
{
public:
  static constexpr float PANE_X = 300.0F;
  static constexpr float PANE_Y = 48.0F;
  static constexpr float PANE_WIDTH = 650.0F;
  static constexpr float PANE_HEIGHT = 672.0F;

  /// The map's own tilt limits, so the numbers below are the ones the game gets.
  static constexpr float MIN_PITCH = Neuron::OrbitCamera::MIN_PITCH_RADIANS;
  static constexpr float MAX_PITCH = Neuron::OrbitCamera::MAX_PITCH_RADIANS;

  static Neuron::OrbitCamera MakeCamera(float _yaw = 0.0F, float _pitch = 0.62F)
  {
    Neuron::OrbitCamera camera;
    camera.SetViewport(PANE_X, PANE_Y, PANE_WIDTH, PANE_HEIGHT);
    camera.SetTarget({0.0F, 0.0F, 0.0F});
    camera.SetDistance(700.0F);
    camera.SetOrientation(_yaw, _pitch);
    return camera;
  }

  /// How many orientations the sweeps below visit. Integers rather than a float step, because a
  /// float accumulated in a loop drifts and its last iteration is a coin toss -- and because a
  /// sweep that quietly stopped one step early would weaken exactly the tests that need it not to.
  static constexpr std::int32_t PITCH_SAMPLES = 33;
  static constexpr std::int32_t YAW_SAMPLES = 24;

  [[nodiscard]] static float PitchAt(std::int32_t _sample) noexcept
  {
    return MIN_PITCH + (MAX_PITCH - MIN_PITCH) * static_cast<float>(_sample) / static_cast<float>(PITCH_SAMPLES - 1);
  }

  [[nodiscard]] static float YawAt(std::int32_t _sample) noexcept
  {
    constexpr float TURN = 6.28318530717958647692F;
    return TURN * static_cast<float>(_sample) / static_cast<float>(YAW_SAMPLES);
  }

  [[nodiscard]] static bool SameSky(const Neuron::Starfield& _a, const Neuron::Starfield& _b)
  {
    return std::ranges::equal(_a.Stars(), _b.Stars(), [](const Neuron::Starfield::Star& _first, const Neuron::Starfield::Star& _second)
                              { return _first.x == _second.x && _first.y == _second.y && _first.z == _second.z; });
  }

  TEST_METHOD(EveryStarIsADirection)
  {
    const Neuron::Starfield sky;
    Assert::AreEqual(static_cast<std::size_t>(Neuron::Starfield::DEFAULT_COUNT), sky.Stars().size());

    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const float length = std::sqrt(star.x * star.x + star.y * star.y + star.z * star.z);
      Assert::AreEqual(1.0F, length, 0.0005F, L"a star is a direction, so it is a unit vector");
      Assert::IsTrue(star.radiusPixels >= 0.7F && star.radiusPixels <= 1.2F);
    }
  }

  TEST_METHOD(TheSameSeedIsTheSameSky)
  {
    Assert::IsTrue(SameSky(Neuron::Starfield{}, Neuron::Starfield{}),
                   L"a screenshot is only comparable with another one if the background is the same");
    Assert::IsFalse(SameSky(Neuron::Starfield{}, Neuron::Starfield{Neuron::Starfield::DEFAULT_SEED + 1}));
  }

  // Sampling two angles and calling them latitude and longitude would bunch stars at the poles, and
  // on a map that tips all the way to overhead that is not subtle -- the sky would visibly thicken
  // as the camera rose. Equal heights must hold equal numbers.
  TEST_METHOD(TheSkyIsEvenRatherThanBunchedAtThePoles)
  {
    const Neuron::Starfield sky{Neuron::Starfield::DEFAULT_SEED, 20000};

    // Four bands of equal HEIGHT, which on a sphere are four bands of equal area.
    std::array<std::int32_t, 4> bands = {};
    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const auto band = static_cast<std::size_t>(std::clamp((star.y + 1.0F) * 0.5F * 4.0F, 0.0F, 3.999F));
      ++bands[band];
    }

    for (const std::int32_t count : bands)
    {
      Assert::IsTrue(count > 4500 && count < 5500, L"equal areas of sky must hold roughly equal numbers of stars");
    }
  }

  // The defect that started this. A sky at infinity sweeps at the focal-length rate, which is the
  // FASTEST anything moves rather than the slowest: what stands still under rotation is whatever is
  // painted on the glass. The field this replaced slid at 90 px/radian, about a tenth of this.
  TEST_METHOD(TheSkySweepsAtTheFocalRate)
  {
    const float focal = (PANE_HEIGHT * 0.5F) / std::tan(Neuron::OrbitCamera::DEFAULT_FIELD_OF_VIEW_RADIANS * 0.5F);
    Assert::IsTrue(focal > 900.0F && focal < 940.0F, L"the arithmetic this is measured against");

    // A star on the view axis, followed through a small yaw.
    const Neuron::OrbitCamera::WorldPoint axis = {0.0F, 0.0F, -1.0F};
    const Neuron::OrbitCamera::ScreenPoint at = MakeCamera(0.0F).ProjectDirection(axis);
    const Neuron::OrbitCamera::ScreenPoint after = MakeCamera(0.01F).ProjectDirection(axis);

    const float pixelsPerRadian = std::abs(after.xPixels - at.xPixels) / 0.01F;
    Assert::IsTrue(pixelsPerRadian > 900.0F, L"a sky that barely moves is a sky painted on the screen");
    Assert::IsTrue(pixelsPerRadian < 1400.0F, L"and one that races is not a sky either");
  }

  // A direction is periodic, so this is true by construction rather than by arithmetic -- which is
  // exactly why it is worth a test. The old field slid by an unbounded multiple of the yaw, so a
  // full turn left the galaxy where it started and the sky 565 pixels away from where it started.
  TEST_METHOD(AFullTurnPutsTheSkyBackWhereItWas)
  {
    const Neuron::Starfield sky;
    constexpr float TWO_PI = 6.28318530717958647692F;

    const Neuron::OrbitCamera before = MakeCamera(0.7F);
    const Neuron::OrbitCamera after = MakeCamera(0.7F + TWO_PI);

    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const Neuron::OrbitCamera::WorldPoint direction = {star.x, star.y, star.z};
      const Neuron::OrbitCamera::ScreenPoint a = before.ProjectDirection(direction);
      const Neuron::OrbitCamera::ScreenPoint b = after.ProjectDirection(direction);

      Assert::AreEqual(a.visible, b.visible);
      if (a.visible)
      {
        Assert::AreEqual(a.xPixels, b.xPixels, 0.05F, L"the same view of the galaxy must be the same view of the sky");
        Assert::AreEqual(a.yPixels, b.yPixels, 0.05F);
      }
    }
  }

  // The old field had a 510-pixel band of stars in a 672-pixel pane and no vertical wrap, so tilting
  // opened an empty strip along the top that reached 146 pixels at full pitch. A sphere has no edge
  // to run off.
  //
  // **What this asserts is that no part of the pane is SYSTEMATICALLY starved**, not that every
  // single view has a star in its top eighth. Those are different claims and the first one is the
  // one that matters. At about thirty stars in view, an eighth of the pane holds three or four, and
  // a handful of the four hundred orientations swept below happen to hold none -- which is what a
  // scattering of stars looks like rather than a bug. The old field failed this catastrophically:
  // its top eighth held zero at EVERY yaw once the camera was tilted past halfway.
  TEST_METHOD(NoTiltStarvesAnyPartOfTheSky)
  {
    const Neuron::Starfield sky;

    for (std::int32_t pitchSample = 0; pitchSample < PITCH_SAMPLES; ++pitchSample)
    {
      std::int32_t top = 0;
      std::int32_t bottom = 0;

      for (std::int32_t yawSample = 0; yawSample < YAW_SAMPLES; ++yawSample)
      {
        const Neuron::OrbitCamera camera = MakeCamera(YawAt(yawSample), PitchAt(pitchSample));
        for (const Neuron::Starfield::Star& star : sky.Stars())
        {
          const Neuron::OrbitCamera::ScreenPoint at = camera.ProjectDirection({star.x, star.y, star.z});
          if (!camera.InsideViewport(at))
          {
            continue;
          }
          top += at.yPixels < PANE_Y + PANE_HEIGHT / 8.0F ? 1 : 0;
          bottom += at.yPixels >= PANE_Y + PANE_HEIGHT * 7.0F / 8.0F ? 1 : 0;
        }
      }

      Assert::IsTrue(top > 0, L"the top of the pane was empty at every angle of some tilt");
      Assert::IsTrue(bottom > 0, L"and so was the bottom");

      // Both bands are equally far off the view axis, so a sphere puts about the same number in
      // each however the camera is tilted.
      //
      // The bounds are wide because the scatter genuinely is: swept across the whole tilt this
      // ratio was MEASURED at 0.44 to 1.29, centred on 1.0 with no trend. It is noisier than
      // independent sampling would suggest, and for a reason worth knowing -- yaw rotates about the
      // world's up axis, so turning the camera slides the same stars sideways without changing
      // their elevation. A sweep of yaw does not resample the vertical distribution; it is the same
      // draw seen twenty-four times. What these bounds catch is a TREND, which is what the field
      // this replaced had: its top eighth held zero at every yaw once the camera passed halfway.
      const float ratio = static_cast<float>(top) / static_cast<float>(bottom);
      Assert::IsTrue(ratio > 0.3F && ratio < 3.0F, L"one edge of the pane is systematically emptier than the other");
    }
  }

  // Thirty was what the authored field showed, and it looked right. Over a whole sphere that needs
  // several hundred, because only the frustum's share is ever on screen -- so this is the test that
  // keeps DEFAULT_COUNT honest if the field of view ever changes.
  TEST_METHOD(AboutThirtyStarsAreVisibleFromAnywhere)
  {
    const Neuron::Starfield sky;

    std::int32_t fewest = 1000000;
    std::int32_t most = 0;
    for (std::int32_t pitchSample = 0; pitchSample < PITCH_SAMPLES; ++pitchSample)
    {
      for (std::int32_t yawSample = 0; yawSample < YAW_SAMPLES; ++yawSample)
      {
        const std::int32_t visible = sky.VisibleCount(MakeCamera(YawAt(yawSample), PitchAt(pitchSample)));
        fewest = std::min(fewest, visible);
        most = std::max(most, visible);
      }
    }

    // Measured across the whole tilt and a full turn: 16 at the emptiest angle, 43 at the fullest,
    // averaging about thirty -- which is what the authored field showed and what DEFAULT_COUNT was
    // chosen to reproduce.
    Assert::IsTrue(fewest >= 12, L"the sky must never be nearly empty from any angle");
    Assert::IsTrue(most <= 55, L"nor crowded from any other");
  }

  // A direction behind the eye has no projection, and the half of the sky behind the camera is
  // always exactly that. Drawing it would put stars in the pane twice over, mirrored.
  TEST_METHOD(HalfTheSkyIsAlwaysBehindYou)
  {
    const Neuron::OrbitCamera camera = MakeCamera(0.0F, 0.62F);

    Assert::IsTrue(camera.ProjectDirection({0.0F, 0.0F, -1.0F}).visible, L"straight ahead");
    Assert::IsFalse(camera.ProjectDirection({0.0F, 0.0F, 1.0F}).visible, L"straight behind");

    const Neuron::Starfield sky;
    Assert::IsTrue(sky.VisibleCount(camera) < Neuron::Starfield::DEFAULT_COUNT / 2, L"at most half the sphere can ever be in front");
  }

  // `ProjectDirection` is `Project` with the eye subtraction removed, so a point far enough away
  // must agree with it. This is the cross-check that says the new method is the same lens rather
  // than a second, subtly different one.
  TEST_METHOD(ADirectionAgreesWithAPointAVeryLongWayOff)
  {
    const Neuron::OrbitCamera camera = MakeCamera(0.9F, 0.5F);
    const Neuron::OrbitCamera::WorldPoint eye = camera.Position();
    const Neuron::Starfield sky{Neuron::Starfield::DEFAULT_SEED, 64};

    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const Neuron::OrbitCamera::ScreenPoint direction = camera.ProjectDirection({star.x, star.y, star.z});
      if (!camera.InsideViewport(direction))
      {
        continue;
      }

      constexpr float FAR_AWAY = 4.0e6F;
      const Neuron::OrbitCamera::ScreenPoint point =
        camera.Project({eye.x + star.x * FAR_AWAY, eye.y + star.y * FAR_AWAY, eye.z + star.z * FAR_AWAY});

      Assert::AreEqual(direction.xPixels, point.xPixels, 1.0F);
      Assert::AreEqual(direction.yPixels, point.yPixels, 1.0F);
    }
  }
};

} // namespace NeuronClientTests
