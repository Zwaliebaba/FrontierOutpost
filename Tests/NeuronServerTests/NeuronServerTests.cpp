// NeuronServerTests.cpp -- the schedule, the store, and the session that joins them.
//
// Step 1 of Design/Plans/4X-02-ServerAndClient.md. `SuiteSmoke` is gone; this is what replaced it.
//
// EVERY TEST HERE DRIVES A COUNTING FAKE, not the real game. That is ADR-007's argument, carried
// forward by ADR-025: `NeuronServer` references `NeuronCore` and nothing else, so the only
// simulation it can be tested against is one written here. The payoff is that these tests fail for
// server reasons -- a lock missed, a store truncated, a reload that did not reproduce -- and never
// because somebody changed a combat rule.

#include "pch.h"
#include "CppUnitTest.h"

#include "MatchStore.h"
#include "Session.h"
#include "TickSchedule.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

namespace
{

constexpr std::int32_t PLAYERS = 4;

// Two spellings of the same durations, because they are used as two different things: an interval
// is a `std::uint32_t` the schedule is constructed with, and an instant is a `std::int64_t` the
// arithmetic below happens in. Multiplying the first and passing it as the second is an implicit
// widening that clang-tidy is right to object to -- the multiplication would happen in 32 bits and
// only then widen.
constexpr std::uint32_t SIX_HOUR_INTERVAL = 6 * 60 * 60;
constexpr std::uint32_t ONE_HOUR_INTERVAL = 60 * 60;
constexpr Neuron::Instant SIX_HOURS = SIX_HOUR_INTERVAL;
constexpr Neuron::Instant ONE_HOUR = ONE_HOUR_INTERVAL;

/// A simulation that counts what was done to it and has no rules at all.
///
/// Its "state" is the sequence of turns it was given, hashed. That is enough to make a reload test
/// meaningful -- replaying the same turns in the same order must produce the same hash, and
/// replaying them in a different order must not -- without this file knowing what a fleet is.
class CountingSimulation final : public Neuron::Simulation
{
public:
  explicit CountingSimulation(std::uint64_t _seed = 7)
    : m_seed(_seed)
  {
    m_pending.assign(PLAYERS, Neuron::PlayerTurn{});
  }

  [[nodiscard]] std::int32_t PlayerCount() const override
  {
    return PLAYERS;
  }
  [[nodiscard]] std::uint32_t Tick() const override
  {
    return m_tick;
  }
  [[nodiscard]] bool IsFinished() const override
  {
    return m_tick >= m_length;
  }
  [[nodiscard]] std::uint64_t Hash() const override
  {
    return m_hash;
  }

  [[nodiscard]] std::vector<std::uint8_t> Configuration() const override
  {
    return {static_cast<std::uint8_t>(m_seed & 0xFFU), static_cast<std::uint8_t>(m_length & 0xFFU)};
  }

  void Submit(std::int32_t _player, std::span<const std::uint8_t> _orders) override
  {
    if (_player < 0 || _player >= PLAYERS)
    {
      ++submissionsRefused;
      return;
    }
    m_pending[static_cast<std::size_t>(_player)].orders.assign(_orders.begin(), _orders.end());
    ++submissions;
  }

  void MarkPresent(std::int32_t _player) override
  {
    if (_player >= 0 && _player < PLAYERS)
    {
      m_pending[static_cast<std::size_t>(_player)].present = true;
    }
  }

  void Resolve() override
  {
    ++resolves;
    ++m_tick;

    // The hash folds in the turns IN ORDER, so a reload that replayed them out of order, dropped
    // one, or lost a presence mark produces a different number -- which is exactly what the
    // session's reload check is supposed to catch.
    for (const Neuron::PlayerTurn& turn : m_pending)
    {
      m_hash = m_hash * 1099511628211ULL + (turn.present ? 1U : 2U);
      for (const std::uint8_t byte : turn.orders)
      {
        m_hash = m_hash * 1099511628211ULL + byte;
      }
      m_hash = m_hash * 1099511628211ULL + 0xFFU;
    }

    m_locked = std::move(m_pending);
    m_pending.assign(PLAYERS, Neuron::PlayerTurn{});
  }

