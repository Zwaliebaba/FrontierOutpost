// DigestViewTests.cpp -- ranking and grouping, which decide what a player reads first.
//
// **The actor grouping had never executed.** It is the most intricate presentation rule in the
// client -- a player who produced two or more events collapses into one card, ranked by their worst
// -- and the only evidence it worked was that nothing on screen had ever looked wrong. Nothing on
// screen would look wrong if it silently stopped grouping either.
//
// These build the state by hand, unlike SignalTests. That is the right way round for this file: a
// digest is whatever the server chose to say, the grouping rule is about combinations of events
// that a real match produces rarely and this test needs every time, and nothing here reads a
// snapshot.

#include "pch.h"
#include "CppUnitTest.h"

#include "DigestView.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

namespace
{

[[nodiscard]] Lockstep::DigestEvent Event(Lockstep::EventKind _kind, Lockstep::OwnerId _actor, std::string _title)
{
  Lockstep::DigestEvent event;
  event.kind = _kind;
  event.actor = _actor;
  event.title = std::move(_title);
  event.detail = "detail";
  return event;
}

[[nodiscard]] Lockstep::MatchState StateWith(std::vector<Lockstep::DigestEvent> _digest)
{
  Lockstep::MatchState state;
  state.viewer = 0;
  state.match.tick = 46;
  state.players = {Lockstep::PlayerBadge{.label = "YOU", .isYou = true}, Lockstep::PlayerBadge{.label = "HALVORSEN"},
                   Lockstep::PlayerBadge{.label = "SORNE"}};
  state.digest = std::move(_digest);
  return state;
}

[[nodiscard]] std::size_t ActorCards(const std::vector<Lockstep::DigestCard>& _cards)
{
  std::size_t count = 0;
  for (const Lockstep::DigestCard& card : _cards)
  {
    count += card.actor != Lockstep::NOBODY ? 1U : 0U;
  }
  return count;
}

} // namespace

TEST_CLASS(ConsequenceOrderTests)
{
public:
  TEST_METHOD(TheOrderIsTheOneTheGuidelinesSpecify)
  {
    // System lost, contact, proposal, absence, region, income, silence. Written out rather than
    // asserted pairwise because the list IS the rule -- a reader should be able to check it against
    // DESIGN-GUIDELINES "Copy" without following a chain of comparisons.
    const std::vector<Lockstep::EventKind> expected = {
      Lockstep::EventKind::Loss,   Lockstep::EventKind::Contact, Lockstep::EventKind::Proposal, Lockstep::EventKind::Custodian,
      Lockstep::EventKind::Region, Lockstep::EventKind::Economy, Lockstep::EventKind::Ignored};

    for (std::size_t index = 1; index < expected.size(); ++index)
    {
      Assert::IsTrue(Lockstep::ConsequenceRank(expected[index - 1]) < Lockstep::ConsequenceRank(expected[index]),
                     L"the consequence order does not match the guidelines");
    }
  }

  TEST_METHOD(TheWorstThingIsReadFirst)
  {
    // Authored in the wrong order on purpose. A digest arrives in whatever order the resolver
    // emitted it, and the screen's job is to put the thing that costs the player most at the top.
    const std::vector<Lockstep::DigestCard> cards = Lockstep::CardsOf(StateWith({
      Event(Lockstep::EventKind::Economy, Lockstep::NOBODY, "Production +38"),
      Event(Lockstep::EventKind::Loss, 1, "Pell lost to Sorne"),
      Event(Lockstep::EventKind::Region, Lockstep::NOBODY, "The Fallow opens T60"),
      Event(Lockstep::EventKind::Contact, 2, "Contact with Sorne"),
    }));

    Assert::IsTrue(cards.size() >= 4U);
    Assert::IsTrue(cards[0].kind == Lockstep::EventKind::Loss, L"a loss was not read first");
    Assert::IsTrue(cards[1].kind == Lockstep::EventKind::Contact, L"a contact did not come second");
  }
};

