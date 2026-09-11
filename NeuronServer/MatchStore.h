#pragma once

#include "Simulation.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Neuron
{

/// A match on disk: its configuration, and every turn that was locked into it.
///
/// **ADR-024, and the one sanctioned exception to R13.** The executable still ships alone; a server
/// process may write this one file. What it holds is the rules, the seed and the locked turns —
/// never the state — and a match is loaded by re-resolving from tick zero, which a measured 2.1 ms
/// for a whole match makes practical (`Design/Reference/tick-resolution-cost.md`).
///
/// The store does not know what a match is. It is a length-prefixed configuration blob followed by
/// a turn per tick per player, all opaque, because `NeuronServer` cannot name `GameLogic`'s types
/// (ADR-025). That also means it cannot sanity-check what it holds — the only check it can perform
/// is the one that matters, which is whether the replay reproduces the hash.
class MatchStore
{
public:
  /// What a store holds, in memory.
  struct Contents
  {
    std::vector<std::uint8_t> configuration;
    /// One entry per resolved tick, each a turn per player.
    std::vector<std::vector<PlayerTurn>> ticks;
    /// The hash the match had when this was last written. A reload that does not reproduce it is a
    /// simulation that has changed under a live match.
    std::uint64_t hash = 0;
  };

  /// Why a store could not be read.
  enum class Problem : std::uint8_t
  {
    None,
    /// No file there. Distinct from a broken one: it means start a new match, not stop.
    NotFound,
    /// Present, and not a match store, or written by an incompatible build.
    NotAStore,
    /// Present, the right shape, and cut short.
    Truncated
  };

  [[nodiscard]] static const char* Describe(Problem _problem) noexcept;

  /// Encodes contents to bytes. Separate from writing them so a test can round-trip without a
  /// filesystem, and so the caller decides what durability means.
  [[nodiscard]] static std::vector<std::uint8_t> Encode(const Contents& _contents);

  /// Decodes bytes to contents. Never throws and never trusts a length it was handed.
  [[nodiscard]] static Problem Decode(std::span<const std::uint8_t> _bytes, Contents& _outContents);

  /// Writes to a path, replacing whatever was there.
  ///
  /// **Written to a temporary and renamed over the target**, so a process that dies mid-write
  /// leaves the previous store intact rather than a half-written one. A match store that can be
  /// corrupted by a crash is a match store that will be, at four locks a day for three weeks.
  [[nodiscard]] static bool Save(const std::string& _path, const Contents& _contents);

  [[nodiscard]] static Problem Load(const std::string& _path, Contents& _outContents);

  /// The bytes a store starts with, so a file that is not one can be refused rather than parsed.
  static constexpr std::uint32_t MAGIC = 0x4D544E46U; // "FNTM", little-endian
  static constexpr std::uint32_t VERSION = 1;

private:
  /// Bounds on what a declared count may be, for the same reason `OrderSet::Read` has them: these
  /// numbers come off a disk that anything can write to.
  static constexpr std::uint32_t MAXIMUM_TICKS = 100'000;
  static constexpr std::uint32_t MAXIMUM_PLAYERS = 64;
  static constexpr std::uint32_t MAXIMUM_BLOB_BYTES = 1'000'000;
};

} // namespace Neuron
