#pragma once

#include <cstdint>

namespace Neuron
{

/// A deterministic pseudo-random generator, specified by value rather than by name.
///
/// This is **splitmix64**, written out below: a 64-bit state advanced by an odd constant, then two
/// xor-shift-multiply rounds and a final xor-shift. Every operation is on `std::uint64_t`, every
/// constant is in the source, and there is no branch, no table and no floating point anywhere in
/// it. Given a seed it produces the same sequence on every machine, every compiler and every
/// standard library, forever -- which is the property ADR-018 is about and the reason this class
/// exists at all.
///
/// WHY NOT `std::mt19937`. The engine is specified by the standard and the **distributions are
/// not**: `std::uniform_int_distribution` maps the engine's output to a range in a way the standard
/// leaves to the implementation, so the same generator and the same seed give different draws on
/// different libraries. A galaxy that differs between an MSVC build and a libstdc++ one is exactly
/// the failure this is here to prevent, and `Design/Reference/mobile-portability.md` makes that a
/// live concern rather than a hypothetical.
///
/// Its statistical quality is adequate for what this game asks of it -- laying out a galaxy -- and
/// is not the reason it was chosen. Reproducibility is. If something later needs a stronger
/// generator, that is a new type beside this one rather than a change to this one: the sequence
/// here is pinned by a test and changing it changes every galaxy ever generated.
class Prng
{
public:
  explicit constexpr Prng(std::uint64_t _seed) noexcept
    : m_state(_seed)
  {
  }

  /// The next 64 bits. Every other draw is built on this one.
  [[nodiscard]] constexpr std::uint64_t Next() noexcept
  {
    // The golden ratio scaled to 64 bits, and odd -- which is what makes the additive step a full
    // cycle over the whole 64-bit state rather than a short one.
    m_state += 0x9E3779B97F4A7C15ULL;

    std::uint64_t mixed = m_state;
    mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ULL;
    mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBULL;
    return mixed ^ (mixed >> 31);
  }

  /// Uniform in [0, _bound), and uniform is meant literally.
  ///
  /// REJECTION, NOT MODULO. `Next() % bound` is biased whenever `bound` does not divide 2^64: the
  /// first `2^64 mod bound` values each occur once more often than the rest. The bias is far too
  /// small to see and quite large enough to make a generated galaxy consistently favour one
  /// direction, which is the kind of unfairness nobody would ever find by looking. Redrawing the
  /// values in that partial block costs, on average, a fraction of one extra draw.
  [[nodiscard]] constexpr std::uint32_t Below(std::uint32_t _bound) noexcept
  {
    if (_bound <= 1)
    {
      return 0;
    }

    const std::uint64_t bound = _bound;
    // `0 - bound` in unsigned arithmetic is 2^64 - bound, so this is 2^64 mod bound without
    // needing 2^64 itself.
    const std::uint64_t threshold = (0ULL - bound) % bound;

    std::uint64_t drawn = Next();
    while (drawn < threshold)
    {
      drawn = Next();
    }
    return static_cast<std::uint32_t>(drawn % bound);
  }

  /// Uniform in [_low, _high], inclusive at both ends. Inclusive because every range this game
  /// draws is written that way -- "two to four ticks" means 2, 3 or 4.
  [[nodiscard]] constexpr std::int32_t Between(std::int32_t _low, std::int32_t _high) noexcept
  {
    if (_high <= _low)
    {
      return _low;
    }

    const auto span = static_cast<std::uint32_t>(_high - _low) + 1U;
    return _low + static_cast<std::int32_t>(Below(span));
  }

private:
  std::uint64_t m_state;
};

} // namespace Neuron
