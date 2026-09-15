// TextFieldTests.cpp -- the one field this tree has.

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

} // namespace NeuronClientTests
