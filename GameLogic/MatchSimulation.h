#pragma once

#include "BotPolicy.h"
#include "Match.h"
#include "Snapshot.h"
#include "TickLog.h"
#include "TickResolver.h"

#include "Simulation.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Lockstep
{

/// The 4X behind `Neuron::Simulation`.
///
/// This class is the seam, from the game's side. It lives in `GameLogic` rather than in
/// `NeuronServer` for the reason AGENTS.md §2 gives: the server references `NeuronCore` and nothing
/// else, so the game reaches *up* to the interface and the server never reaches down. The
/// executable is the only thing that sees both and is where they are joined.
///
/// **Everything crossing the interface is bytes** (`Simulation.h`), so this is also where encoding
/// happens: order sets decoded on the way in, snapshots and digests encoded on the way out. A
/// malformed order set is dropped here, with the reason, rather than reaching `Match::Validate` as
/// a record of zeros that would look like a legal order for player 0.
class MatchSimulation final : public Neuron::Simulation
{
public:
  /// A new match from rules and a seed. Every seat is a human's.
  MatchSimulation(const MatchRules& _rules, std::uint64_t _seed);

  /// The same, with some of the seats played by the machine.
  ///
  /// `_bots` is one entry per player, in seat order, and an empty entry is a seat somebody will sit
  /// in. A shorter vector leaves the remaining seats human, which is what a store written before
  /// bots existed decodes to.
  MatchSimulation(const MatchRules& _rules, std::uint64_t _seed, std::vector<std::optional<BotPolicy>> _bots);

  /// The same match, from what `Configuration()` wrote. Used when a store is reloaded.
  [[nodiscard]] static MatchSimulation FromConfiguration(std::span<const std::uint8_t> _configuration);

  [[nodiscard]] std::int32_t PlayerCount() const override;
  [[nodiscard]] std::uint32_t Tick() const override;
  [[nodiscard]] bool IsFinished() const override;
  [[nodiscard]] std::uint64_t Hash() const override;
  [[nodiscard]] std::vector<std::uint8_t> Configuration() const override;

  void Submit(std::int32_t _player, std::span<const std::uint8_t> _orders) override;
  void MarkPresent(std::int32_t _player) override;
  void Resolve() override;

  [[nodiscard]] std::vector<Neuron::PlayerTurn> LockedTurn() const override;
  [[nodiscard]] std::vector<std::uint8_t> SnapshotFor(std::int32_t _player) const override;
  [[nodiscard]] std::vector<std::uint8_t> DigestFor(std::int32_t _player) const override;
  [[nodiscard]] std::vector<std::string> TakeEvents() override;

  /// The match itself. The executable draws from it and the tests assert on it; the server never
  /// sees it, which is the point of the interface above.
  [[nodiscard]] const Match& State() const noexcept
  {
    return m_match;
  }

  /// The log of the tick just resolved. *Replay tick N* reads it.
  [[nodiscard]] const TickLog& LastTick() const noexcept
  {
    return m_lastTick;
  }

  /// How many submissions were refused as malformed since the match began. Zero in a healthy
  /// match; anything else is a client bug or somebody poking the socket, and the server wants to
  /// know which without being able to read the orders themselves.
  [[nodiscard]] std::uint32_t RejectedSubmissions() const noexcept
  {
    return m_rejectedSubmissions;
  }

private:
  MatchSimulation() = default;

  /// Tells the constructor below that the seed is one the generator already accepted.
  struct Reloaded
  {
  };

  /// A match rebuilt from a store. Separate from the public constructor because the seed means
  /// something different: `Match::Create` SEARCHES from its seed and `Match::Reload` uses it as-is,
  /// and a store's seed is the one that was settled on rather than the one that was asked for.
  MatchSimulation(Reloaded, const MatchRules& _rules, std::uint64_t _acceptedSeed, std::vector<std::optional<BotPolicy>> _bots);

  /// Turns the tick just resolved into instrumentation lines.
  void RecordEvents();

  Match m_match;
  TickLog m_lastTick;

  /// What is held for the tick being assembled, indexed by player.
  std::vector<Neuron::PlayerTurn> m_pending;
  /// What was locked into the tick just resolved.
  std::vector<Neuron::PlayerTurn> m_locked;

  std::uint32_t m_rejectedSubmissions = 0;

  /// Which seats play themselves, indexed by player. Empty entry means a person's seat.
  std::vector<std::optional<BotPolicy>> m_bots;

  /// What the instrumentation log has not been told yet.
  std::vector<std::string> m_events;

  /// Plays every bot seat that has not already been played, just before the lock.
  ///
  /// **The bots go through `Submit`'s own path, not around it.** A bot's orders are encoded, put in
  /// the same pending slot a human's would occupy and locked into `LockedTurn()` beside them, so
  /// the match store (ADR-024) records a bot's tick exactly as it records a person's and a replay
  /// of that store does not need the bots at all. It also means a bot seat that a human somehow
  /// submitted for keeps the human's orders: whatever arrived first is what is played.
  void PlayBots();

  /// The tick each player's capital fell, or zero. **This is H3's entire measurement**: the test
  /// plan asks whether losers keep playing, which is a fleet order from somebody whose capital fell
  /// on an EARLIER tick. It is a join across ticks and this is the only thing holding both halves.
  std::vector<std::uint32_t> m_capitalFellAt;
};

} // namespace Lockstep
