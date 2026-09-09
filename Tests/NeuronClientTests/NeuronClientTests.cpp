#include "pch.h"
#include "CppUnitTest.h"

// NeuronClient.h, not NeuronCore.h: it is the umbrella the library's own translation units
// compile against, so a suite that includes anything else is testing a header in a configuration
// nothing else ever builds it in.
#include "NeuronClient.h"

#include "FontRenderer.h"
#include "IsometricCamera.h"
#include "Palette.h"
#include "PointerInput.h"

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

// The camera is the one piece of this renderer with an inverse, and the inverse is what turns a
// click into an order (MVP-01 step 6). Everything here is arithmetic with no device, no window
// and no D3D12, which is the point: the projection can be wrong in ways that still draw a
// perfectly plausible ship.
TEST_CLASS(IsometricCameraTests)
{
public:
  static constexpr float VIRTUAL_WIDTH = 640.0F;
  static constexpr float VIRTUAL_HEIGHT = 400.0F;

  // Exact powers of two throughout the projection, so equality is the right comparison and a
  // tolerance would only hide a real error. The one place it is not is the camera's own rounding,
  // which is tested by value rather than by tolerance too.
  static constexpr float EXACT = 0.0F;

  static Neuron::IsometricCamera MakeCamera(float _x = 0.0F, float _y = 0.0F, float _z = 0.0F)
  {
    Neuron::IsometricCamera camera{VIRTUAL_WIDTH, VIRTUAL_HEIGHT};
    camera.Follow({_x, _y, _z});
    return camera;
  }

  TEST_METHOD(TheTargetIsAtTheCenterOfTheScreen)
  {
    const Neuron::IsometricCamera camera = MakeCamera(17.0F, 0.0F, -4.0F);
    const Neuron::IsometricCamera::ScreenPoint center = camera.Project({17.0F, 0.0F, -4.0F});

    Assert::AreEqual(VIRTUAL_WIDTH * 0.5F, center.xPixels, EXACT);
    Assert::AreEqual(VIRTUAL_HEIGHT * 0.5F, center.yPixels, EXACT);
  }

  // The 2:1 in "2:1 dimetric" (ADR-003). A one-unit square on the ground is a diamond sixteen
  // pixels wide and eight tall, and these four numbers are that claim.
  TEST_METHOD(AGroundTileIsExactlyTwiceAsWideAsItIsTall)
  {
    const Neuron::IsometricCamera camera = MakeCamera();
    const Neuron::IsometricCamera::ScreenPoint origin = camera.Project({0.0F, 0.0F, 0.0F});
    const Neuron::IsometricCamera::ScreenPoint alongX = camera.Project({1.0F, 0.0F, 0.0F});
    const Neuron::IsometricCamera::ScreenPoint alongZ = camera.Project({0.0F, 0.0F, 1.0F});

    Assert::AreEqual(8.0F, alongX.xPixels - origin.xPixels, EXACT, L"+X moves half a tile width right");
    Assert::AreEqual(4.0F, alongX.yPixels - origin.yPixels, EXACT, L"+X moves half a tile height down");
    Assert::AreEqual(-8.0F, alongZ.xPixels - origin.xPixels, EXACT, L"+Z moves half a tile width left");
    Assert::AreEqual(4.0F, alongZ.yPixels - origin.yPixels, EXACT, L"+Z moves half a tile height down");
  }

  TEST_METHOD(HeightMovesStraightUpTheScreen)
  {
    const Neuron::IsometricCamera camera = MakeCamera();
    const Neuron::IsometricCamera::ScreenPoint ground = camera.Project({0.0F, 0.0F, 0.0F});
    const Neuron::IsometricCamera::ScreenPoint raised = camera.Project({0.0F, 1.0F, 0.0F});

    Assert::AreEqual(0.0F, raised.xPixels - ground.xPixels, EXACT, L"height must not shift a pixel sideways");
    Assert::AreEqual(-8.0F, raised.yPixels - ground.yPixels, EXACT);
  }

  TEST_METHOD(TheCenterOfTheScreenUnprojectsToTheTarget)
  {
    const Neuron::IsometricCamera camera = MakeCamera(12.0F, 0.0F, 5.0F);
    const Neuron::IsometricCamera::WorldPoint ground = camera.UnprojectToGround(VIRTUAL_WIDTH * 0.5F, VIRTUAL_HEIGHT * 0.5F);

    Assert::AreEqual(12.0F, ground.x, EXACT);
    Assert::AreEqual(0.0F, ground.y, EXACT, L"unprojection lands on the ground plane by construction");
    Assert::AreEqual(5.0F, ground.z, EXACT);
  }

  // A known pixel to a known offset. Sixteen pixels right of center is x - z == 2 with x + z == 0,
  // which is (1, 0, -1) -- worked out by hand rather than by running the code.
  TEST_METHOD(AKnownPixelUnprojectsToAKnownPoint)
  {
    const Neuron::IsometricCamera camera = MakeCamera();
    const Neuron::IsometricCamera::WorldPoint ground = camera.UnprojectToGround(VIRTUAL_WIDTH * 0.5F + 16.0F, VIRTUAL_HEIGHT * 0.5F);

    Assert::AreEqual(1.0F, ground.x, EXACT);
    Assert::AreEqual(-1.0F, ground.z, EXACT);
  }

  TEST_METHOD(ProjectAndUnprojectAreInverses)
  {
    const Neuron::IsometricCamera camera = MakeCamera(-6.0F, 0.0F, 3.0F);

    for (const Neuron::IsometricCamera::WorldPoint& expected :
         {Neuron::IsometricCamera::WorldPoint{0.0F, 0.0F, 0.0F}, Neuron::IsometricCamera::WorldPoint{40.0F, 0.0F, -17.0F},
          Neuron::IsometricCamera::WorldPoint{-125.5F, 0.0F, 64.25F}})
    {
      const Neuron::IsometricCamera::ScreenPoint pixel = camera.Project(expected);
      const Neuron::IsometricCamera::WorldPoint roundTrip = camera.UnprojectToGround(pixel.xPixels, pixel.yPixels);

      Assert::AreEqual(expected.x, roundTrip.x, EXACT);
      Assert::AreEqual(expected.z, roundTrip.z, EXACT);
    }
  }

  // The property MVP-01 step 6 asks for: the same pixel, after the ship has moved, is a different
  // world point by exactly the ship's displacement. This is what makes a click an order in world
  // coordinates rather than an offset that quietly means something else once the camera scrolls.
  TEST_METHOD(TheSamePixelMovesWithTheCamera)
  {
    const Neuron::IsometricCamera before = MakeCamera(0.0F, 0.0F, 0.0F);
    // A displacement that projects to whole pixels, so the camera's snap adds nothing to compare
    // against: (3, 0, 1) is 16 right and 16 down.
    const Neuron::IsometricCamera after = MakeCamera(3.0F, 0.0F, 1.0F);

    constexpr float SAMPLE_X = 200.0F;
    constexpr float SAMPLE_Y = 150.0F;
    const Neuron::IsometricCamera::WorldPoint groundBefore = before.UnprojectToGround(SAMPLE_X, SAMPLE_Y);
    const Neuron::IsometricCamera::WorldPoint groundAfter = after.UnprojectToGround(SAMPLE_X, SAMPLE_Y);

    Assert::AreEqual(3.0F, groundAfter.x - groundBefore.x, EXACT);
    Assert::AreEqual(1.0F, groundAfter.z - groundBefore.z, EXACT);
  }

  // ADR-003: the camera snaps to whole virtual pixels. A target a hundredth of a unit away from
  // one that snaps identically must produce an identical projection -- if it does not, the whole
  // scene shimmers by a pixel as the ship drifts.
  TEST_METHOD(TheCameraSnapsToWholePixels)
  {
    const Neuron::IsometricCamera exact = MakeCamera(1.0F, 0.0F, 0.0F);
    const Neuron::IsometricCamera nudged = MakeCamera(1.01F, 0.0F, 0.0F);

    // 1.0 projects to (8, 4); 1.01 projects to (8.08, 4.04), which rounds to the same whole pixel.
    const Neuron::IsometricCamera::ScreenPoint fromExact = exact.Project({0.0F, 0.0F, 0.0F});
    const Neuron::IsometricCamera::ScreenPoint fromNudged = nudged.Project({0.0F, 0.0F, 0.0F});

    Assert::AreEqual(fromExact.xPixels, fromNudged.xPixels, EXACT);
    Assert::AreEqual(fromExact.yPixels, fromNudged.yPixels, EXACT);
  }

  TEST_METHOD(TheViewProjectionPutsTheTargetAtTheCenterOfClipSpace)
  {
    const Neuron::IsometricCamera camera = MakeCamera(9.0F, 0.0F, 3.0F);
    const std::array<float, 16> matrix = camera.ViewProjection();

    // Row-vector convention: clip = (x, y, z, 1) * matrix.
    auto transform = [&matrix](float _x, float _y, float _z, std::size_t _column)
    { return _x * matrix[_column] + _y * matrix[4 + _column] + _z * matrix[8 + _column] + matrix[12 + _column]; };

    Assert::AreEqual(0.0F, transform(9.0F, 0.0F, 3.0F, 0), EXACT, L"the target is at clip x = 0");
    Assert::AreEqual(0.0F, transform(9.0F, 0.0F, 3.0F, 1), EXACT, L"the target is at clip y = 0");
    Assert::AreEqual(0.5F, transform(9.0F, 0.0F, 3.0F, 2), 1.0e-6F, L"and halfway through the depth range");
  }

  // Nearer must mean a smaller depth: the camera looks along (1, 1, 1), so a point further along
  // that direction is closer to it. Getting this backwards draws the ship inside out and is not
  // obvious on a mesh that is nearly convex.
  TEST_METHOD(PointsTowardsTheCameraAreNearer)
  {
    const Neuron::IsometricCamera camera = MakeCamera();
    const std::array<float, 16> matrix = camera.ViewProjection();

    auto depth = [&matrix](float _x, float _y, float _z) { return _x * matrix[2] + _y * matrix[6] + _z * matrix[10] + matrix[14]; };

    Assert::IsTrue(depth(1.0F, 1.0F, 1.0F) < depth(0.0F, 0.0F, 0.0F), L"towards the camera is nearer");
    Assert::IsTrue(depth(0.0F, 0.0F, 0.0F) < depth(-1.0F, -1.0F, -1.0F), L"away from the camera is further");
  }
};