TEST_CLASS(ActorGroupingTests)
{
public:
  TEST_METHOD(TwoEventsByOnePlayerBecomeOneCard)
  {
    // The rule, in as few events as it takes to trigger it. Six lines about one rival is one thing
    // happening rather than six.
    const std::vector<Lockstep::DigestCard> cards = Lockstep::CardsOf(StateWith({
      Event(Lockstep::EventKind::Contact, 1, "Halvorsen at Kepler-Reach"),
      Event(Lockstep::EventKind::Proposal, 1, "Halvorsen proposes a lane"),
    }));

    Assert::AreEqual(std::size_t{1}, ActorCards(cards), L"two events by one player did not group");
    for (const Lockstep::DigestCard& card : cards)
    {
      if (card.actor == 1)
      {
        Assert::AreEqual(std::size_t{2}, card.lines.size(), L"the actor card does not carry both events");
        Assert::IsTrue(card.kind == Lockstep::EventKind::Contact, L"the card is not coloured by its worst event");
        Assert::IsTrue(card.leadEvent >= 0, L"the card leads with nothing, so a tap has nothing to focus");
      }
    }
  }

  TEST_METHOD(OneEventByAPlayerStaysAnEventCard)
  {
    // The boundary. Grouping a single event would put a player's name above a sentence that
    // already has their name in it.
    const std::vector<Lockstep::DigestCard> cards = Lockstep::CardsOf(StateWith({
      Event(Lockstep::EventKind::Contact, 1, "Halvorsen at Kepler-Reach"),
      Event(Lockstep::EventKind::Economy, Lockstep::NOBODY, "Production +38"),
    }));

    Assert::AreEqual(std::size_t{0}, ActorCards(cards), L"a single event was collapsed into an actor card");
  }

  TEST_METHOD(AnActorCardRanksByItsWorstEventAndNotItsFirst)
  {
    // **The half of the rule that is easy to get wrong.** A rival whose first event is income and
    // whose second is a system lost has to sort with the loss, or the worst thing on the screen is
    // three cards down behind a card about money.
    const std::vector<Lockstep::DigestCard> cards = Lockstep::CardsOf(StateWith({
      Event(Lockstep::EventKind::Economy, 1, "Halvorsen income"),
      Event(Lockstep::EventKind::Loss, 1, "Halvorsen took Pell"),
      Event(Lockstep::EventKind::Contact, 2, "Sorne sighted"),
    }));

    Assert::IsTrue(cards[0].actor == 1, L"the grouped rival did not sort by their worst event");
    Assert::IsTrue(cards[0].kind == Lockstep::EventKind::Loss);
  }

  TEST_METHOD(EventsAboutNobodyAreNeverGrouped)
  {
    // Income and the region's timer carry `NOBODY`. Grouping on that would put every world event
    // on one card belonging to no one.
    const std::vector<Lockstep::DigestCard> cards = Lockstep::CardsOf(StateWith({
      Event(Lockstep::EventKind::Economy, Lockstep::NOBODY, "Production +38"),
      Event(Lockstep::EventKind::Region, Lockstep::NOBODY, "The Fallow opens T60"),
      Event(Lockstep::EventKind::Ignored, Lockstep::NOBODY, "Sorne silent 4 ticks"),
    }));

    Assert::AreEqual(std::size_t{0}, ActorCards(cards), L"events about nobody were grouped onto one card");
    Assert::AreEqual(std::size_t{3}, cards.size(), L"three world events did not produce three cards");
  }

  TEST_METHOD(TwoPlayersWithTwoEventsEachBecomeTwoCards)
  {
    const std::vector<Lockstep::DigestCard> cards = Lockstep::CardsOf(StateWith({
      Event(Lockstep::EventKind::Contact, 1, "Halvorsen sighted"),
      Event(Lockstep::EventKind::Proposal, 1, "Halvorsen proposes"),
      Event(Lockstep::EventKind::Contact, 2, "Sorne sighted"),
      Event(Lockstep::EventKind::Proposal, 2, "Sorne proposes"),
    }));

    Assert::AreEqual(std::size_t{2}, ActorCards(cards), L"two rivals did not produce two actor cards");
    Assert::AreEqual(std::size_t{2}, cards.size(), L"something other than the two cards was emitted");
  }

  TEST_METHOD(EveryEventSurvivesGrouping)
  {
    // The safety net under all of the above: grouping rearranges what the player reads and must
    // never drop any of it. A lost event is a thing that happened and was never reported.
    const std::vector<Lockstep::DigestEvent> digest = {
      Event(Lockstep::EventKind::Contact, 1, "one"),    Event(Lockstep::EventKind::Proposal, 1, "two"),
      Event(Lockstep::EventKind::Loss, 1, "three"),     Event(Lockstep::EventKind::Economy, Lockstep::NOBODY, "four"),
      Event(Lockstep::EventKind::Custodian, 2, "five"),
    };

    std::size_t carried = 0;
    for (const Lockstep::DigestCard& card : Lockstep::CardsOf(StateWith(digest)))
    {
      carried += card.actor != Lockstep::NOBODY ? card.lines.size() : 1U;
    }
    Assert::AreEqual(digest.size(), carried, L"grouping lost an event");
  }
};

