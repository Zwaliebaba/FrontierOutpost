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

// The atlas FontRenderer uploads is Font.h's own bytes, and the glyph table is what says where in
// it each letter lives -- so these assertions are assertions about what ends up on the screen.
// They are worth having because the two things that would break the font are both silent: an
// off-by-one in the codepoint lookup shifts the whole alphabet by one and still draws letters, and
// a glyph pointed at the wrong atlas box still draws something glyph-shaped.
//
// **They are written against the SHAPE of the tables and not against one face's pixels.** The old
// version of this class pinned the eight bytes of 'A' as hex, which was the right test of a
// hand-typed font and is the wrong test of a baked one: it would have to be rewritten every time
// anybody changed a size or a weight, and a test nobody can read the failure of gets deleted
// rather than fixed (ADR-073).
TEST_CLASS(FontTests)
{
public:
  TEST_METHOD(EveryFaceIsBakedAndNonEmpty)
  {
    Assert::AreEqual(static_cast<size_t>(Neuron::Face::Count), Neuron::FONT_FACES.size());
    for (std::size_t index = 0; index < Neuron::FONT_FACES.size(); ++index)
    {
      const Neuron::FontFace& face = Neuron::FONT_FACES[index];
      Assert::IsTrue(face.glyphCount > 0, L"a face with no glyphs draws nothing at all");
      Assert::IsTrue(static_cast<std::size_t>(face.firstGlyph) + face.glyphCount <= Neuron::FONT_GLYPHS.size(),
                     L"a face's slice runs past the end of the glyph table");
      Assert::IsTrue(face.ascent > 0, L"a face with no ascent puts every glyph on the same row");
    }
  }

  // The lookup is a binary search over each face's slice, which means the slice has to be sorted.
  // Unsorted, it would still find SOME glyph for every letter, which is exactly the silent failure
  // worth pinning.
  TEST_METHOD(EveryFaceSliceIsSortedByCodepoint)
  {
    for (const Neuron::FontFace& face : Neuron::FONT_FACES)
    {
      for (std::uint16_t offset = 1; offset < face.glyphCount; ++offset)
      {
        const std::size_t at = static_cast<std::size_t>(face.firstGlyph) + offset;
        Assert::IsTrue(Neuron::FONT_GLYPHS[at - 1].codepoint < Neuron::FONT_GLYPHS[at].codepoint,
                       L"the glyph table must be sorted for the binary search to be right");
      }
    }
  }

  TEST_METHOD(ALookupFindsTheCodepointItWasAskedFor)
  {
    for (std::size_t index = 0; index < Neuron::FONT_FACES.size(); ++index)
    {
      const auto face = static_cast<Neuron::Face>(index);
      Assert::AreEqual(static_cast<std::uint32_t>(U'A'), Neuron::FontRenderer::GlyphOf(U'A', face).codepoint);
      Assert::AreEqual(static_cast<std::uint32_t>(U'~'), Neuron::FontRenderer::GlyphOf(U'~', face).codepoint);
      Assert::AreEqual(static_cast<std::uint32_t>(U' '), Neuron::FontRenderer::GlyphOf(U' ', face).codepoint);
    }
  }

  // The five characters ADR-014 could not carry, and the whole reason the lookup stopped being
  // `code - 32`. A font that silently drew them as blanks would look exactly like the old
  // substitutions, which is why this asserts the codepoint rather than that something was found.
  // The escapes are \u rather than the characters themselves because two of the five are outside
  // code page 1252 and MSVC will not put them in a narrow literal -- char32_t is fine, but the
  // habit is worth keeping where the file is read beside the ones that are not.
  TEST_METHOD(TheCharactersAdr014SubstitutedAreBaked)
  {
    for (const char32_t codepoint : {U'\u00B7', U'\u2212', U'\u2013', U'\u2192', U'\u203A'})
    {
      Assert::AreEqual(static_cast<std::uint32_t>(codepoint), Neuron::FontRenderer::GlyphOf(codepoint, Neuron::Face::MonoRegular).codepoint,
                       L"a character ADR-014 had to substitute is missing from the bake");
    }
  }

  TEST_METHOD(AnUnbakedCodepointFallsBackToABlank)
  {
    const Neuron::FontGlyph& missing = Neuron::FontRenderer::GlyphOf(U'\u4E2D', Neuron::Face::MonoRegular);
    Assert::AreEqual(static_cast<std::uint32_t>(U' '), missing.codepoint,
                     L"an unbaked codepoint must draw the blank, not index past the table");
  }

  TEST_METHOD(SpaceHasNoInk)
  {
    const Neuron::FontGlyph& space = Neuron::FontRenderer::GlyphOf(U' ', Neuron::Face::MonoRegular);
    for (std::uint32_t row = 0; row < space.height; ++row)
    {
      for (std::uint32_t column = 0; column < space.width; ++column)
      {
        Assert::AreEqual(static_cast<std::uint8_t>(0), Neuron::FontRenderer::AtlasTexel(space.atlasX + column, space.atlasY + row),
                         L"a space has no lit pixels");
      }
    }
  }

  TEST_METHOD(ACapitalAHasInkAndAdvancesTheCursor)
  {
    const Neuron::FontGlyph& letter = Neuron::FontRenderer::GlyphOf(U'A', Neuron::Face::MonoRegular);
    Assert::IsTrue(letter.advance > 0, L"a letter that advances nothing writes the next one on top of it");

    bool anyInk = false;
    for (std::uint32_t row = 0; row < letter.height && !anyInk; ++row)
    {
      for (std::uint32_t column = 0; column < letter.width && !anyInk; ++column)
      {
        anyInk = Neuron::FontRenderer::AtlasTexel(letter.atlasX + column, letter.atlasY + row) != 0;
      }
    }
    Assert::IsTrue(anyInk, L"'A' is blank, which means the bake or the atlas coordinates are wrong");
  }

  // A glyph box that runs off the atlas reads whatever is next in the array, which on screen looks
  // like a letter with somebody else's pixels stuck to it.
  TEST_METHOD(EveryGlyphBoxIsInsideTheAtlas)
  {
    for (const Neuron::FontGlyph& glyph : Neuron::FONT_GLYPHS)
    {
      Assert::IsTrue(static_cast<std::uint32_t>(glyph.atlasX) + glyph.width <= Neuron::FONT_ATLAS_WIDTH,
                     L"a glyph runs off the right of the atlas");
      Assert::IsTrue(static_cast<std::uint32_t>(glyph.atlasY) + glyph.height <= Neuron::FONT_ATLAS_HEIGHT,
                     L"a glyph runs off the bottom of the atlas");
    }
    Assert::AreEqual(static_cast<size_t>(Neuron::FONT_ATLAS_WIDTH) * Neuron::FONT_ATLAS_HEIGHT, Neuron::FONT_ATLAS.size());
  }
};

