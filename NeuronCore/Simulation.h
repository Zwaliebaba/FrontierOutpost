#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Neuron
{

/// What one player contributed to one tick: whether the server saw them, and what they ordered.
///
/// **Presence is stored alongside the orders and not derived from them**, because they are
/// different facts and the simulation's custodian rule reads both. A player who logged in and
/// changed nothing is present and submitted nothing; a player whose orders arrived from a retry is
/// not thereby present twice. A match store that kept only the orders would replay into a different
/// world than the one that was played.
struct PlayerTurn
{
  bool present = false;
  /// The encoded order set, or empty when the player submitted nothing.
  std::vector<std::uint8_t> orders;
};

/// Something the server can drive a tick of, without knowing what it is.
///
/// ADR-025. The successor to ADR-007's `Simulation`, which was
/// `ApplyOrder(MoveToOrder) / Tick() / Snapshot()` and is Deprecated — its *argument* is what
/// survives: `NeuronServer` references `NeuronCore` and nothing else (AGENTS.md §2), so the
/// interface lives here and the server ticks a simulation without knowing the game.
///
/// **IT SPEAKS IN BYTES, AND THAT IS THE POINT.** `NeuronCore` cannot name `Frontier::OrderSet` or
/// `Frontier::Snapshot` — they are `GameLogic`'s, on the far side of the seam. So orders arrive as
/// bytes, snapshots and digests leave as bytes, and the server routes them between a socket and
/// this interface without ever decoding one. A server that could read an order could be tempted to
/// act on it, and the day it does the simulation has stopped being authoritative.
///
/// The cost is real and worth stating: the server cannot validate, inspect or log the *contents* of
/// anything it carries. Everything it needs to know — the tick, whether the match is over, the hash
/// — is on this interface explicitly, because the alternative is peeking.
class Simulation
{
public:
  Simulation() = default;
  virtual ~Simulation() = default;

  Simulation(const Simulation&) = delete;
  Simulation& operator=(const Simulation&) = delete;
  Simulation(Simulation&&) = delete;
  Simulation& operator=(Simulation&&) = delete;

  /// How many players. The server routes by index, and an index outside this is a bug or an
  /// attacker.
  [[nodiscard]] virtual std::int32_t PlayerCount() const = 0;

  /// The number of ticks resolved so far. Tick zero is the state before anything was played.
  [[nodiscard]] virtual std::uint32_t Tick() const = 0;

  [[nodiscard]] virtual bool IsFinished() const = 0;

  /// A hash of the whole state. The match store asserts a reload against it, which is the entire
  /// safety argument for storing orders rather than state (ADR-024).
  [[nodiscard]] virtual std::uint64_t Hash() const = 0;

  /// The rules and seed this match was created from, encoded. Written once at the head of a store
  /// and handed back to recreate the same match from nothing.
  [[nodiscard]] virtual std::vector<std::uint8_t> Configuration() const = 0;

  /// Hold one player's orders for the tick being assembled, replacing anything held for them.
  ///
  /// Replacing rather than appending is what makes "edit until the lock" work: a client may send
  /// its whole order set as often as it likes and only the last one before the lock counts.
  virtual void Submit(std::int32_t _player, std::span<const std::uint8_t> _orders) = 0;

  /// The server saw this player since the last lock.
  virtual void MarkPresent(std::int32_t _player) = 0;

  /// Resolve with whatever is held, then clear the held orders and presence marks.
  virtual void Resolve() = 0;

  /// What was locked into the tick just resolved, in player order. This is what a store keeps.
  [[nodiscard]] virtual std::vector<PlayerTurn> LockedTurn() const = 0;

  [[nodiscard]] virtual std::vector<std::uint8_t> SnapshotFor(std::int32_t _player) const = 0;
  [[nodiscard]] virtual std::vector<std::uint8_t> DigestFor(std::int32_t _player) const = 0;

  /// What happened, in words, for the instrumentation log. Taken and cleared.
  ///
  /// **The game writes these, not the server**, and that is the resolution of the question ADR-025
  /// left open. The test plan wants proposals, lanes, captures and custodians logged, and all of
  /// those are things only the simulation knows -- but the server cannot read a digest to find
  /// them, because the seam is bytes and a server that could read one could act on one. So the game
  /// says what happened and the server decides where it goes and stamps it with a time.
  ///
  /// Lines are plain text because their reader is a person with a Phase 0 spreadsheet, not a
  /// program. A format nobody has asked for yet would be a format guessed at.
  [[nodiscard]] virtual std::vector<std::string> TakeEvents() = 0;
};

} // namespace Neuron
