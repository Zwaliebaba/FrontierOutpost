// LinkStatusTests.cpp -- the four dialogs, and one state message decoded.
//
// **Both of these used to live inside the frame loop of `RunGame`**, which needs a window, a D3D12
// device and a swap chain: the only way to reach either was to launch the client on Windows and
// arrange for the link to fail. They are `StatusFor` (LinkStatus.h) and `StateFrom`
// (SnapshotView.h) now, and this is what that bought.

#include "pch.h"
#include "CppUnitTest.h"

#include "LinkStatus.h"
#include "MainPage.h"
#include "SnapshotView.h"

#include "MatchSimulation.h"
#include "TickResolver.h"

#include "ByteWriter.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

namespace
{

/// A board with a resolved tick behind it, so the digest has something in it.
[[nodiscard]] Lockstep::Match PlayedOnce()
{
  Lockstep::MatchRules rules;
  rules.playerCount = 6;
  Lockstep::Match match = Lockstep::Match::Create(rules, 0x5041'434B'4554'5321ULL);
  Lockstep::TickLog log;
  return Lockstep::TickResolver::Resolve(match, {}, log);
}

/// One state message as the wire carries it.
[[nodiscard]] std::vector<std::uint8_t> SnapshotBytes(const Lockstep::Match& _match, Lockstep::PlayerId _seat)
{
  Neuron::ByteWriter writer;
  Lockstep::Snapshot::For(_match, _seat).Write(writer);
  return writer.Bytes();
}

/// One tick's digest as the wire carries it, under the tick it is about.
[[nodiscard]] Neuron::Protocol::TickDigest DigestBytes(const Lockstep::TickLog& _log, Lockstep::PlayerId _seat, std::uint32_t _tick)
{
  Neuron::ByteWriter writer;
  Lockstep::Snapshot::WriteDigest(writer, Lockstep::Snapshot::DigestFor(_log, _seat));
  return Neuron::Protocol::TickDigest{.tick = _tick, .bytes = writer.Bytes()};
}

[[nodiscard]] Lockstep::MatchState AState()
{
  Lockstep::MatchRules rules;
  rules.playerCount = 6;
  const Lockstep::Match match = Lockstep::Match::Create(rules, 0x4C'49'4E'4B'01'02'03'04ULL);
  return Lockstep::ViewOf(Lockstep::Snapshot::For(match, Lockstep::PlayerId{0}), {}, 600);
}

} // namespace

// The order is the order of severity, and it is the whole of what this function decides: a refusal
// is final, a lost link is not, a match with no first state has not started, and a finished match
// is the only one of the four that is not a problem (ADR-038, ADR-085, ADR-097).
TEST_CLASS(LinkStatusTests)
{
public:
  TEST_METHOD(AWelcomedLinkWithAStateIsNoDialogAtAll)
  {
    const Lockstep::LinkStatus status =
      Lockstep::StatusFor(Lockstep::LinkFacts{.status = Lockstep::MatchConnection::Status::Playing}, AState(), true, false);
    Assert::IsTrue(status.kind == Lockstep::ConnectionDialog::Kind::None, L"a live match put a dialog over the board");
  }

  TEST_METHOD(ARefusalOutranksEverythingElse)
  {
    Lockstep::MatchState state = AState();
    state.match.finished = true;
    const Lockstep::LinkStatus status = Lockstep::StatusFor(Lockstep::LinkFacts{.status = Lockstep::MatchConnection::Status::Refused,
                                                                                .server = "host:7777",
                                                                                .refusal = Neuron::RefusalReason::AlreadyConnected},
                                                            state, true, false);
    Assert::IsTrue(status.kind == Lockstep::ConnectionDialog::Kind::Refused, L"a refusal did not win");
    Assert::IsTrue(status.facts.reason == Neuron::RefusalReason::AlreadyConnected, L"the refusal did not carry its reason");
    Assert::AreEqual(std::string{"host:7777"}, status.facts.server, L"the refusal did not say which server");
  }

  TEST_METHOD(AReconnectStillReadsAsALostLink)
  {
    // **A reconnect passes through `Connecting` on its way back**, and from the player's side that
    // is still the link being down. Letting the dialog blink out for the length of a handshake and
    // back in would read as the connection returning and going again.
    for (const Lockstep::MatchConnection::Status link :
         {Lockstep::MatchConnection::Status::Lost, Lockstep::MatchConnection::Status::Connecting,
          Lockstep::MatchConnection::Status::Resolving})
    {
      const Lockstep::LinkStatus status = Lockstep::StatusFor(Lockstep::LinkFacts{.status = link}, AState(), true, false);
      Assert::IsTrue(status.kind == Lockstep::ConnectionDialog::Kind::Lost, L"a link on its way back did not read as down");
      Assert::IsFalse(status.facts.lockCountdown.empty(), L"a lost link did not say what the clock was at");
    }
  }

  TEST_METHOD(ALostLinkBeforeAnyStateNamesNoCountdown)
  {
    // There is no clock to report yet, and reporting one would be reporting a default.
    const Lockstep::LinkStatus status =
      Lockstep::StatusFor(Lockstep::LinkFacts{.status = Lockstep::MatchConnection::Status::Lost}, AState(), false, false);
    Assert::IsTrue(status.facts.lockCountdown.empty(), L"a link lost before the first state invented a countdown");
  }

  TEST_METHOD(NothingHavingArrivedIsTheWaitingScreen)
  {
    const Lockstep::LinkStatus status =
      Lockstep::StatusFor(Lockstep::LinkFacts{.status = Lockstep::MatchConnection::Status::Playing}, AState(), false, false);
    Assert::IsTrue(status.kind == Lockstep::ConnectionDialog::Kind::Waiting, L"a match with no first state was not waiting");
  }

  TEST_METHOD(AFinishedMatchCarriesTheWholeTable)
  {
    // **Every player's placement, name and score** (ADR-097), ordered by placement, with the
    // reader's own row marked -- so the final screen says how the match went rather than only how
    // the reader did.
    Lockstep::MatchState state = AState();
    state.match.finished = true;
    for (std::size_t index = 0; index < state.players.size(); ++index)
    {
      state.players[index].placement = static_cast<std::uint32_t>(state.players.size() - index);
      state.players[index].score = static_cast<std::uint32_t>(index) * 10;
    }

    const Lockstep::LinkStatus status =
      Lockstep::StatusFor(Lockstep::LinkFacts{.status = Lockstep::MatchConnection::Status::Playing}, state, true, false);
    Assert::IsTrue(status.kind == Lockstep::ConnectionDialog::Kind::Finished, L"a finished match showed no final screen");
    Assert::AreEqual(state.players.size(), status.facts.standings.size(), L"the table lost a player");

    Assert::IsTrue(status.facts.standings.front().text.starts_with("1ST"), L"the table did not start at first place");
    std::int32_t marked = 0;
    for (const Lockstep::ConnectionDialog::Facts::Standing& row : status.facts.standings)
    {
      marked += row.isYou ? 1 : 0;
    }
    Assert::AreEqual(1, marked, L"the reader's own row is not marked exactly once");
  }

  TEST_METHOD(ADismissedFinalScreenStaysDismissed)
  {
    Lockstep::MatchState state = AState();
    state.match.finished = true;
    const Lockstep::LinkStatus status =
      Lockstep::StatusFor(Lockstep::LinkFacts{.status = Lockstep::MatchConnection::Status::Playing}, state, true, true);
    Assert::IsTrue(status.kind == Lockstep::ConnectionDialog::Kind::None, L"the final screen came back after being closed");
  }

  TEST_METHOD(ThereIsNeverAWayBackFromInsideAMatch)
  {
    // The join screen is behind the seats screen and a whole match, and for the host there is no
    // join screen to return to at all. `QUIT` is the honest button.
    const Lockstep::LinkStatus status =
      Lockstep::StatusFor(Lockstep::LinkFacts{.status = Lockstep::MatchConnection::Status::Lost}, AState(), true, false);
    Assert::IsFalse(status.facts.canGoBack, L"a dialog inside a match offered a way back");
  }
};

// One state message and the digests it carries (ADR-044).
TEST_CLASS(FreshStateTests)
{
public:
  TEST_METHOD(AStateMessageDecodesIntoAViewModel)
  {
    const Lockstep::Match match = PlayedOnce();
    const Lockstep::FreshState fresh = Lockstep::StateFrom(SnapshotBytes(match, Lockstep::PlayerId{0}), {}, 0, 600);
    Assert::IsTrue(fresh.decoded, L"a good state message did not decode");
    Assert::IsTrue(fresh.state.connected, L"a decoded state is by definition connected");
    Assert::AreEqual(match.Tick(), fresh.state.match.tick, L"the decoded state is about a different tick");
  }

  TEST_METHOD(AShortMessageIsRefusedWholeRatherThanDrawn)
  {
    // **A state that did not decode is not a state.** The reader fills a short record with zeros,
    // so half a message would draw a plausible board that the server never sent.
    std::vector<std::uint8_t> bytes = SnapshotBytes(PlayedOnce(), Lockstep::PlayerId{0});
    Assert::IsTrue(bytes.size() > 8, L"the snapshot is too small to truncate");
    bytes.resize(bytes.size() / 2);
    Assert::IsFalse(Lockstep::StateFrom(bytes, {}, 0, 600).decoded, L"half a state message was accepted");
  }

  TEST_METHOD(TrailingBytesAreRefusedToo)
  {
    std::vector<std::uint8_t> bytes = SnapshotBytes(PlayedOnce(), Lockstep::PlayerId{0});
    bytes.push_back(0xFF);
    Assert::IsFalse(Lockstep::StateFrom(bytes, {}, 0, 600).decoded, L"a message with a byte left over was accepted");
  }

  TEST_METHOD(EveryUnreadDigestIsConcatenatedAndTheReadOnesAreDropped)
  {
    // The events of every tick since the client last looked, oldest first -- not only the last
    // one's, which is what a player who closed a lid overnight used to be shown.
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    Lockstep::Match match = Lockstep::Match::Create(rules, 0x4449'4745'5354'2121ULL);

    std::vector<Neuron::Protocol::TickDigest> carried;
    for (std::uint32_t tick = 1; tick <= 3; ++tick)
    {
      Lockstep::TickLog log;
      match = Lockstep::TickResolver::Resolve(match, {}, log);
      carried.push_back(DigestBytes(log, Lockstep::PlayerId{0}, tick));
    }

    const std::vector<std::uint8_t> bytes = SnapshotBytes(match, Lockstep::PlayerId{0});
    const Lockstep::FreshState all = Lockstep::StateFrom(bytes, carried, 0, 600);
    Assert::IsTrue(all.decoded, L"three digests did not decode");

    const Lockstep::FreshState some = Lockstep::StateFrom(bytes, carried, 2, 600);
    Assert::IsTrue(some.decoded, L"one digest did not decode");
    Assert::IsTrue(all.state.digest.size() > some.state.digest.size(),
                   L"dropping the digests already read did not shorten what was composed");
  }

  TEST_METHOD(AGapSinceTheLastDrawnTickIsCounted)
  {
    // What the digest header says to a returning player, and the composition root is the only thing
    // that can know it: it is the only thing that sees one state replaced by the next.
    Lockstep::MatchRules rules;
    rules.playerCount = 6;
    Lockstep::Match match = Lockstep::Match::Create(rules, 0x5245'5455'524E'2121ULL);
    for (std::int32_t tick = 0; tick < 5; ++tick)
    {
      Lockstep::TickLog log;
      match = Lockstep::TickResolver::Resolve(match, {}, log);
    }
    const std::vector<std::uint8_t> bytes = SnapshotBytes(match, Lockstep::PlayerId{0});

    const Lockstep::FreshState behind = Lockstep::StateFrom(bytes, {}, 1, 600);
    Assert::AreEqual(match.Tick() - 1, behind.state.unreadTicks, L"the gap since the last drawn tick was not counted");
    Assert::AreEqual(1U, behind.state.lastSeenTick, L"the gap did not say where it was measured from");

    // Caught up, and a fresh process, are both no gap at all -- the second because R13 leaves the
    // client nothing to write, so it has honestly not looked at any of this.
    const Lockstep::FreshState caughtUp = Lockstep::StateFrom(bytes, {}, match.Tick() - 1, 600);
    Assert::AreEqual(0U, caughtUp.state.unreadTicks, L"a client one tick behind was told it had missed something");
    Assert::AreEqual(0U, Lockstep::StateFrom(bytes, {}, 0, 600).state.unreadTicks, L"a fresh process claimed to have missed ticks");
  }
};

} // namespace LockstepTests