// Every std::string in this tree is UTF-8 (NeuronCore/Text.h), which nothing had to know while the
// font was 96 bytes of ASCII and everything has to know now that the middot and the arrow are
// glyphs rather than substitutions (ADR-074).
TEST_CLASS(FontUtf8Tests)
{
public:
  TEST_METHOD(AsciiIsOneByte)
  {
    const auto decoded = Neuron::FontRenderer::DecodeUtf8("A", 0);
    Assert::AreEqual(static_cast<std::uint32_t>(U'A'), static_cast<std::uint32_t>(decoded.codepoint));
    Assert::AreEqual(static_cast<size_t>(1), decoded.bytes);
  }

  TEST_METHOD(TheMiddotIsTwoBytesAndTheArrowIsThree)
  {
    // Byte escapes rather than \u: a narrow literal is encoded in the SOURCE code page, which on
    // this machine is 1252 and cannot hold either character (MSVC C4566). These are the UTF-8
    // bytes themselves, which is what the decoder is handed at runtime in any case.
    const auto middot = Neuron::FontRenderer::DecodeUtf8("\xC2\xB7", 0);
    Assert::AreEqual(static_cast<std::uint32_t>(0x00B7), static_cast<std::uint32_t>(middot.codepoint));
    Assert::AreEqual(static_cast<size_t>(2), middot.bytes);

    const auto arrow = Neuron::FontRenderer::DecodeUtf8("\xE2\x86\x92", 0);
    Assert::AreEqual(static_cast<std::uint32_t>(0x2192), static_cast<std::uint32_t>(arrow.codepoint));
    Assert::AreEqual(static_cast<size_t>(3), arrow.bytes);
  }

  // A stray continuation byte used to draw as a space and must still consume exactly one byte:
  // consuming none is an infinite loop in every caller that walks a string with this.
  TEST_METHOD(AMalformedByteIsConsumedAsOne)
  {
    const std::string malformed(1, static_cast<char>(0xE9));
    const auto decoded = Neuron::FontRenderer::DecodeUtf8(malformed, 0);
    Assert::AreEqual(static_cast<size_t>(1), decoded.bytes, L"a bad byte must not stall the cursor");
    Assert::AreEqual(static_cast<std::uint32_t>(0xFFFD), static_cast<std::uint32_t>(decoded.codepoint));
  }

  TEST_METHOD(MeasuringCountsAMultiByteGlyphOnce)
  {
    // Three codepoints, five bytes. Measured as bytes it would come out nearly twice as wide.
    // Split literal: "\xC2\xB7B" would parse `\xB7B` as one hex escape and not as a middot
    // followed by a B.
    Assert::AreEqual(Neuron::FontRenderer::MeasurePixels("A\xC2\xB7"
                                                         "B"),
                     3U * Neuron::FontRenderer::AdvanceOf(U'A'), L"the middot must measure as one glyph");
  }
};

// The 8x8 font is the whole of this game's typography, so how many characters fit in a rail is
// not a detail -- it is what decides whether the design's copy can be shown at all (ADR-014).
// These are the arithmetic every right-aligned and centred thing on the main page is laid out
// against.
TEST_CLASS(FontMetricsTests)
{
public:
  // Plex Mono is 0.600em, so at the 12px it is baked at a column is SEVEN pixels -- one narrower
  // than the 8x8 font it replaced, which is what gives the rails their extra characters a line. A
  // line box is ascent 13 plus descent 4.
  TEST_METHOD(AMonoColumnIsSevenPixels)
  {
    Assert::AreEqual(7u, Neuron::FontRenderer::AdvancePixels());
    Assert::AreEqual(17u, Neuron::FontRenderer::GlyphHeightPixels());

    // A whole-number blow-up is still exactly that, and nothing in the game asks for one any more
    // (ADR-084): the second size is a baked cut.
    Assert::AreEqual(14u, Neuron::FontRenderer::AdvancePixels(Neuron::FontRenderer::DEFAULT_FACE, 2));
    Assert::AreEqual(34u, Neuron::FontRenderer::GlyphHeightPixels(Neuron::FontRenderer::DEFAULT_FACE, 2));
  }

  // The display cut is the same Medium file at 16px, so it is wider and taller than the 12px cuts
  // and still fixed-pitch. 0.600em at 16px is 9.6 and rounds to TEN.
  TEST_METHOD(TheDisplayCutIsASizeAndNotAScale)
  {
    Assert::AreEqual(10u, Neuron::FontRenderer::AdvancePixels(Neuron::Face::MonoDisplay));
    Assert::AreEqual(22u, Neuron::FontRenderer::GlyphHeightPixels(Neuron::Face::MonoDisplay));
    Assert::IsTrue(Neuron::FontRenderer::AdvancePixels(Neuron::Face::MonoDisplay) > Neuron::FontRenderer::AdvancePixels(),
                   L"the display cut is no bigger than the body cut");

    // Fixed pitch, like every other mono cut: `i` and `W` take the same column.
    Assert::AreEqual(Neuron::FontRenderer::AdvanceOf(U'i', Neuron::Face::MonoDisplay),
                     Neuron::FontRenderer::AdvanceOf(U'W', Neuron::Face::MonoDisplay));
  }

  TEST_METHOD(MeasuringSumsTheAdvances)
  {
    Assert::AreEqual(56u, Neuron::FontRenderer::MeasurePixels("02:14:09"));
    Assert::AreEqual(112u, Neuron::FontRenderer::MeasurePixels("02:14:09", Neuron::FontRenderer::DEFAULT_FACE, 2));
    Assert::AreEqual(80u, Neuron::FontRenderer::MeasurePixels("02:14:09", Neuron::Face::MonoDisplay));
    Assert::AreEqual(0u, Neuron::FontRenderer::MeasurePixels(""));
  }

  // The whole point of two families (ADR-074): one holds a column and one does not. A sans that
  // measured fixed-pitch would mean the bake had picked up the wrong file, which is invisible on
  // screen until a sentence fails to line up with nothing.
  TEST_METHOD(MonoHoldsAColumnAndSansDoesNot)
  {
    for (const Neuron::Face face : {Neuron::Face::MonoRegular, Neuron::Face::MonoMedium})
    {
      Assert::AreEqual(Neuron::FontRenderer::AdvanceOf(U'i', face), Neuron::FontRenderer::AdvanceOf(U'W', face),
                       L"a monospaced face advances the same for every glyph");
    }
    for (const Neuron::Face face : {Neuron::Face::SansRegular, Neuron::Face::SansMedium})
    {
      Assert::IsTrue(Neuron::FontRenderer::AdvanceOf(U'i', face) < Neuron::FontRenderer::AdvanceOf(U'W', face),
                     L"a proportional face must not advance an 'i' as far as a 'W'");
    }
  }

  // The digest rail is 300 wide and spends 14 + 8 + 10 + 14 on margins, the dot and the gap,
  // which leaves 254 -- and at this face's seven-pixel column that is 36 characters. It was 31
  // under the 8x8 font: the rail did not change, the face did.
  //
  // THIRTY-SIX IS A FACT ABOUT THE FACE, NOT ABOUT THE RAIL. The rail is 254 pixels wide and stays
  // 254 pixels wide; what fits in it is the font's to answer (ADR-073). This asserts the answer for
  // the face that is in the binary today, and it is SUPPOSED to move when the face does.
  TEST_METHOD(TheDigestRailFitsThirtySixCharactersOfThisFace)
  {
    constexpr std::string_view LONGER_THAN_THE_RAIL = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
    Assert::AreEqual(static_cast<size_t>(36), Neuron::FontRenderer::PrefixThatFits(LONGER_THAN_THE_RAIL, 254));
    Assert::AreEqual(static_cast<size_t>(36), Neuron::FontRenderer::PrefixThatFits(LONGER_THAN_THE_RAIL, 255),
                     L"a part-character does not fit");
    Assert::AreEqual(static_cast<size_t>(0), Neuron::FontRenderer::PrefixThatFits(LONGER_THAN_THE_RAIL, 6));
  }
};