  [[nodiscard]] std::vector<Neuron::PlayerTurn> LockedTurn() const override
  {
    return m_locked;
  }

  [[nodiscard]] std::vector<std::uint8_t> SnapshotFor(std::int32_t _player) const override
  {
    return {static_cast<std::uint8_t>(_player), static_cast<std::uint8_t>(m_tick & 0xFFU)};
  }

  [[nodiscard]] std::vector<std::uint8_t> DigestFor(std::int32_t _player) const override
  {
    return {static_cast<std::uint8_t>(0xD0), static_cast<std::uint8_t>(_player)};
  }

  void SetLength(std::uint32_t _ticks) noexcept
  {
    m_length = _ticks;
  }

  std::uint32_t resolves = 0;
  std::uint32_t submissions = 0;
  std::uint32_t submissionsRefused = 0;

private:
  std::uint64_t m_seed = 0;
  std::uint64_t m_hash = 0xCBF29CE484222325ULL;
  std::uint32_t m_tick = 0;
  std::uint32_t m_length = 1000;

  std::vector<Neuron::PlayerTurn> m_pending;
  std::vector<Neuron::PlayerTurn> m_locked;
};

[[nodiscard]] std::vector<std::uint8_t> SomeOrders(std::uint8_t _tag)
{
  return {_tag, static_cast<std::uint8_t>(_tag + 1), static_cast<std::uint8_t>(_tag + 2)};
}

/// A path in the temp directory that nothing else is using.
[[nodiscard]] std::string TemporaryStorePath(const char* _name)
{
  char buffer[512] = {};
  std::size_t length = 0;
  if (getenv_s(&length, buffer, sizeof(buffer), "TEMP") != 0 || length == 0)
  {
    return std::string("frontier-") + _name + ".store";
  }
  return std::string(buffer) + "\\frontier-" + _name + ".store";
}

} // namespace

