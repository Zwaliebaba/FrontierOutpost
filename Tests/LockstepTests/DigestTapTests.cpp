// DigestTapTests.cpp -- the digest rail, tapped: overflow and paging (ADR-080), and a proposal answered
// from its card (ADR-068).

#include "pch.h"
#include "CppUnitTest.h"

#include "Headless.h"

#include "DigestView.h"
#include "MainPage.h"
#include "SnapshotView.h"

#include <algorithm>
#include <format>
#include <functional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

// The digest is taller than the column it is drawn in, and nothing scrolls (ADR-052 option C). An
// actor card collapses and the stack pages (ADR-061), and both are controls that have to be pressed.
TEST_CLASS(DigestOverflowTapTests)
{
public:
  /// A digest event about `_actor`, added to a real state so the page has a galaxy under it.
  static void AddEvent(Lockstep::MatchState& _state, Lockstep::OwnerId _actor, std::string _title)
  {
    Lockstep::DigestEvent event;
    event.kind = Lockstep::EventKind::Economy;
    event.actor = _actor;
    event.title = std::move(_title);
    event.detail = "Something happened, and here is the line that says so.";
    _state.digest.push_back(std::move(event));
  }

  /// Sweeps the digest column and reports where the tap that satisfied `_done` was.
  [[nodiscard]] static bool SweepDigest(Lockstep::MainPage& _page, Headless& _renderers, const std::function<bool()>& _done,
                                        std::int32_t& _outX, std::int32_t& _outY)
  {
    bool stale = true;
    for (std::int32_t y = TOP_BAR; y < SCREEN_HEIGHT; y += STEP)
    {
      for (std::int32_t x = 0; x < static_cast<std::int32_t>(Lockstep::MainPage::DIGEST_WIDTH); x += STEP)
      {
        if (stale)
        {
          _renderers.Begin();
          DrawPage(_page, _renderers);
        }
        stale = _page.HandleTap(static_cast<float>(x), static_cast<float>(y));
        if (_done())
        {
          _outX = x;
          _outY = y;
          return true;
        }
      }
    }
    return false;
  }

  TEST_METHOD(AnActorCardOpensAndClosesFromItsTitle)
  {
    // An actor card is the only card whose body is a LIST, and the only one that can be dropped
    // without losing a fact: the title still names the rival and the stamp still counts them.
    const auto simulation = PlayedMatch(2);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.digest.clear();
    AddEvent(state, 1, "Halvorsen took Pell");
    AddEvent(state, 1, "Halvorsen proposes a lane");

    Lockstep::MainPage page;
    page.Create(std::move(state));
    Assert::IsTrue(page.ExpandedActor() == Lockstep::NOBODY, L"a digest opened with a card already open");

    Headless renderers;
    std::int32_t x = 0;
    std::int32_t y = 0;
    Assert::IsTrue(
      SweepDigest(page, renderers, [&page] { return page.ExpandedActor() == 1; }, x, y), L"nothing in the digest opens an actor card");

    renderers.Begin();
    DrawPage(page, renderers);
    (void)page.HandleTap(static_cast<float>(x), static_cast<float>(y));
    Assert::IsTrue(page.ExpandedActor() == Lockstep::NOBODY, L"the same title did not close the card again");
  }

  TEST_METHOD(OnlyOneActorCardIsOpenAtATime)
  {
    const auto simulation = PlayedMatch(2);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.digest.clear();
    AddEvent(state, 1, "Halvorsen took Pell");
    AddEvent(state, 1, "Halvorsen proposes a lane");
    AddEvent(state, 2, "Sorne took Dothan");
    AddEvent(state, 2, "Sorne proposes a lane");

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Headless renderers;
    std::int32_t x = 0;
    std::int32_t y = 0;
    Assert::IsTrue(SweepDigest(page, renderers, [&page] { return page.ExpandedActor() != Lockstep::NOBODY; }, x, y));
    const Lockstep::OwnerId first = page.ExpandedActor();

    // Another card OPEN, not merely the first one shut: the sweep passes over the first title again
    // on its way down and closing it is not what is being claimed here.
    Assert::IsTrue(
      SweepDigest(
        page, renderers, [&page, first] { return page.ExpandedActor() != first && page.ExpandedActor() != Lockstep::NOBODY; }, x, y),
      L"the second actor card could not be opened");
    Assert::IsTrue(page.ExpandedActor() != first, L"two actor cards were open at once");
  }

  /// Twenty cards is more than the column holds however they are laid out, which is the condition
  /// the band exists for.
  [[nodiscard]] static Lockstep::MatchState ATallDigest()
  {
    const auto simulation = PlayedMatch(2);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.digest.clear();
    for (std::int32_t index = 0; index < 20; ++index)
    {
      AddEvent(state, Lockstep::NOBODY, std::format("Production +{}", index + 1));
    }
    return state;
  }

  TEST_METHOD(ADigestTallerThanTheColumnPages)
  {
    // What the band must never do is drop a card: the top of the stack has to be reachable again.
    Lockstep::MainPage page;
    page.Create(ATallDigest());
    Assert::AreEqual(std::size_t{0}, page.DigestTop());

    Headless renderers;
    std::int32_t x = 0;
    std::int32_t y = 0;
    Assert::IsTrue(SweepDigest(
                     page, renderers, [&page] { return page.DigestTop() > 0; }, x, y),
                   L"a digest taller than the column offers no way to the rest of it");

    Assert::IsTrue(SweepDigest(page, renderers, [&page] { return page.DigestTop() == 0; }, x, y), L"there is no way back to the top");
  }

  TEST_METHOD(AWheelOverTheDigestScrollsItAndOverTheMapDoesNot)
  {
    // The wheel has been banked by `PointerInput` since ADR-009 and read by nothing (ADR-080). What
    // decides it is the pane under the pointer, so the same notch over two panes is two answers.
    Lockstep::MainPage page;
    page.Create(ATallDigest());

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    Assert::IsTrue(page.HandleZoom(-1, 100.0F, 300.0F), L"a notch over the digest scrolled nothing");
    Assert::AreEqual(std::size_t{1}, page.DigestTop(), L"and it moved by something other than one card");

    // Over the map the same notch is a camera and not a list (ADR-090). It is consumed -- so this
    // asserts what the DIGEST did, not what the call returned, which is the thing that changed when
    // the map learned to zoom.
    (void)page.HandleZoom(-1, 700.0F, 300.0F);
    Assert::AreEqual(std::size_t{1}, page.DigestTop(), L"a notch over the map scrolled the digest");
    Assert::IsFalse(page.Map().AtAuthoredFraming(), L"and the map did not take it either, so the notch went nowhere");
    page.ResetView();

    Assert::IsTrue(page.HandleZoom(1, 100.0F, 300.0F), L"a notch the other way did not come back");
    Assert::AreEqual(std::size_t{0}, page.DigestTop());

    // And it stops at the top rather than banking notches that have to be spun back.
    Assert::IsFalse(page.HandleZoom(1, 100.0F, 300.0F));
    Assert::AreEqual(std::size_t{0}, page.DigestTop());
  }

  TEST_METHOD(ThePageKeysMoveAScreenfulAndNotACard)
  {
    // A page is however many cards fit, which only the layout knows -- so the key asks the frame
    // rather than a number chosen in the handler (ADR-080).
    Lockstep::MainPage page;
    page.Create(ATallDigest());

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    const std::size_t screenful = page.CardsOnScreen();
    Assert::IsTrue(screenful > 1, L"this fixture fits one card a screen, so a page and a card are the same move");

    Assert::IsTrue(page.HandleKey(Neuron::KeyboardInput::Key::PageDown));
    Assert::AreEqual(screenful, page.DigestTop(), L"PageDown moved by something other than a screenful");

    Assert::IsTrue(page.HandleKey(Neuron::KeyboardInput::Key::PageUp));
    Assert::AreEqual(std::size_t{0}, page.DigestTop(), L"PageUp did not come back");

    Assert::IsFalse(page.HandleKey(Neuron::KeyboardInput::Key::Escape), L"a key this screen does not use did something");
  }

  TEST_METHOD(ADragOverTheDigestScrollsItByWholeCards)
  {
    // The finger's half. A drag is continuous and the column moves in cards, so what is left over is
    // banked: without that a slow drag scrolls nothing at all.
    Lockstep::MainPage page;
    page.Create(ATallDigest());

    Headless renderers;
    renderers.Begin();
    DrawPage(page, renderers);

    const Neuron::PointerInput::Drag nudge{.deltaXPixels = 0.0F, .deltaYPixels = -10.0F, .originXPixels = 100.0F, .originYPixels = 300.0F};
    Assert::IsTrue(page.HandleDrag(nudge), L"a drag that began on the digest was not consumed by it");
    Assert::AreEqual(std::size_t{0}, page.DigestTop(), L"ten pixels moved a whole card");

    for (std::int32_t again = 0; again < 4; ++again)
    {
      (void)page.HandleDrag(nudge);
    }
    Assert::AreEqual(std::size_t{1}, page.DigestTop(), L"fifty pixels of drag banked no card at all");
  }

  TEST_METHOD(TheBandSaysWhatIsHiddenAndNotHowManyPages)
  {
    // `1 / 4 · MORE ›` told a player how much column was left and nothing about whether the battle
    // they had not seen was in it (ADR-080).
    const auto simulation = PlayedMatch(2);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    state.digest.clear();
    for (std::int32_t index = 0; index < 20; ++index)
    {
      AddEvent(state, Lockstep::NOBODY, std::format("Production +{}", index + 1));
    }

    const std::vector<Lockstep::DigestCard> cards = Lockstep::CardsOf(state);
    Assert::IsTrue(cards.size() > 3U);

    const std::string summary = Lockstep::HiddenSummary(cards, 3);
    Assert::IsTrue(summary.find(std::to_string(cards.size() - 3)) != std::string::npos, L"the band does not say how many are hidden");
    Assert::IsTrue(summary.find("MORE") != std::string::npos);
    Assert::IsTrue(Lockstep::HiddenSummary(cards, cards.size()).empty(), L"a band with nothing below it still claimed something");
  }
};