// The half of the click that is not the camera: a WM_POINTERDOWN carrying SCREEN coordinates
// becoming a point on the 640x400 virtual screen. The camera turns that into a world point and is
// tested above; between them they are the whole of what a tap does.
//
// This needs a real window, because the conversion is ScreenToClient and that is a property of a
// window rather than arithmetic. It is created off-screen and never shown.
TEST_CLASS(PointerInputTests)
{
public:
  static constexpr int WINDOW_LEFT = 300;
  static constexpr int WINDOW_TOP = 200;
  static constexpr std::uint32_t PRESENT_SCALE = 2;

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
    m_window = CreateWindowExW(0, L"NeuronClientTestsPointerHost", L"", WS_POPUP, WINDOW_LEFT, WINDOW_TOP, 1280, 800, nullptr, nullptr,
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
    input.Create(m_window, PRESENT_SCALE);

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y));
  }

  TEST_METHOD(APointerDownBecomesAVirtualTexel)
  {
    Neuron::PointerInput input;
    input.Create(m_window, PRESENT_SCALE);

    // The window's client area starts at (300, 200) on screen, so this screen point is client
    // (400, 300), which at present scale 2 is virtual texel (200, 150).
    Assert::IsTrue(input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 400, WINDOW_TOP + 300)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(200.0F, x, 0.0F);
    Assert::AreEqual(150.0F, y, 0.0F);
  }

  TEST_METHOD(TheTopLeftOfTheClientAreaIsTheOrigin)
  {
    Neuron::PointerInput input;
    input.Create(m_window, PRESENT_SCALE);
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
    input.Create(m_window, PRESENT_SCALE);
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
    input.Create(m_window, PRESENT_SCALE);
    input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100));
    input.HandleMessage(WM_POINTERDOWN, 0, PackScreenPoint(WINDOW_LEFT + 900, WINDOW_TOP + 500));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(450.0F, x, 0.0F);
    Assert::AreEqual(250.0F, y, 0.0F);
  }

  // There is no keyboard control in this game and no mouse-button handler either: with
  // EnableMouseInPointer on, a mouse click arrives as WM_POINTERDOWN. Anything else is ignored,
  // and asserting that is what stops a second input path appearing by accident.
  TEST_METHOD(OtherMessagesAreIgnored)
  {
    Neuron::PointerInput input;
    input.Create(m_window, PRESENT_SCALE);

    Assert::IsFalse(input.HandleMessage(WM_LBUTTONDOWN, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100)));
    Assert::IsFalse(input.HandleMessage(WM_KEYDOWN, VK_SPACE, 0));
    Assert::IsFalse(input.HandleMessage(WM_POINTERUP, 0, PackScreenPoint(WINDOW_LEFT + 100, WINDOW_TOP + 100)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y));
  }

private:
  HWND m_window = nullptr;
};

} // namespace NeuronClientTests
