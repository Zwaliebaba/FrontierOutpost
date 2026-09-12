// JoinPageTests.cpp -- screen 03, the only screen in this game that types.
//
// It is the first thing a player sees and the last screen to get a test, which is the wrong way
// round: everything else in this client is a tap, and this is the one place a person can put a
// character in the wrong field, hold backspace, or hit Enter with half an address in front of them.
// ADR-034 amended ADR-014's interface layer for this screen alone, and `Neuron::TextField` is the
// whole of that amendment.
//
// The keyboard needs no seam of its own -- `HandleTyped` and `HandleKey` are ordinary calls. The
// TAPS need one, for the reason in TapTests.cpp: the hit list is built while the page draws
// (ADR-041).

#include "pch.h"
#include "CppUnitTest.h"

#include "JoinPage.h"

#include <functional>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

namespace
{

using Key = Neuron::KeyboardInput::Key;

constexpr std::int32_t SCREEN_WIDTH = 1280;
constexpr std::int32_t SCREEN_HEIGHT = 720;
constexpr std::int32_t STEP = 8;

struct Headless
{
  Neuron::ShapeRenderer shapes;
  Neuron::FontRenderer text;

  Headless()
  {
    shapes.CreateHeadless();
    text.CreateHeadless();
  }

  void Draw(Lockstep::JoinPage& _page)
  {
    shapes.BeginFrame(0);
    text.BeginFrame(0);
    _page.DrawWorld(shapes, text);
    _page.DrawInterface(shapes, text);
  }
};

/// Sweeps the screen until `_done`, redrawing only after a tap that landed on something.
[[nodiscard]] bool SweepFor(Lockstep::JoinPage& _page, Headless& _renderers, const std::function<bool()>& _done)
{
  bool stale = true;
  for (std::int32_t y = 0; y < SCREEN_HEIGHT; y += STEP)
  {
    for (std::int32_t x = 0; x < SCREEN_WIDTH; x += STEP)
    {
      if (stale)
      {
        _renderers.Draw(_page);
      }
      stale = _page.HandleTap(static_cast<float>(x), static_cast<float>(y));
      if (_done())
      {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] Lockstep::JoinPage Offered()
{
  Lockstep::JoinPage page;
  page.Offer("127.0.0.1:7341", "5H7K-K2MU");
  return page;
}

} // namespace

TEST_CLASS(JoinPageTypingTests)
{
public:
  TEST_METHOD(TheCaretStartsInTheTokenBecauseTheServerIsUsuallyRight)
  {
    // The server field carries whatever the command line or the last session offered and is
    // usually already correct; the token is the thing the player was handed and came here to type.
    Lockstep::JoinPage page = Offered();
    page.HandleTyped("Z");

    Assert::AreEqual(std::string{"127.0.0.1:7341"}, page.Server(), L"typing reached the server field");
    Assert::AreEqual(std::string{"5H7K-K2MUZ"}, page.Token());
  }

  TEST_METHOD(TabMovesBetweenTheTwoFields)
  {
    Lockstep::JoinPage page = Offered();
    page.HandleKey(Key::Tab);
    page.HandleTyped("!");
    Assert::AreEqual(std::string{"127.0.0.1:7341!"}, page.Server(), L"Tab did not move the caret to the server");

    page.HandleKey(Key::Tab);
    page.HandleTyped("?");
    Assert::AreEqual(std::string{"5H7K-K2MU?"}, page.Token(), L"Tab did not come back to the token");
  }

  TEST_METHOD(BackspaceTakesTheLastCharacterOfTheFocusedField)
  {
    Lockstep::JoinPage page = Offered();
    page.HandleKey(Key::Backspace);
    Assert::AreEqual(std::string{"5H7K-K2M"}, page.Token());
    Assert::AreEqual(std::string{"127.0.0.1:7341"}, page.Server(), L"backspace reached the wrong field");
  }

  TEST_METHOD(EnterIsJoin)
  {
    // A form with one button and a keyboard in front of it should not need the mouse to finish.
    Lockstep::JoinPage page = Offered();
    page.HandleKey(Key::Enter);
    Assert::IsTrue(page.TakeJoinRequest(), L"Enter did not ask to join");
  }

  TEST_METHOD(AJoinRequestIsTakenOnlyOnce)
  {
    // A held finger, or a held Enter, must not reconnect on every frame.
    Lockstep::JoinPage page = Offered();
    page.HandleKey(Key::Enter);
    Assert::IsTrue(page.TakeJoinRequest());
    Assert::IsFalse(page.TakeJoinRequest(), L"one Enter asked to join twice");
  }

  TEST_METHOD(HalfAFormCannotJoin)
  {
    // Neither field may be empty. A `Hello` with no token is a message the server refuses and a
    // round trip the player learns nothing from.
    Lockstep::JoinPage empty;
    empty.Offer("127.0.0.1:7341", "");
    empty.HandleKey(Key::Enter);
    Assert::IsFalse(empty.TakeJoinRequest(), L"an empty token asked to join");

    Lockstep::JoinPage noServer;
    noServer.Offer("", "5H7K-K2MU");
    noServer.HandleKey(Key::Enter);
    Assert::IsFalse(noServer.TakeJoinRequest(), L"an empty server asked to join");
  }

  TEST_METHOD(AConnectingScreenTakesNoMoreInput)
  {
    // The fields go inert while a `Hello` is on its way, because a token edited halfway through an
    // attempt is a token that does not match the attempt the answer belongs to.
    Lockstep::JoinPage page = Offered();
    page.SetStatus(Lockstep::JoinPage::Status::Connecting);

    page.HandleTyped("XYZ");
    page.HandleKey(Key::Backspace);
    Assert::AreEqual(std::string{"5H7K-K2MU"}, page.Token(), L"a connecting screen accepted typing");

    page.HandleKey(Key::Enter);
    Assert::IsFalse(page.TakeJoinRequest(), L"a connecting screen asked to join again");
  }

  TEST_METHOD(FocusTokenPutsTheCaretBackWhereTheTypoIs)
  {
    // ADR-038's `EDIT TOKEN`: the commonest refusal is a typo, and it is nearly always in one of
    // the two fields rather than either.
    Lockstep::JoinPage page = Offered();
    page.HandleKey(Key::Tab);
    page.FocusToken();
    page.HandleTyped("9");

    Assert::AreEqual(std::string{"5H7K-K2MU9"}, page.Token(), L"the caret was not put back in the token");
  }

  TEST_METHOD(AskToJoinNeedsNoTapAndNoKey)
  {
    // ADR-038's `RETRY`, which goes through the one path that opens a connection rather than a
    // second copy of it.
    Lockstep::JoinPage page = Offered();
    page.AskToJoin();
    Assert::IsTrue(page.TakeJoinRequest());
  }
};

TEST_CLASS(JoinPageTapTests)
{
public:
  TEST_METHOD(TheServerFieldCanBeFocusedByTappingIt)
  {
    Lockstep::JoinPage page = Offered();
    Headless renderers;

    // Typed into after every tap, so the sweep stops at the first thing that moved the caret.
    const bool focused = SweepFor(page, renderers,
                                  [&page]
                                  {
                                    page.HandleTyped("#");
                                    const bool moved = page.Server().find('#') != std::string::npos;
                                    return moved;
                                  });
    Assert::IsTrue(focused, L"nothing on the screen puts the caret in the server field");
  }

  TEST_METHOD(ShowRevealsTheToken)
  {
    // The token is masked because it is read off one screen and typed into another, usually in a
    // room with other people in it. `SHOW` is how somebody checks what they typed.
    Lockstep::JoinPage page = Offered();
    Assert::IsFalse(page.TokenIsRevealed(), L"the token starts revealed");

    Headless renderers;
    const bool revealed = SweepFor(page, renderers, [&page] { return page.TokenIsRevealed(); });
    Assert::IsTrue(revealed, L"nothing on the screen reveals the token");

    // And it does not alter what is in the field, which is the failure that would look like a fix.
    Assert::AreEqual(std::string{"5H7K-K2MU"}, page.Token(), L"revealing the token changed it");
  }

  TEST_METHOD(TheJoinButtonAsksToJoin)
  {
    Lockstep::JoinPage page = Offered();
    Headless renderers;

    const bool asked = SweepFor(page, renderers, [&page] { return page.TakeJoinRequest(); });
    Assert::IsTrue(asked, L"nothing on the screen asks to join");
  }

  TEST_METHOD(AnEmptyFormOffersNoJoinButtonAtAll)
  {
    // Not drawn-and-refused: the button is not there. A control that takes a tap and does nothing
    // teaches a player that the screen is broken rather than that the form is incomplete.
    Lockstep::JoinPage page;
    page.Offer("127.0.0.1:7341", "");

    Headless renderers;
    const bool asked = SweepFor(page, renderers, [&page] { return page.TakeJoinRequest(); });
    Assert::IsFalse(asked, L"a form with no token could still be submitted by tapping");
  }
};

} // namespace LockstepTests