// Wrapping is the piece that had to exist because the reference's copy is longer than the
// digest rail can hold, and it is the piece most likely to be quietly wrong (ADR-014).
//
// **Every width below is in PIXELS.** These tests counted characters until 2026-09-13, which
// was the same question while every glyph was eight pixels wide (ADR-073). 254 is the digest
// rail; 32 is four characters of the current face.
TEST_CLASS(TextWrapTests)
{
public:
  TEST_METHOD(ShortTextIsOneLine)
  {
    const std::vector<std::string> lines = Neuron::FontRenderer::WrapToWidth("Sealed region opens T60", 254);
    Assert::AreEqual(static_cast<size_t>(1), lines.size());
    Assert::AreEqual(std::string("Sealed region opens T60"), lines[0]);
  }

  TEST_METHOD(ItBreaksOnSpacesAndNeverExceedsTheWidth)
  {
    // The first digest event, at the width the digest rail actually has.
    const std::vector<std::string> lines = Neuron::FontRenderer::WrapToWidth("Outpost present. Their fleet ETA T47. Ours ETA T47.", 254);
    Assert::IsTrue(lines.size() >= 2, L"this line does not fit in the rail");
    for (const std::string& line : lines)
    {
      Assert::IsTrue(Neuron::FontRenderer::MeasurePixels(line) <= 254, L"a wrapped line is wider than the rail");
      Assert::IsTrue(line.front() != ' ' && line.back() != ' ', L"a wrapped line carries no edge space");
    }
  }

  TEST_METHOD(NoWordIsLost)
  {
    constexpr const char* SOURCE = "Lane income foregone: 12/tick. Shipyard Idris idle.";
    std::string rejoined;
    for (const std::string& line : Neuron::FontRenderer::WrapToWidth(SOURCE, 254))
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
    const std::vector<std::string> lines = Neuron::FontRenderer::WrapToWidth("ABCDEFGHIJ", 32);
    Assert::AreEqual(static_cast<size_t>(3), lines.size());
    Assert::AreEqual(std::string("ABCD"), lines[0]);
    Assert::AreEqual(std::string("EFGH"), lines[1]);
    Assert::AreEqual(std::string("IJ"), lines[2]);
  }

  TEST_METHOD(AZeroWidthWrapsToNothingRatherThanLoopingForever)
  {
    Assert::AreEqual(static_cast<size_t>(0), Neuron::FontRenderer::WrapToWidth("anything", 0).size());
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

  // A layer boundary is what lets the mesh pass be drawn between two of one page's shape layers
  // (ADR-103): the take stops at it, the next take carries on, and a page that never marks one is
  // drained exactly as before.
  TEST_METHOD(ALayerBoundaryStopsTheNextTake)
  {
    Neuron::ShapeRenderer shapes;
    shapes.BeginFrame();

    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE); // 6 vertices
    shapes.EndLayer();
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE);
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE); // 12 more

    const Neuron::ShapeRenderer::Batch ground = shapes.TakeUnflushed();
    Assert::AreEqual(static_cast<std::size_t>(6), ground.vertices.size(), L"the first take stops at the boundary");
    Assert::AreEqual(0u, ground.firstVertex);

    const Neuron::ShapeRenderer::Batch over = shapes.TakeUnflushed();
    Assert::AreEqual(static_cast<std::size_t>(12), over.vertices.size(), L"the second take is the rest");
    Assert::AreEqual(6u, over.firstVertex);

    Assert::IsTrue(shapes.TakeUnflushed().vertices.empty(), L"and then nothing is new");
  }

  TEST_METHOD(AnEmptyLayerStillCountsAsATake)
  {
    Neuron::ShapeRenderer shapes;
    shapes.BeginFrame();

    shapes.EndLayer();
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE);

    Assert::IsTrue(shapes.TakeUnflushed().vertices.empty(), L"an empty layer is an empty take, not a skipped one");
    Assert::AreEqual(static_cast<std::size_t>(6), shapes.TakeUnflushed().vertices.size(), L"so the overlay lands on the take after it");
  }

  // A stroked polygon is CLOSED, and that is the whole of what it adds over a run of `Line` calls:
  // the edge somebody forgets is the last one, and a hexagon with a gap in it reads as a rendering
  // fault rather than as a missing statement (ADR-107).
  TEST_METHOD(AStrokedPolygonJoinsItsLastCornerBackToItsFirst)
  {
    Neuron::ShapeRenderer shapes;
    shapes.BeginFrame();

    // A line is a quad, so four corners drawn open would be three of them.
    const std::array<Neuron::ShapeRenderer::ShapePoint, 4> diamond = {
      Neuron::ShapeRenderer::ShapePoint{11.0F, 2.0F}, Neuron::ShapeRenderer::ShapePoint{20.0F, 11.0F},
      Neuron::ShapeRenderer::ShapePoint{11.0F, 20.0F}, Neuron::ShapeRenderer::ShapePoint{2.0F, 11.0F}};
    shapes.StrokePolygon(diamond, Neuron::WHITE, 1.5F);
    Assert::AreEqual(static_cast<std::size_t>(24), shapes.TakeUnflushed().vertices.size(), L"a four-corner polygon is not four edges");
  }

  TEST_METHOD(APolygonOfFewerThanTwoCornersDrawsNothing)
  {
    Neuron::ShapeRenderer shapes;
    shapes.BeginFrame();

    const std::array<Neuron::ShapeRenderer::ShapePoint, 1> alone = {Neuron::ShapeRenderer::ShapePoint{4.0F, 4.0F}};
    shapes.StrokePolygon(alone, Neuron::WHITE);
    shapes.StrokePolygon({}, Neuron::WHITE);
    Assert::IsTrue(shapes.TakeUnflushed().vertices.empty(), L"a polygon with no edges recorded geometry");
  }

  TEST_METHOD(BeginFrameForgetsTheBoundaries)
  {
    Neuron::ShapeRenderer shapes;
    shapes.BeginFrame();
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE);
    shapes.EndLayer();

    shapes.BeginFrame();
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE);
    shapes.FillRect(0.0F, 0.0F, 10.0F, 10.0F, Neuron::WHITE);
    Assert::AreEqual(static_cast<std::size_t>(12), shapes.TakeUnflushed().vertices.size(), L"a boundary from the last frame is gone");
  }
};