TEST_CLASS(EmptyDigestTests)
{
public:
  TEST_METHOD(AnEmptyDigestStillOffersAWayToGiveAnOrder)
  {
    // The digest IS the order surface, so an empty digest with no card is a first screen with no
    // controls on it -- which is the screen every new match opens on.
    const std::vector<Lockstep::DigestCard> cards = Lockstep::CardsOf(StateWith({}));

    Assert::AreEqual(std::size_t{1}, cards.size(), L"an empty digest produced no card at all");
    Assert::IsFalse(cards[0].title.empty(), L"the opening card says nothing");
  }

  TEST_METHOD(TheOpeningCardFocusesNothingItDoesNotHave)
  {
    // It is synthetic: there is no digest entry behind it, so `leadEvent` must not point into the
    // digest. A -1 followed into a vector crashed the client once already.
    const std::vector<Lockstep::DigestCard> cards = Lockstep::CardsOf(StateWith({}));
    Assert::IsTrue(cards[0].leadEvent == Lockstep::EventRefs::NONE || cards[0].leadEvent >= 0);
    Assert::IsTrue(cards[0].leadEvent < 1, L"the opening card leads with a digest entry that does not exist");
  }
};

TEST_CLASS(DeltaTests)
{
public:
  TEST_METHOD(TheDeltaCountsWhatTheDigestHolds)
  {
    Lockstep::MatchState state = StateWith({
      Event(Lockstep::EventKind::Loss, 1, "Pell lost"),
      Event(Lockstep::EventKind::Contact, 2, "Sorne sighted"),
      Event(Lockstep::EventKind::Proposal, 1, "a lane"),
      Event(Lockstep::EventKind::Proposal, 2, "another lane"),
    });
    state.unreadTicks = 2;
    state.lastSeenTick = 44;

    const Lockstep::DigestDelta delta = Lockstep::DeltaOf(state);
    Assert::IsTrue(delta.Any(), L"four events produced no delta at all");
    Assert::IsFalse(delta.cells.empty());
  }

  TEST_METHOD(ThereIsNoDeltaForSomebodyWhoMissedNothing)
  {
    // **The box belongs to the `SINCE YOU LOOKED` header and appears with it.** A player watching
    // every tick as it resolves has not missed anything, so a box telling them what changed would
    // be telling them what they just watched. `unreadTicks` is the whole of the condition.
    Lockstep::MatchState state = StateWith({
      Event(Lockstep::EventKind::Loss, 1, "Pell lost"),
      Event(Lockstep::EventKind::Contact, 2, "Sorne sighted"),
    });
    Assert::AreEqual(0U, state.unreadTicks);
    Assert::IsFalse(Lockstep::DeltaOf(state).Any(), L"a player who missed nothing was shown what they missed");
  }

  TEST_METHOD(NothingHappeningIsNoDelta)
  {
    // The four-cell box is drawn only when it has something in it. A box of zeroes is furniture.
    Lockstep::MatchState state = StateWith({});
    state.unreadTicks = 3;
    Assert::IsFalse(Lockstep::DeltaOf(state).Any(), L"an empty digest produced a delta box");
  }
};

} // namespace LockstepTests