TEST_CLASS(ProposalAnswerTapTests)
{
public:
  TEST_METHOD(BothOffersCanBeAnsweredBeforeOneLock)
  {
    Lockstep::MainPage page;
    page.Create(SeatZeroWithTwoOffers());
    Assert::AreEqual(std::size_t{2}, page.State().proposals.size(), L"this test needs two offers on the table");

    // Sweep the digest column until both offers carry an answer. Every ACCEPT and DECLINE on the
    // screen is pressed on the way, which is the point: the answers must end up on different
    // offers rather than overwriting one another.
    Headless renderers;
    const bool answered = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH - ORDERS_RAIL, SCREEN_HEIGHT,
                                   [&page] { return page.State().orders.answers.size() == 2U; });
    Assert::IsTrue(answered, L"two offers on screen and no pair of taps answers both");

    const Lockstep::OrderSet orders = Lockstep::OrdersOf(page.State());
    Assert::AreEqual(std::size_t{2}, orders.answers.size(), L"both answers did not reach one order set");
    Assert::IsTrue(orders.answers[0].proposal != orders.answers[1].proposal, L"both answers named the same offer");
  }

  TEST_METHOD(AnsweringOneOfferTwiceSendsOneAnswer)
  {
    Lockstep::MainPage page;
    page.Create(SeatZeroWithTwoOffers());

    Headless renderers;
    const bool first = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH - ORDERS_RAIL, SCREEN_HEIGHT,
                                [&page] { return !page.State().orders.answers.empty(); });
    Assert::IsTrue(first, L"nothing on the screen answers an offer");

    const std::int32_t offer = page.State().orders.answers.front().proposal;
    const bool accepted = page.State().orders.answers.front().accepted;

    // Press both buttons on that same offer as many times as they are found. However many taps it
    // takes, one offer is one answer -- the last one given.
    std::size_t answersForThatOffer = 0;
    (void)SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH - ORDERS_RAIL, SCREEN_HEIGHT,
                   [&page, offer, &answersForThatOffer]
                   {
                     answersForThatOffer = static_cast<std::size_t>(
                       std::count_if(page.State().orders.answers.begin(), page.State().orders.answers.end(),
                                     [offer](const Lockstep::ProposalAnswer& _answer) { return _answer.proposal == offer; }));
                     return answersForThatOffer > 1U;
                   });

    Assert::AreEqual(std::size_t{1}, answersForThatOffer, L"one offer collected more than one answer");

    const Lockstep::OrderSet orders = Lockstep::OrdersOf(page.State());
    const std::size_t forThatOffer = static_cast<std::size_t>(
      std::count_if(orders.answers.begin(), orders.answers.end(), [&page, offer](const Lockstep::AnswerOrder& _answer)
                    { return _answer.proposal == Lockstep::ProposalId{page.State().proposals[static_cast<std::size_t>(offer)].id}; }));
    Assert::AreEqual(std::size_t{1}, forThatOffer, L"one offer produced more than one answer order");

    // And the answer that survived is a real one either way round.
    const bool stillThere = std::any_of(page.State().orders.answers.begin(), page.State().orders.answers.end(),
                                        [offer](const Lockstep::ProposalAnswer& _answer) { return _answer.proposal == offer; });
    Assert::IsTrue(stillThere, L"the answer vanished rather than being replaced");
    (void)accepted;
  }
};

} // namespace LockstepTests