// The mesh recorder: world-space solids the shape recorder is not (ADR-103). What is pinned here
// is the one thing a screenshot cannot say and the culler would silently hide: that every face is
// wound counter-clockwise seen from outside, with a normal pointing the same way.
TEST_CLASS(MeshRendererTests)
{
public:
  using MeshVertex = Neuron::MeshRenderer::MeshVertex;

  /// Five tones a test can tell apart by value, in ramp order: shadow, grazed, lit, silhouette,
  /// glint.
  static constexpr Neuron::ColorRamp TONES = {Neuron::DARK_GRAY, Neuron::LIGHT_GRAY, Neuron::WHITE, Neuron::BRIGHT_CYAN,
                                              Neuron::BRIGHT_MAGENTA};

  /// (b - a) x (c - a), the direction a counter-clockwise triangle faces.
  static std::array<float, 3> FaceDirection(const MeshVertex& _a, const MeshVertex& _b, const MeshVertex& _c)
  {
    const float ux = _b.x - _a.x;
    const float uy = _b.y - _a.y;
    const float uz = _b.z - _a.z;
    const float vx = _c.x - _a.x;
    const float vy = _c.y - _a.y;
    const float vz = _c.z - _a.z;
    return {uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
  }

  TEST_METHOD(SegmentCountsAreClampedAndRiseWithRadius)
  {
    Assert::AreEqual(8u, Neuron::MeshRenderer::SegmentsForRadius(1.0F), L"a far ball gets the floor");
    Assert::AreEqual(10u, Neuron::MeshRenderer::SegmentsForRadius(5.0F));
    Assert::AreEqual(12u, Neuron::MeshRenderer::SegmentsForRadius(6.0F));
    Assert::AreEqual(12u, Neuron::MeshRenderer::SegmentsForRadius(100.0F), L"and never more than the ceiling");
    Assert::AreEqual(8u, Neuron::MeshRenderer::RingsForSegments(12), L"a twelve-segment ball is twelve by eight");
  }

  TEST_METHOD(ASphereIsWoundOutwardWithOutwardNormals)
  {
    Neuron::MeshRenderer meshes;
    meshes.BeginFrame();
    meshes.Sphere({10.0F, 20.0F, -30.0F}, 5.0F, TONES, 12);

    const std::span<const MeshVertex> vertices = meshes.Vertices();
    Assert::AreEqual(static_cast<std::size_t>(Neuron::MeshRenderer::SphereVertexCount(12)), vertices.size());
    Assert::AreEqual(static_cast<std::size_t>(504), vertices.size(), L"twelve by eight: two polar bands of triangles, six of quads");

    for (std::size_t index = 0; index < vertices.size(); index += 3)
    {
      const MeshVertex& a = vertices[index];
      const MeshVertex& b = vertices[index + 1];
      const MeshVertex& c = vertices[index + 2];

      const std::array<float, 3> facing = FaceDirection(a, b, c);
      const float outX = (a.x + b.x + c.x) / 3.0F - 10.0F;
      const float outY = (a.y + b.y + c.y) / 3.0F - 20.0F;
      const float outZ = (a.z + b.z + c.z) / 3.0F + 30.0F;
      Assert::IsTrue(facing[0] * outX + facing[1] * outY + facing[2] * outZ > 0.0F, L"a face is counter-clockwise seen from outside");

      for (const MeshVertex& vertex : {a, b, c})
      {
        const float length = std::sqrt(vertex.nx * vertex.nx + vertex.ny * vertex.ny + vertex.nz * vertex.nz);
        Assert::AreEqual(1.0F, length, 0.001F, L"a normal is unit length");
        const float along = vertex.nx * (vertex.x - 10.0F) + vertex.ny * (vertex.y - 20.0F) + vertex.nz * (vertex.z + 30.0F);
        Assert::AreEqual(5.0F, along, 0.001F, L"and points from the centre through its vertex");
        Assert::AreEqual(Neuron::Pack(Neuron::WHITE), vertex.litColor);
        Assert::AreEqual(Neuron::Pack(Neuron::LIGHT_GRAY), vertex.halfLitColor, L"the grazed band's tone did not reach the vertex");
        Assert::AreEqual(Neuron::Pack(Neuron::DARK_GRAY), vertex.darkColor);
        Assert::AreEqual(Neuron::Pack(Neuron::BRIGHT_CYAN), vertex.rimColor, L"the silhouette's tone did not reach the vertex");
        Assert::AreEqual(Neuron::Pack(Neuron::BRIGHT_MAGENTA), vertex.glintColor, L"the glint's tone did not reach the vertex");
      }
    }
  }

  TEST_METHOD(AnOctahedronHasEightFlatFacesFacingOutward)
  {
    Neuron::MeshRenderer meshes;
    meshes.BeginFrame();
    meshes.Octahedron({0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, 3.0F, 3.0F, 5.0F, TONES);

    const std::span<const MeshVertex> vertices = meshes.Vertices();
    Assert::AreEqual(static_cast<std::size_t>(24), vertices.size(), L"eight faces of three");

    for (std::size_t index = 0; index < vertices.size(); index += 3)
    {
      const MeshVertex& a = vertices[index];
      const MeshVertex& b = vertices[index + 1];
      const MeshVertex& c = vertices[index + 2];
      const std::array<float, 3> facing = FaceDirection(a, b, c);
      Assert::IsTrue(facing[0] * a.nx + facing[1] * a.ny + facing[2] * a.nz > 0.0F, L"the face's normal is the way it winds");
      Assert::IsTrue(a.nx == b.nx && a.ny == b.ny && a.nz == b.nz && a.nx == c.nx && a.ny == c.ny && a.nz == c.nz,
                     L"a flat face carries one normal on all three corners");
      const float outward = facing[0] * (a.x + b.x + c.x) + facing[1] * (a.y + b.y + c.y) + facing[2] * (a.z + b.z + c.z);
      Assert::IsTrue(outward > 0.0F, L"and it faces away from the centre");
    }
  }

  // A stem is a column now (ADR-105). Its four sides have to face outward, or the culler removes
  // the ones the camera can see and leaves the ones it cannot.
  // A fleet marker has to say which way the fleet is going (ADR-106), so the solid that replaced the
  // flat arrowhead has to be longer along its heading than across it -- and along the heading it was
  // GIVEN, not along an axis.
  TEST_METHOD(AnOctahedronIsLongestAlongTheWayItPoints)
  {
    Neuron::MeshRenderer meshes;
    meshes.BeginFrame();
    // A heading at forty-five degrees, so an implementation that quietly used an axis fails.
    meshes.Octahedron({0.0F, 0.0F, 0.0F}, {0.7071F, 0.0F, 0.7071F}, 10.0F, 2.0F, 2.0F, TONES);

    float alongMost = 0.0F;
    float acrossMost = 0.0F;
    for (const MeshVertex& vertex : meshes.Vertices())
    {
      // The heading and the axis across it, both unit.
      alongMost = std::max(alongMost, std::abs(vertex.x * 0.7071F + vertex.z * 0.7071F));
      acrossMost = std::max(acrossMost, std::abs(vertex.x * -0.7071F + vertex.z * 0.7071F));
    }

    Assert::AreEqual(10.0F, alongMost, 0.01F, L"the dart does not reach its length along its heading");
    Assert::AreEqual(2.0F, acrossMost, 0.01F, L"the dart is not its width across its heading");
  }

  TEST_METHOD(AColumnStandsOnItsFootWithOutwardFaces)
  {
    Neuron::MeshRenderer meshes;
    meshes.BeginFrame();
    meshes.Column({40.0F, 0.0F, -15.0F}, 30.0F, 1.5F, TONES);

    const std::span<const MeshVertex> vertices = meshes.Vertices();
    Assert::AreEqual(static_cast<std::size_t>(Neuron::MeshRenderer::COLUMN_VERTEX_COUNT), vertices.size(), L"four sides and a cap");

    float lowest = 1000.0F;
    float highest = -1000.0F;
    for (std::size_t index = 0; index < vertices.size(); index += 3)
    {
      const MeshVertex& a = vertices[index];
      const std::array<float, 3> facing = FaceDirection(a, vertices[index + 1], vertices[index + 2]);
      Assert::IsTrue(facing[0] * a.nx + facing[1] * a.ny + facing[2] * a.nz > 0.0F, L"a face's normal is the way it winds");

      // Away from the column's own axis, or straight up for the cap. A side that faced inward
      // would be culled exactly when it should be drawn.
      const float outX = (a.x + vertices[index + 1].x + vertices[index + 2].x) / 3.0F - 40.0F;
      const float outZ = (a.z + vertices[index + 1].z + vertices[index + 2].z) / 3.0F + 15.0F;
      Assert::IsTrue(a.ny > 0.9F || a.nx * outX + a.nz * outZ > 0.0F, L"a side faces away from the axis");

      for (const MeshVertex& vertex : {a, vertices[index + 1], vertices[index + 2]})
      {
        lowest = std::min(lowest, vertex.y);
        highest = std::max(highest, vertex.y);
      }
    }

    Assert::AreEqual(0.0F, lowest, 0.0001F, L"a column starts on the plane");
    Assert::AreEqual(30.0F, highest, 0.0001F, L"and reaches exactly its height");
  }

  TEST_METHOD(TakingHandsOverOnlyWhatIsNew)
  {
    Neuron::MeshRenderer meshes;
    meshes.BeginFrame();
    meshes.Sphere({0.0F, 0.0F, 0.0F}, 1.0F, TONES, 8);
    const std::size_t first = meshes.Vertices().size();

    const Neuron::MeshRenderer::Batch one = meshes.TakeUnflushed();
    Assert::AreEqual(first, one.vertices.size());
    Assert::AreEqual(0u, one.firstVertex);

    meshes.Sphere({0.0F, 0.0F, 0.0F}, 1.0F, TONES, 8);
    const Neuron::MeshRenderer::Batch two = meshes.TakeUnflushed();
    Assert::AreEqual(first, two.vertices.size(), L"the second take is only the second ball");
    Assert::AreEqual(static_cast<std::uint32_t>(first), two.firstVertex, L"and it sits after the first in the frame");
    Assert::IsTrue(meshes.TakeUnflushed().vertices.empty());
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
  /// These tests are written at scale 1 in a window that IS the canvas, so a client pixel is a
  /// canvas pixel and every expected number below is arithmetic on the window origin. The mapping
  /// at other scales is PresentationTests and PointerCanvasTests (ADR-075).
  static constexpr Neuron::Presentation AT_SCALE_ONE = Neuron::Presentation::For(1280, 720, 1);

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
    input.Create(m_window, AT_SCALE_ONE);

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y));
  }

  TEST_METHOD(APointerDownBecomesAScreenPixel)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);
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
    input.Create(m_window, AT_SCALE_ONE);
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
    input.Create(m_window, AT_SCALE_ONE);
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
    input.Create(m_window, AT_SCALE_ONE);

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
// The pointer path at a presentation that is NOT the identity, which is the half of ADR-075 that
// PresentationTests cannot reach: these go through a real window, a real WM_POINTER* lParam and
// real ScreenToClient, and they are what says a button is pressed where it is drawn once the
// canvas stops being the client area.
//
// The window is 1920x1080 at scale 1, so the canvas sits at (320, 180) with a 320x180 border on
// every side. That is the same shape a 4K monitor gives at scale 2 and a phone gives in portrait;
// it is used here because it is the one a test can create on any machine.
TEST_CLASS(PointerCanvasTests)
{
public:
  static constexpr int WINDOW_LEFT = 300;
  static constexpr int WINDOW_TOP = 200;
  static constexpr int SURFACE_WIDTH = 1920;
  static constexpr int SURFACE_HEIGHT = 1080;

  /// A 320x180 letterbox on each side. Written out rather than derived, so that a wrong offset in
  /// Presentation::For cannot agree with a wrong offset here.
  static constexpr float LETTERBOX_X = 320.0F;
  static constexpr float LETTERBOX_Y = 180.0F;

  static constexpr Neuron::Presentation LETTERBOXED = Neuron::Presentation::For(SURFACE_WIDTH, SURFACE_HEIGHT, 1);

  TEST_METHOD_INITIALIZE(CreateHostWindow)
  {
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"NeuronClientTestsCanvasHost";
    RegisterClassExW(&windowClass);

    // WS_POPUP, so the client area starts exactly at the window's top-left, as in
    // PointerInputTests.
    m_window = CreateWindowExW(0, L"NeuronClientTestsCanvasHost", L"", WS_POPUP, WINDOW_LEFT, WINDOW_TOP, SURFACE_WIDTH, SURFACE_HEIGHT,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
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

  [[nodiscard]] static LPARAM PackScreenPoint(int _screenX, int _screenY)
  {
    return static_cast<LPARAM>((static_cast<std::uint32_t>(_screenY & 0xFFFF) << 16) | static_cast<std::uint32_t>(_screenX & 0xFFFF));
  }

  /// A screen point for a given CANVAS pixel: the window origin, plus the letterbox, plus it.
  [[nodiscard]] static LPARAM ScreenPointForCanvas(int _canvasX, int _canvasY)
  {
    return PackScreenPoint(WINDOW_LEFT + static_cast<int>(LETTERBOX_X) + _canvasX, WINDOW_TOP + static_cast<int>(LETTERBOX_Y) + _canvasY);
  }

  TEST_METHOD(ATapReportsTheCanvasPixelItLandedOn)
  {
    Neuron::PointerInput input;
    input.Create(m_window, LETTERBOXED);

    Assert::IsTrue(input.HandleMessage(WM_POINTERDOWN, 1, ScreenPointForCanvas(640, 360)));
    Assert::IsTrue(input.HandleMessage(WM_POINTERUP, 1, ScreenPointForCanvas(640, 360)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(640.0F, x);
    Assert::AreEqual(360.0F, y);
  }

  // The whole point of the stage: the press is reported where the thing was DRAWN, not where the
  // surface says the finger was.
  TEST_METHOD(TheCanvasOriginIsNotTheSurfaceOrigin)
  {
    Neuron::PointerInput input;
    input.Create(m_window, LETTERBOXED);

    Assert::IsTrue(input.HandleMessage(WM_POINTERDOWN, 1, ScreenPointForCanvas(0, 0)));
    Assert::IsTrue(input.HandleMessage(WM_POINTERUP, 1, ScreenPointForCanvas(0, 0)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.TakeClick(x, y));
    Assert::AreEqual(0.0F, x);
    Assert::AreEqual(0.0F, y);
  }

  // A press out in the black border starts nothing AND is not consumed, so the window's default
  // handling still happens. Both halves matter: consuming it would swallow a click that belongs to
  // nobody, and recording it would put a contact at a negative canvas coordinate.
  TEST_METHOD(APressInTheLetterboxIsNeitherRecordedNorConsumed)
  {
    Neuron::PointerInput input;
    input.Create(m_window, LETTERBOXED);

    Assert::IsFalse(input.HandleMessage(WM_POINTERDOWN, 1, PackScreenPoint(WINDOW_LEFT + 10, WINDOW_TOP + 10)));
    Assert::IsTrue(input.HandleMessage(WM_POINTERUP, 1, PackScreenPoint(WINDOW_LEFT + 10, WINDOW_TOP + 10)));

    float x = 0.0F;
    float y = 0.0F;
    Assert::IsFalse(input.TakeClick(x, y));
  }

  // And the other half of that rule. A drag that STARTED on the canvas keeps going when it leaves,
  // because a gesture that stuck at the edge of the canvas would be a defect a player can feel.
  TEST_METHOD(ADragThatLeavesTheCanvasKeepsDragging)
  {
    Neuron::PointerInput input;
    input.Create(m_window, LETTERBOXED);

    Assert::IsTrue(input.HandleMessage(WM_POINTERDOWN, 1, ScreenPointForCanvas(40, 360)));
    // Out past the left edge of the canvas, into the letterbox: canvas x is negative here.
    Assert::IsTrue(
      input.HandleMessage(WM_POINTERUPDATE, 1, PackScreenPoint(WINDOW_LEFT + 10, WINDOW_TOP + static_cast<int>(LETTERBOX_Y) + 360)));

    Neuron::PointerInput::Drag drag = {};
    Assert::IsTrue(input.TakeDrag(drag));

    // From canvas x=40 to canvas x=(10 - 320) = -310, so the movement is -350 canvas pixels.
    Assert::AreEqual(-350.0F, drag.deltaXPixels);
    Assert::AreEqual(40.0F, drag.originXPixels);
    Assert::AreEqual(360.0F, drag.originYPixels);
  }

  // Hover is a state, and out in the letterbox the state is "nothing is under the pointer".
  TEST_METHOD(HoverInTheLetterboxIsNoHoverAtAll)
  {
    Neuron::PointerInput input;
    input.Create(m_window, LETTERBOXED);

    // WM_MOUSEMOVE's lParam is CLIENT pixels already, so these are surface coordinates.
    Assert::IsFalse(input.HandleMessage(WM_MOUSEMOVE, 0, PackScreenPoint(320 + 100, 180 + 50)));
    float x = 0.0F;
    float y = 0.0F;
    Assert::IsTrue(input.PointerPosition(x, y));
    Assert::AreEqual(100.0F, x);
    Assert::AreEqual(50.0F, y);

    Assert::IsFalse(input.HandleMessage(WM_MOUSEMOVE, 0, PackScreenPoint(10, 10)));
    Assert::IsFalse(input.PointerPosition(x, y));
  }

private:
  HWND m_window = nullptr;
};

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

  /// A world point through the matrix, as the GPU would take it: row vector times row-major
  /// matrix, then the homogeneous divide, then the viewport transform onto the pane.
  struct ThroughTheMatrix
  {
    float xPixels;
    float yPixels;
    float depthZeroToOne;
    float w;
  };

  static ThroughTheMatrix PushThrough(const std::array<float, 16>& _matrix, const Neuron::OrbitCamera::WorldPoint& _point)
  {
    const std::array<float, 4> row = {_point.x, _point.y, _point.z, 1.0F};
    std::array<float, 4> clip = {};
    for (std::size_t column = 0; column < 4; ++column)
    {
      for (std::size_t inner = 0; inner < 4; ++inner)
      {
        clip[column] += row[inner] * _matrix[inner * 4 + column];
      }
    }
    const float ndcX = clip[0] / clip[3];
    const float ndcY = clip[1] / clip[3];
    return ThroughTheMatrix{PANE_X + (ndcX * 0.5F + 0.5F) * PANE_WIDTH, PANE_Y + (0.5F - ndcY * 0.5F) * PANE_HEIGHT, clip[2] / clip[3],
                            clip[3]};
  }

  // The matrix and `Project` are two statements of one camera, and this is what keeps them one
  // (ADR-103). The mesh pass puts a ball where the matrix says and the label where `Project` says;
  // the day they drift, every station's name floats away from its station. Three points off every
  // axis, at several orientations, to a hundredth of a pixel.
  TEST_METHOD(TheMatrixLandsOnTheSamePixelAsProject)
  {
    constexpr std::array<Neuron::OrbitCamera::WorldPoint, 3> POINTS = {
      Neuron::OrbitCamera::WorldPoint{120.0F, 30.0F, -80.0F},
      Neuron::OrbitCamera::WorldPoint{-300.0F, 0.0F, 200.0F},
      Neuron::OrbitCamera::WorldPoint{45.0F, 60.0F, 310.0F},
    };

    for (const float yaw : {0.0F, 1.3F, -2.5F})
    {
      for (const float pitch : {0.2F, 0.62F, 1.3F})
      {
        const Neuron::OrbitCamera camera = MakeCamera(yaw, pitch);
        const std::array<float, 16> matrix = camera.ViewProjection();

        for (const Neuron::OrbitCamera::WorldPoint& point : POINTS)
        {
          const Neuron::OrbitCamera::ScreenPoint expected = camera.Project(point);
          const ThroughTheMatrix actual = PushThrough(matrix, point);

          Assert::IsTrue(expected.visible);
          Assert::AreEqual(expected.xPixels, actual.xPixels, 0.01F, L"x drifted between the matrix and Project");
          Assert::AreEqual(expected.yPixels, actual.yPixels, 0.01F, L"y drifted between the matrix and Project");
          Assert::AreEqual(expected.depth, actual.w, 0.01F, L"the divide is the same divide");
          Assert::IsTrue(actual.depthZeroToOne > 0.0F && actual.depthZeroToOne < 1.0F, L"a framed point is inside the depth range");
        }
      }
    }
  }

  // The depth range is Direct3D's: nearer is smaller, the near plane is zero and the far plane is
  // one, so `DepthState(true)`'s LESS puts the nearer ball in front.
  TEST_METHOD(TheMatrixMapsDepthNearToZeroAndFarToOne)
  {
    const Neuron::OrbitCamera camera = MakeCamera(0.0F, 0.6F, 1000.0F);
    const std::array<float, 16> matrix = camera.ViewProjection();
    const Neuron::OrbitCamera::WorldPoint eye = camera.Position();

    // Straight ahead of the eye, by the distance the planes sit at.
    const auto ahead = [&](float _depth)
    {
      const float scale = _depth / 1000.0F;
      return Neuron::OrbitCamera::WorldPoint{eye.x * (1.0F - scale), eye.y * (1.0F - scale), eye.z * (1.0F - scale)};
    };

    // A thousandth: the near point is one unit in front of an eye a thousand units out, which is
    // about where float's precision on the subtraction sits.
    Assert::AreEqual(0.0F, PushThrough(matrix, ahead(Neuron::OrbitCamera::NEAR_PLANE_WORLD_UNITS)).depthZeroToOne, 0.001F);
    Assert::AreEqual(1.0F, PushThrough(matrix, ahead(1000.0F * Neuron::OrbitCamera::FAR_PLANE_IN_DISTANCES)).depthZeroToOne, 0.001F);

    const float nearer = PushThrough(matrix, ahead(900.0F)).depthZeroToOne;
    const float further = PushThrough(matrix, ahead(1100.0F)).depthZeroToOne;
    Assert::IsTrue(nearer < further, L"nearer is smaller");

    // Behind the eye has a negative w, which is the matrix's way of saying what `visible` says.
    Assert::IsTrue(PushThrough(matrix, ahead(-10.0F)).w < 0.0F);
    Assert::IsFalse(camera.Project(ahead(-10.0F)).visible);
  }
};

// Tap and drag are the same gesture until they are not, and telling them apart is the whole of
// what this suite is about. Getting it wrong is not subtle: a tap that fires on press opens a
// panel every time the player tries to rotate the map, and a drag that swallows short movements
// makes the screen feel stuck (ADR-016).
TEST_CLASS(DragInputTests)
{
public:
  /// These tests are written at scale 1 in a window that IS the canvas, so a client pixel is a
  /// canvas pixel and every expected number below is arithmetic on the window origin. The mapping
  /// at other scales is PresentationTests and PointerCanvasTests (ADR-075).
  static constexpr Neuron::Presentation AT_SCALE_ONE = Neuron::Presentation::For(1280, 720, 1);

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
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

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
  /// These tests are written at scale 1 in a window that IS the canvas, so a client pixel is a
  /// canvas pixel and every expected number below is arithmetic on the window origin. The mapping
  /// at other scales is PresentationTests and PointerCanvasTests (ADR-075).
  static constexpr Neuron::Presentation AT_SCALE_ONE = Neuron::Presentation::For(1280, 720, 1);

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
    input.Create(m_window, AT_SCALE_ONE);
    Assert::AreEqual(0, input.TakeZoomSteps());
  }

  TEST_METHOD(AWheelNotchIsOneStep)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

    Assert::IsFalse(input.HandleMessage(WM_MOUSEWHEEL, PackWheel(0, WHEEL_DELTA), 0));
    Assert::AreEqual(0, input.TakeZoomSteps());
  }

  TEST_METHOD(TakingTheZoomClearsIt)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);
    input.HandleMessage(WM_POINTERWHEEL, PackWheel(1, WHEEL_DELTA), 0);

    Assert::AreEqual(1, input.TakeZoomSteps());
    Assert::AreEqual(0, input.TakeZoomSteps(), L"a zoom is delivered once");
  }

  TEST_METHOD(NotchesInOneFrameAddUp)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

    StartPinch(input, 100.0F);
    Assert::AreEqual(0, input.TakeZoomSteps(), L"putting two fingers down is not yet a zoom");

    MovePinch(input, 100.0F * Neuron::PointerInput::PINCH_STEP_RATIO);
    Assert::AreEqual(1, input.TakeZoomSteps());
  }

  TEST_METHOD(PinchingTwoContactsTogetherZoomsOut)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

    StartPinch(input, 200.0F);
    MovePinch(input, 200.0F / Neuron::PointerInput::PINCH_STEP_RATIO);

    Assert::AreEqual(-1, input.TakeZoomSteps());
  }

  // One update can carry a large jump, from a fast pinch or a frame that was missed. Banking a
  // single step for it would make the zoom lag the fingers.
  TEST_METHOD(ALargeSpreadInOneUpdateBanksSeveralSteps)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

    input.HandleMessage(WM_POINTERDOWN, PackPointer(1), PackScreenPoint(WINDOW_LEFT + 200, WINDOW_TOP + 300));
    float x = 0.0F;
    float y = 0.0F;

    input.HandleMessage(WM_POINTERDOWN, PackPointer(2), PackScreenPoint(WINDOW_LEFT + 600, WINDOW_TOP + 300));
    Assert::IsFalse(input.TakeClick(x, y), L"a pinch must not also fly the ship somewhere");
  }

  TEST_METHOD(OneContactStillTapsNormally)
  {
    Neuron::PointerInput input;
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

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
    input.Create(m_window, AT_SCALE_ONE);

    StartPinch(input, 100.0F);
    input.HandleMessage(WM_POINTERDOWN, PackPointer(3), PackScreenPoint(WINDOW_LEFT + 1200, WINDOW_TOP + 700));
    Assert::AreEqual(0, input.TakeZoomSteps());

    MovePinch(input, 100.0F * Neuron::PointerInput::PINCH_STEP_RATIO);
    Assert::AreEqual(1, input.TakeZoomSteps(), L"the original two still drive the pinch");
  }

