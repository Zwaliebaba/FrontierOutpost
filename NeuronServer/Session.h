#pragma once

#include "MatchStore.h"
#include "Simulation.h"
#include "TickSchedule.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Neuron
{

/// One match, running.
///
/// The session owns a `Simulation` it cannot see inside, a `TickSchedule` that tells it when, and a
/// `MatchStore` that outlives the process. It accepts orders from identified players, marks
/// presence, and at each lock resolves, persists and publishes.
///
/// **It never reads a clock.** `Advance(now)` is how time reaches it, which is the same discipline
/// as the simulation's and for a related reason: a session that read a clock could not be tested
/// against a three-week match in a loop, and Phase 0 needs exactly that rehearsed before six people
/// give up a weekend.
///
/// **It never decodes anything it carries.** Orders arrive as bytes and go to the simulation as
/// bytes; snapshots and digests come back as bytes and go to the socket as bytes (ADR-025).
class Session
{
public:
  /// Takes ownership of the simulation. `_storePath` may be empty, which means a match that does
  /// not survive the process -- useful in tests and honest about what it is.
  ///
  /// `_tokens` are the seats' tokens, kept in the store beside the schedule so that a restarted
  /// server admits the same people (ADR-042). The session never reads them; it carries them.
  Session(std::unique_ptr<Simulation> _simulation, TickSchedule _schedule, std::string _storePath, std::vector<std::string> _tokens = {});

  /// Replays a stored match into a fresh simulation, then asserts the result against the hash the
  /// store was written with.
  ///
  /// **That assertion is the whole safety argument for storing orders rather than state**
  /// (ADR-024), so it is a hard failure rather than a warning: a mismatch means the simulation has
  /// changed under a live match, and continuing would put six people in a world that is not the one
  /// they left.
  [[nodiscard]] static bool Reload(Simulation& _simulation, const MatchStore::Contents& _contents);

  /// A session picking up where a stored match left off: the same schedule, the same tokens, and
  /// the stored turns kept so the next lock is appended to the history rather than starting one.
  ///
  /// Null when the replay does not reproduce the stored hash. That is the one answer this cannot
  /// paper over, and the caller decides how loudly to say it (ADR-042).
  [[nodiscard]] static std::unique_ptr<Session> Resume(std::unique_ptr<Simulation> _simulation, const MatchStore::Contents& _contents,
                                                       std::string _storePath);

  [[nodiscard]] const Simulation& Match() const noexcept
  {
    return *m_simulation;
  }
  [[nodiscard]] const TickSchedule& Schedule() const noexcept
  {
    return m_schedule;
  }
  [[nodiscard]] const std::vector<std::string>& Tokens() const noexcept
  {
    return m_contents.tokens;
  }

  /// A player was seen. Presence is a fact about being here, not about submitting -- a player who
  /// logs in and changes nothing is present and is not on their way to custody.
  void MarkPresent(std::int32_t _player);

  /// Hold a player's orders for the next lock, replacing whatever they sent before. Returns false
  /// for a player index this match does not have; the simulation decides whether the bytes are a
  /// legal order set and the session cannot tell.
  [[nodiscard]] bool Submit(std::int32_t _player, std::span<const std::uint8_t> _orders);

  /// Resolve every lock that is due at `_now`, in order, and persist after each.
  ///
  /// **Every lock, not the latest one.** A server that slept through two locks owes two
  /// resolutions: a match that skips a tick has a hole in its order list and stops replaying
  /// (ADR-026). Returns how many it resolved.
  std::uint32_t Advance(Instant _now);

  /// Seconds until the next lock. What the client's countdown is drawn from, so that six clients
  /// agree about when the tick is rather than each running its own timer.
  [[nodiscard]] std::int64_t SecondsUntilNextLock(Instant _now) const;

  /// The game's instrumentation for the ticks just resolved, taken and cleared.
  [[nodiscard]] std::vector<std::string> TakeEvents();

  [[nodiscard]] std::vector<std::uint8_t> SnapshotFor(std::int32_t _player) const;
  [[nodiscard]] std::vector<std::uint8_t> DigestFor(std::int32_t _player) const;

  /// How many ticks this session has resolved since it was constructed. Distinct from the
  /// simulation's tick, which counts a reloaded match's replayed ticks too.
  [[nodiscard]] std::uint32_t ResolvedHere() const noexcept
  {
    return m_resolvedHere;
  }

  /// Whether the last persist succeeded. A session that cannot write its store keeps playing and
  /// says so, rather than stopping a live match over a full disk.
  [[nodiscard]] bool Persisted() const noexcept
  {
    return m_persisted;
  }

private:
  void Persist();

  std::unique_ptr<Simulation> m_simulation;
  TickSchedule m_schedule;
  std::string m_storePath;

  MatchStore::Contents m_contents;
  std::uint32_t m_resolvedHere = 0;
  bool m_persisted = true;
};

} // namespace Neuron