// Pure arithmetic over an injected instant. No clock is read anywhere in this class, which is what
// lets a test drive three weeks in a loop.
TEST_CLASS(TickScheduleTests)
{
public:
  TEST_METHOD(TickZeroLocksOneIntervalAfterTheMatchStarts)
  {
    // Tick 0 is the state before anything was played, so the first lock is what produces tick 1.
    const Neuron::TickSchedule schedule{1000, SIX_HOUR_INTERVAL};

    Assert::AreEqual(static_cast<std::int64_t>(1000 + SIX_HOURS), schedule.LockOf(0));
    Assert::AreEqual(static_cast<std::int64_t>(1000 + 2 * SIX_HOURS), schedule.LockOf(1));
  }

  TEST_METHOD(LocksAreCountedFromTheStart)
  {
    const Neuron::TickSchedule schedule{0, SIX_HOUR_INTERVAL};

    Assert::AreEqual(0U, schedule.LocksDueAt(0));
    Assert::AreEqual(0U, schedule.LocksDueAt(SIX_HOURS - 1));
    Assert::AreEqual(1U, schedule.LocksDueAt(SIX_HOURS));
    Assert::AreEqual(4U, schedule.LocksDueAt(4 * SIX_HOURS), L"four ticks a day at six hours");
  }

  // Four a day at six hours, and across a day boundary rather than only within one -- the one-pager
  // wants fixed UTC times and the phase of day comes from the start instant.
  TEST_METHOD(TheDayBoundaryIsNotSpecial)
  {
    constexpr Neuron::Instant SIX_AM = 6 * ONE_HOUR;
    const Neuron::TickSchedule schedule{SIX_AM, SIX_HOUR_INTERVAL};

    Assert::AreEqual(static_cast<std::int64_t>(12 * 3600), schedule.LockOf(0), L"12:00");
    Assert::AreEqual(static_cast<std::int64_t>(18 * 3600), schedule.LockOf(1), L"18:00");
    Assert::AreEqual(static_cast<std::int64_t>(24 * 3600), schedule.LockOf(2), L"midnight");
    Assert::AreEqual(static_cast<std::int64_t>(30 * 3600), schedule.LockOf(3), L"06:00 the next day");
  }

  TEST_METHOD(APhaseZeroHourlyTickWorksTheSameWay)
  {
    const Neuron::TickSchedule schedule{0, ONE_HOUR_INTERVAL};
    Assert::AreEqual(48U, schedule.LocksDueAt(48 * ONE_HOUR), L"a forty-eight hour match at an hourly tick");
  }

  // The count, not the boolean. A server that slept through two locks owes two resolutions.
  TEST_METHOD(LocksOwedIsACountAndNotAFlag)
  {
    const Neuron::TickSchedule schedule{0, SIX_HOUR_INTERVAL};

    Assert::AreEqual(0U, schedule.LocksOwed(0, 0));
    Assert::AreEqual(1U, schedule.LocksOwed(0, SIX_HOURS));
    Assert::AreEqual(3U, schedule.LocksOwed(0, 3 * SIX_HOURS), L"three missed locks are three resolutions");
    Assert::AreEqual(0U, schedule.LocksOwed(5, 3 * SIX_HOURS), L"and a simulation ahead of the clock owes nothing");
  }

  TEST_METHOD(AnInstantBeforeTheMatchOwesNothing)
  {
    const Neuron::TickSchedule schedule{10'000, SIX_HOUR_INTERVAL};
    Assert::AreEqual(0U, schedule.LocksDueAt(0), L"not a negative count wrapped to enormous");
    Assert::AreEqual(0U, schedule.LocksOwed(0, 0));
  }

  TEST_METHOD(TheCountdownRunsDownAndStopsAtZero)
  {
    const Neuron::TickSchedule schedule{0, SIX_HOUR_INTERVAL};

    Assert::AreEqual(static_cast<std::int64_t>(SIX_HOURS), schedule.SecondsUntilLockOf(0, 0));
    Assert::AreEqual(static_cast<std::int64_t>(1), schedule.SecondsUntilLockOf(0, SIX_HOURS - 1));
    Assert::AreEqual(static_cast<std::int64_t>(0), schedule.SecondsUntilLockOf(0, SIX_HOURS));
    Assert::AreEqual(static_cast<std::int64_t>(0), schedule.SecondsUntilLockOf(0, SIX_HOURS + 99), L"never negative");
  }

  TEST_METHOD(AnIntervalOfZeroIsFlooredRatherThanDividingByZero)
  {
    const Neuron::TickSchedule schedule{0, 0};
    Assert::AreEqual(1U, schedule.IntervalSeconds());
  }
};

TEST_CLASS(MatchStoreTests)
{
public:
  [[nodiscard]] static Neuron::MatchStore::Contents Populated()
  {
    Neuron::MatchStore::Contents contents;
    contents.configuration = {1, 2, 3, 4, 5};
    contents.hash = 0xABCDEF0123456789ULL;

    for (std::uint32_t tick = 0; tick < 3; ++tick)
    {
      std::vector<Neuron::PlayerTurn> turns;
      for (std::int32_t player = 0; player < PLAYERS; ++player)
      {
        Neuron::PlayerTurn turn;
        turn.present = (player % 2) == 0;
        if (player != 1)
        {
          turn.orders = SomeOrders(static_cast<std::uint8_t>(tick * 10 + player));
        }
        turns.push_back(turn);
      }
      contents.ticks.push_back(turns);
    }
    return contents;
  }

  static void AssertSame(const Neuron::MatchStore::Contents& _expected, const Neuron::MatchStore::Contents& _actual)
  {
    Assert::AreEqual(_expected.hash, _actual.hash);
    Assert::AreEqual(_expected.configuration.size(), _actual.configuration.size());
    Assert::AreEqual(_expected.ticks.size(), _actual.ticks.size());

    for (std::size_t tick = 0; tick < _expected.ticks.size(); ++tick)
    {
      Assert::AreEqual(_expected.ticks[tick].size(), _actual.ticks[tick].size());
      for (std::size_t player = 0; player < _expected.ticks[tick].size(); ++player)
      {
        Assert::AreEqual(_expected.ticks[tick][player].present, _actual.ticks[tick][player].present);
        Assert::AreEqual(_expected.ticks[tick][player].orders.size(), _actual.ticks[tick][player].orders.size());
      }
    }
  }

  TEST_METHOD(AStoreSurvivesTheRoundTrip)
  {
    const Neuron::MatchStore::Contents original = Populated();
    const std::vector<std::uint8_t> bytes = Neuron::MatchStore::Encode(original);

    Neuron::MatchStore::Contents returned;
    Assert::IsTrue(Neuron::MatchStore::Decode(bytes, returned) == Neuron::MatchStore::Problem::None);
    AssertSame(original, returned);
  }

  TEST_METHOD(AnEmptyMatchRoundTrips)
  {
    Neuron::MatchStore::Contents original;
    original.configuration = {9};
    original.hash = 1;

    Neuron::MatchStore::Contents returned;
    Assert::IsTrue(Neuron::MatchStore::Decode(Neuron::MatchStore::Encode(original), returned) == Neuron::MatchStore::Problem::None);
    Assert::IsTrue(returned.ticks.empty(), L"a match that has not resolved a tick is still a match");
  }

  // The file is rewritten four times a day for three weeks, so the thing certain to happen to it is
  // a process dying partway through a write.
  TEST_METHOD(ATruncatedStoreIsRefusedRatherThanGuessedAt)
  {
    const std::vector<std::uint8_t> bytes = Neuron::MatchStore::Encode(Populated());

    for (const std::size_t cut : {std::size_t{0}, std::size_t{4}, std::size_t{20}, bytes.size() / 2, bytes.size() - 1})
    {
      Neuron::MatchStore::Contents returned;
      const auto problem = Neuron::MatchStore::Decode(std::span<const std::uint8_t>{bytes.data(), cut}, returned);
      Assert::IsTrue(problem != Neuron::MatchStore::Problem::None,
                     (std::wstring(L"cut at ") + std::to_wstring(cut) + L" was accepted").c_str());
    }
  }

  TEST_METHOD(SomethingThatIsNotAStoreIsNamedAsSuch)
  {
    const std::vector<std::uint8_t> nonsense = {'h', 'e', 'l', 'l', 'o', ' ', 't', 'h', 'e', 'r', 'e', '!'};

    Neuron::MatchStore::Contents returned;
    Assert::IsTrue(Neuron::MatchStore::Decode(nonsense, returned) == Neuron::MatchStore::Problem::NotAStore,
                   L"refused before it is parsed, not parsed into a garbage match");
  }

  TEST_METHOD(AStoreFromAnotherVersionIsRefused)
  {
    std::vector<std::uint8_t> bytes = Neuron::MatchStore::Encode(Populated());
    bytes[4] = 99; // the version field

    Neuron::MatchStore::Contents returned;
    Assert::IsTrue(Neuron::MatchStore::Decode(bytes, returned) == Neuron::MatchStore::Problem::NotAStore);
  }

  TEST_METHOD(ALyingCountDoesNotAllocateTheWorld)
  {
    Neuron::MatchStore::Contents contents;
    contents.configuration = {1};
    std::vector<std::uint8_t> bytes = Neuron::MatchStore::Encode(contents);

    // Overwrite the tick count with four billion. The decoder must refuse rather than reserve.
    const std::size_t tickCountAt = bytes.size() - 4;
    bytes[tickCountAt] = 0xFF;
    bytes[tickCountAt + 1] = 0xFF;
    bytes[tickCountAt + 2] = 0xFF;
    bytes[tickCountAt + 3] = 0xFF;

    Neuron::MatchStore::Contents returned;
    Assert::IsTrue(Neuron::MatchStore::Decode(bytes, returned) == Neuron::MatchStore::Problem::Truncated);
  }

  TEST_METHOD(AStoreSurvivesTheFilesystemToo)
  {
    const std::string path = TemporaryStorePath("roundtrip");
    const Neuron::MatchStore::Contents original = Populated();

    Assert::IsTrue(Neuron::MatchStore::Save(path, original));

    Neuron::MatchStore::Contents returned;
    Assert::IsTrue(Neuron::MatchStore::Load(path, returned) == Neuron::MatchStore::Problem::None);
    AssertSame(original, returned);

    (void)std::remove(path.c_str());
  }

  // A missing store is not a broken one. It means start a new match, and the difference decides
  // whether a server comes up or refuses to.
  TEST_METHOD(AMissingStoreIsDistinctFromABrokenOne)
  {
    Neuron::MatchStore::Contents returned;
    const auto problem = Neuron::MatchStore::Load(TemporaryStorePath("definitely-not-here"), returned);
    Assert::IsTrue(problem == Neuron::MatchStore::Problem::NotFound);
  }

  TEST_METHOD(EveryProblemDescribesItself)
  {
    for (std::uint8_t problem = 0; problem <= static_cast<std::uint8_t>(Neuron::MatchStore::Problem::Truncated); ++problem)
    {
      const char* text = Neuron::MatchStore::Describe(static_cast<Neuron::MatchStore::Problem>(problem));
      Assert::IsNotNull(text);
      Assert::AreNotEqual("unknown", text);
    }
  }
};

TEST_CLASS(SessionTests)
{
public:
  struct Running
  {
    CountingSimulation* fake = nullptr;
    std::unique_ptr<Neuron::Session> session;
  };

  [[nodiscard]] static Running Start(const std::string& _storePath = {}, std::uint32_t _interval = SIX_HOUR_INTERVAL)
  {
    auto simulation = std::make_unique<CountingSimulation>();
    CountingSimulation* observed = simulation.get();
    return Running{.fake = observed,
                   .session = std::make_unique<Neuron::Session>(std::move(simulation), Neuron::TickSchedule{0, _interval}, _storePath)};
  }

  // Once per lock and never between. The single most important property of the session: a client
  // that could provoke a resolution would be a client that could see the future half a tick early.
  TEST_METHOD(TheSessionResolvesOncePerLockAndNeverBetween)
  {
    Running running = Start();

    Assert::AreEqual(0U, running.session->Advance(0), L"nothing is owed at the start");
    Assert::AreEqual(0U, running.fake->resolves);

    Assert::AreEqual(0U, running.session->Advance(SIX_HOURS - 1), L"nor a second before the lock");
    Assert::AreEqual(0U, running.fake->resolves);

    Assert::AreEqual(1U, running.session->Advance(SIX_HOURS));
    Assert::AreEqual(1U, running.fake->resolves);

    Assert::AreEqual(0U, running.session->Advance(SIX_HOURS + 1), L"and not again until the next one");
    Assert::AreEqual(1U, running.fake->resolves);
  }

  TEST_METHOD(TwoMissedLocksProduceTwoResolutionsInOrder)
  {
    Running running = Start();

    Assert::AreEqual(2U, running.session->Advance(2 * SIX_HOURS), L"a server that slept owes both");
    Assert::AreEqual(2U, running.fake->resolves);
    Assert::AreEqual(2U, running.fake->Tick());
  }

  TEST_METHOD(AVeryLongSleepIsCaughtUpEntirely)
  {
    Running running = Start();
    Assert::AreEqual(20U, running.session->Advance(20 * SIX_HOURS), L"five days offline is twenty ticks owed");
    Assert::AreEqual(20U, running.fake->Tick());
  }

  TEST_METHOD(AFinishedMatchStopsResolvingEvenWhenLocksKeepComing)
  {
    Running running = Start();
    running.fake->SetLength(3);

    // The schedule is arithmetic and does not know the match is over; the session does.
    Assert::AreEqual(3U, running.session->Advance(50 * SIX_HOURS));
    Assert::IsTrue(running.fake->IsFinished());
    Assert::AreEqual(0U, running.session->Advance(100 * SIX_HOURS), L"and it stays stopped");
  }

  TEST_METHOD(OrdersAreHeldForTheNextLockAndReplaceEachOther)
  {
    Running running = Start();

    Assert::IsTrue(running.session->Submit(0, SomeOrders(1)));
    Assert::IsTrue(running.session->Submit(0, SomeOrders(2)), L"editing until the lock replaces rather than appends");
    Assert::AreEqual(0U, running.fake->resolves, L"and submitting resolves nothing");

    (void)running.session->Advance(SIX_HOURS);

    const std::vector<Neuron::PlayerTurn> locked = running.fake->LockedTurn();
    Assert::AreEqual(static_cast<size_t>(PLAYERS), locked.size());
    Assert::AreEqual(static_cast<size_t>(3), locked[0].orders.size());
    Assert::AreEqual(static_cast<std::uint8_t>(2), locked[0].orders[0], L"the last set before the lock is the one that counts");
  }

  TEST_METHOD(APlayerWhoIsNotInTheMatchIsRefused)
  {
    Running running = Start();

    Assert::IsFalse(running.session->Submit(-1, SomeOrders(1)));
    Assert::IsFalse(running.session->Submit(PLAYERS, SomeOrders(1)));
    Assert::IsTrue(running.session->Submit(PLAYERS - 1, SomeOrders(1)));
  }

  // Presence is marked by being seen, not by submitting. Getting this backwards makes every quiet
  // player a custodian on tick three.
  TEST_METHOD(PresenceIsMarkedByBeingSeenRatherThanBySubmitting)
  {
    Running running = Start();

    running.session->MarkPresent(2);
    Assert::IsTrue(running.session->Submit(3, SomeOrders(5)));

    (void)running.session->Advance(SIX_HOURS);
    const std::vector<Neuron::PlayerTurn> locked = running.fake->LockedTurn();

    Assert::IsTrue(locked[2].present, L"seen and silent is present");
    Assert::IsTrue(locked[2].orders.empty());
    Assert::IsFalse(locked[3].present, L"and submitting without being seen is not");
    Assert::IsFalse(locked[3].orders.empty());
  }

  TEST_METHOD(PresenceDoesNotCarryIntoTheNextTick)
  {
    Running running = Start();

    running.session->MarkPresent(1);
    (void)running.session->Advance(SIX_HOURS);
    Assert::IsTrue(running.fake->LockedTurn()[1].present);

    (void)running.session->Advance(2 * SIX_HOURS);
    Assert::IsFalse(running.fake->LockedTurn()[1].present, L"a player seen last tick is not thereby seen this one");
  }

  TEST_METHOD(TheCountdownComesFromTheSchedule)
  {
    Running running = Start();

    Assert::AreEqual(static_cast<std::int64_t>(SIX_HOURS), running.session->SecondsUntilNextLock(0));
    (void)running.session->Advance(SIX_HOURS);
    Assert::AreEqual(static_cast<std::int64_t>(SIX_HOURS), running.session->SecondsUntilNextLock(SIX_HOURS),
                     L"and points at the next lock once one has passed");
  }

  TEST_METHOD(SnapshotsAndDigestsComeStraightFromTheSimulation)
  {
    Running running = Start();
    (void)running.session->Advance(SIX_HOURS);

    const std::vector<std::uint8_t> snapshot = running.session->SnapshotFor(2);
    Assert::AreEqual(static_cast<size_t>(2), snapshot.size());
    Assert::AreEqual(static_cast<std::uint8_t>(2), snapshot[0], L"routed by player, unread by the session");

    const std::vector<std::uint8_t> digest = running.session->DigestFor(2);
    Assert::AreEqual(static_cast<std::uint8_t>(0xD0), digest[0]);
  }

  // ADR-024's safety argument, end to end: a match saved, discarded and reloaded is the same match.
  TEST_METHOD(AMatchSavedDiscardedAndLoadedIsTheSameMatch)
  {
    const std::string path = TemporaryStorePath("reload");
    std::uint64_t liveHash = 0;

    {
      Running running = Start(path);
      running.session->MarkPresent(0);
      (void)running.session->Submit(0, SomeOrders(11));
      (void)running.session->Advance(SIX_HOURS);

      running.session->MarkPresent(1);
      (void)running.session->Submit(2, SomeOrders(22));
      (void)running.session->Advance(2 * SIX_HOURS);

      Assert::IsTrue(running.session->Persisted());
      liveHash = running.fake->Hash();
    }

    Neuron::MatchStore::Contents contents;
    Assert::IsTrue(Neuron::MatchStore::Load(path, contents) == Neuron::MatchStore::Problem::None);
    Assert::AreEqual(static_cast<size_t>(2), contents.ticks.size());
    Assert::AreEqual(liveHash, contents.hash);

    CountingSimulation reloaded;
    Assert::IsTrue(Neuron::Session::Reload(reloaded, contents), L"the replay reproduced the hash");
    Assert::AreEqual(liveHash, reloaded.Hash());
    Assert::AreEqual(2U, reloaded.Tick());

    (void)std::remove(path.c_str());
  }

  // The check has to be able to FAIL, or it is decoration. A store whose turns were tampered with
  // must not reload silently.
  TEST_METHOD(AReloadThatDoesNotReproduceTheHashIsRefused)
  {
    const std::string path = TemporaryStorePath("tampered");

    {
      Running running = Start(path);
      running.session->MarkPresent(0);
      (void)running.session->Submit(0, SomeOrders(11));
      (void)running.session->Advance(SIX_HOURS);
    }

    Neuron::MatchStore::Contents contents;
    Assert::IsTrue(Neuron::MatchStore::Load(path, contents) == Neuron::MatchStore::Problem::None);

    // One player's presence flipped. Nothing about the file is malformed; it simply is not what
    // was played.
    contents.ticks[0][1].present = !contents.ticks[0][1].present;

    CountingSimulation reloaded;
    Assert::IsFalse(Neuron::Session::Reload(reloaded, contents), L"a match that replays differently is refused");

    (void)std::remove(path.c_str());
  }

  TEST_METHOD(TheStoreGrowsByOneTickPerLock)
  {
    const std::string path = TemporaryStorePath("growth");

    Running running = Start(path);
    for (std::uint32_t tick = 1; tick <= 4; ++tick)
    {
      (void)running.session->Advance(tick * SIX_HOURS);

      Neuron::MatchStore::Contents contents;
      Assert::IsTrue(Neuron::MatchStore::Load(path, contents) == Neuron::MatchStore::Problem::None);
      Assert::AreEqual(static_cast<size_t>(tick), contents.ticks.size(), L"persisted after every lock, not at the end");
    }

    (void)std::remove(path.c_str());
  }

  // A match with no store is a match that does not survive the process, and saying so is better
  // than pretending otherwise.
  TEST_METHOD(ASessionWithNoStorePathStillRuns)
  {
    Running running = Start();
    Assert::AreEqual(3U, running.session->Advance(3 * SIX_HOURS));
    Assert::IsTrue(running.session->Persisted(), L"nothing to write is not a failure to write");
  }
};

} // namespace NeuronServerTests
