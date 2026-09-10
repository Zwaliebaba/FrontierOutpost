#include "pch.h"
#include "CppUnitTest.h"

// NeuronClient.h, not NeuronCore.h: it is the umbrella the library's own translation units
// compile against, so a suite that includes anything else is testing a header in a configuration
// nothing else ever builds it in.
#include "NeuronClient.h"

#include "Color.h"
#include "FontRenderer.h"
#include "PointerInput.h"
#include "SceneTarget.h"
#include "ShapeRenderer.h"

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
    input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 900, WINDOW_TOP + 500));

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

} // namespace NeuronClientTests
