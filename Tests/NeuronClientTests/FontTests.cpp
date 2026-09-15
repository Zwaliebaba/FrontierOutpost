// FontTests.cpp -- the baked font: its atlas, its UTF-8, its metrics, and the wrap (ADR-073, ADR-074).

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

} // namespace NeuronClientTests