private:
  HWND m_window = nullptr;
};

// ---- The one field this tree has ----------------------------------------------------------------
//
// ADR-014 said there was no text input at all; ADR-034 amended it for the join screen, because the
// product is a mobile client and a phone has no command line. These tests are what keep the
// amendment small: everything asserted here is something the field does, and the list is short on
// purpose.

TEST_CLASS(TextFieldTests)
{
public:
  TEST_METHOD(TypingPutsCharactersAtTheCaret)
  {
    Neuron::TextField field;
    for (const char letter : std::string("alpha"))
    {
      field.Type(letter);
    }
    Assert::AreEqual(std::string("alpha"), field.Text());
    Assert::AreEqual(std::size_t{5}, field.Caret());

    field.CaretHome();
    field.Type('>');
    Assert::AreEqual(std::string(">alpha"), field.Text(), L"a caret at the start inserts at the start");
    Assert::AreEqual(std::size_t{1}, field.Caret());
  }

  // The font is 96 glyphs (ADR-014), so a character outside them is text the screen cannot draw.
  // Storing it would put a field's contents and its appearance permanently out of step.
  TEST_METHOD(OnlyPrintableAsciiIsAccepted)
  {
    Neuron::TextField field;
    field.Type('A');
    field.Type('\n');
    field.Type('\t');
    field.Type(static_cast<char>(0x1B));
    field.Type(static_cast<char>(200));
    field.Type('z');

    Assert::AreEqual(std::string("Az"), field.Text());
  }

  // A refusal, not a truncation. A field that dropped the last character typed would look like a
  // dropped keystroke, and the player would type it again.
  TEST_METHOD(TheLimitRefusesRatherThanTruncates)
  {
    Neuron::TextField field{4};
    for (const char letter : std::string("abcdefg"))
    {
      field.Type(letter);
    }

    Assert::AreEqual(std::string("abcd"), field.Text());
    Assert::AreEqual(std::size_t{4}, field.Caret());
  }

  TEST_METHOD(BackspaceAndDeleteTakeDifferentSides)
  {
    Neuron::TextField field;
    field.Set("abcd");
    field.CaretLeft();
    field.CaretLeft();

    field.Backspace();
    Assert::AreEqual(std::string("acd"), field.Text(), L"backspace takes what is behind the caret");

    field.Delete();
    Assert::AreEqual(std::string("ad"), field.Text(), L"delete takes what is in front of it");
  }

  TEST_METHOD(TheCaretStopsAtBothEnds)
  {
    Neuron::TextField field;
    field.Set("ab");

    field.CaretRight();
    field.CaretRight();
    Assert::AreEqual(std::size_t{2}, field.Caret(), L"and does not run off the end");

    field.CaretHome();
    field.CaretLeft();
    Assert::AreEqual(std::size_t{0}, field.Caret());

    field.CaretEnd();
    Assert::AreEqual(std::size_t{2}, field.Caret());
  }

  // Backspacing an empty field, deleting past the end: both are things a player does without
  // thinking and neither may do anything at all.
  TEST_METHOD(AnEmptyFieldSurvivesEveryKey)
  {
    Neuron::TextField field;
    field.Backspace();
    field.Delete();
    field.CaretLeft();
    field.CaretRight();
    field.CaretHome();
    field.CaretEnd();

    Assert::IsTrue(field.Empty());
    Assert::AreEqual(std::size_t{0}, field.Caret());
  }

  // The mask is a display rule and not a storage one: a token that could not be read back is a
  // token nobody can check they typed right, and ADR-029 says it is a seat rather than a secret.
  TEST_METHOD(MaskingHidesTheTextWithoutLosingIt)
  {
    Neuron::TextField field;
    field.Set("9GZ2-4T");

    Assert::AreEqual(std::string("*******"), field.Shown(true));
    Assert::AreEqual(std::string("9GZ2-4T"), field.Shown(false), L"SHOW has to show the real thing");
    Assert::AreEqual(std::string("9GZ2-4T"), field.Text());

    // One dot per character, so revealing it does not move the caret or change the width.
    Assert::AreEqual(field.Shown(true).size(), field.Shown(false).size());
  }

  TEST_METHOD(SetKeepsOnlyWhatCanBeTypedAndFits)
  {
    Neuron::TextField field{5};
    field.Set("ab\ncdefgh");

    Assert::AreEqual(std::string("abcde"), field.Text(), L"a remembered value gets the same rules as a typed one");
    Assert::AreEqual(std::size_t{5}, field.Caret(), L"and the caret waits at the end of it");
  }
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
      Assert::IsTrue(star.brightness >= 0.0F && star.brightness <= 1.0F);
      Assert::IsTrue(star.radiusPixels >= 0.7F && star.radiusPixels <= 1.2F);
    }
  }

  // Size follows brightness rather than being drawn beside it. A bright small star and a dim large
  // one are both things the eye reads as a contradiction, and two independent draws would produce
  // both.
  TEST_METHOD(SizeFollowsBrightness)
  {
    const Neuron::Starfield sky;

    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const float expected = 0.7F + 0.5F * star.brightness;
      Assert::AreEqual(expected, star.radiusPixels, 0.0005F);
    }
  }

  // Most stars faint, a few bright. A flat draw would give a field of uniformly middling dots,
  // which reads as a texture rather than as a sky -- so this asserts the shape of the distribution
  // and not merely its range.
  TEST_METHOD(MostStarsAreFaint)
  {
    const Neuron::Starfield sky{Neuron::Starfield::DEFAULT_SEED, 20000};

    std::int32_t faint = 0;
    std::int32_t bright = 0;
    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      faint += star.brightness < 0.4F ? 1 : 0;
      bright += star.brightness > 0.6F ? 1 : 0;
    }

    // Brightness is drawn to the power of one and a half, so about 54% fall under 0.4 and about
    // 29% over 0.6. Wide bounds: what is being asserted is the skew, not the exact curve.
    Assert::IsTrue(faint > bright * 3 / 2, L"a sky is mostly faint stars");
    Assert::IsTrue(bright > 0, L"but not uniformly dim ones");
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
    // No band, because this is a test about the SAMPLING rather than about the look: a deliberate
    // concentration towards the galactic plane would mask exactly the accidental one being looked
    // for here.
    const Neuron::Starfield sky{Neuron::Starfield::DEFAULT_SEED, 20000, 0.0F};

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

  // The band, which is the deliberate unevenness. Measured over equal solid angle, the plane
  // carries about 2.2 times the density of the poles -- enough to read as a band, not so much that
  // it becomes a stripe with empty sky either side.
  TEST_METHOD(TheGalacticPlaneIsDenserThanItsPoles)
  {
    const Neuron::Starfield sky{Neuron::Starfield::DEFAULT_SEED, 20000};

    // Twenty degrees each side of the plane, against the two twenty-degree caps at its poles. The
    // areas differ, so the counts are divided by them before being compared.
    constexpr float TWENTY_DEGREES = 0.34906585F;
    const float planeArea = std::sin(TWENTY_DEGREES);
    const float poleArea = 1.0F - std::cos(TWENTY_DEGREES);

    std::int32_t nearPlane = 0;
    std::int32_t nearPoles = 0;
    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const float offPlane = std::abs(star.x * Neuron::Starfield::GALACTIC_POLE_X + star.y * Neuron::Starfield::GALACTIC_POLE_Y +
                                      star.z * Neuron::Starfield::GALACTIC_POLE_Z);
      nearPlane += offPlane < std::sin(TWENTY_DEGREES) ? 1 : 0;
      nearPoles += offPlane > std::cos(TWENTY_DEGREES) ? 1 : 0;
    }

    Assert::IsTrue(nearPoles > 0, L"a band is not a band if the rest of the sky is empty");

    const float density = (static_cast<float>(nearPlane) / planeArea) / (static_cast<float>(nearPoles) / poleArea);
    Assert::IsTrue(density > 2.5F && density < 6.0F, L"the band has to be visible without being a stripe");
  }

  // Turning the band off gives back an even sphere. This is what says the concentration is a choice
  // rather than something the sampling does on its own.
  TEST_METHOD(NoBandIsAnEvenSky)
  {
    const Neuron::Starfield sky{Neuron::Starfield::DEFAULT_SEED, 20000, 0.0F};

    constexpr float TWENTY_DEGREES = 0.34906585F;
    std::int32_t nearPlane = 0;
    std::int32_t nearPoles = 0;
    for (const Neuron::Starfield::Star& star : sky.Stars())
    {
      const float offPlane = std::abs(star.x * Neuron::Starfield::GALACTIC_POLE_X + star.y * Neuron::Starfield::GALACTIC_POLE_Y +
                                      star.z * Neuron::Starfield::GALACTIC_POLE_Z);
      nearPlane += offPlane < std::sin(TWENTY_DEGREES) ? 1 : 0;
      nearPoles += offPlane > std::cos(TWENTY_DEGREES) ? 1 : 0;
    }

    const float density =
      (static_cast<float>(nearPlane) / std::sin(TWENTY_DEGREES)) / (static_cast<float>(nearPoles) / (1.0F - std::cos(TWENTY_DEGREES)));
    Assert::IsTrue(density > 0.8F && density < 1.25F, L"with the band off, every part of the sky is the same density");
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
      Assert::IsTrue(ratio > 0.3F && ratio < 3.5F, L"one edge of the pane is systematically emptier than the other");
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

    // Measured across the whole tilt and a full turn: 13 at the emptiest angle, 89 at the fullest,
    // averaging about thirty -- which is what the authored field showed and what DEFAULT_COUNT was
    // chosen to reproduce. The spread is wide because of the band: looking along it shows far more
    // than looking across it, which is the whole point of having one.
    Assert::IsTrue(fewest >= 10, L"the sky must never be nearly empty from any angle");
    Assert::IsTrue(most <= 110, L"nor crowded past being a backdrop");
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
