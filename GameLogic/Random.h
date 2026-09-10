#pragma once

#include <cstdint>

namespace Frontier
{

/// SplitMix64: the generator's only source of randomness, and it is ours rather than the standard
/// library's (ADR-019).
///
/// The reason is determinism across toolchains. `std::mt19937` is specified bit for bit, but the
/// distributions that make it usable are not: `std::uniform_int_distribution` is free to consume a
/// different number of words and map them differently in every implementation, so the same seed
/// gives a different galaxy under a different standard library. A galaxy that is not reproducible
/// from its seed cannot be replayed from a Phase 0 bug report, which is the whole reason the seed
/// is in the state.
///
/// Sixteen lines, integer only, no floating point anywhere near it (R16).
class Random
{
public:
  explicit Random(std::uint64_t _seed) noexcept
    : m_state(_seed)
  {
  }

  [[nodiscard]] std::uint64_t Next() noexcept
  {
    m_state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = m_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }

  /// A value in [0, _bound). Plain modulo: the bias is about `_bound` parts in 2^64, which for the
  /// counts this generator asks for is not a quantity that exists. Rejection sampling would be
  /// unbiased and would make the number of words consumed depend on the values drawn, which is a
  /// worse property for something whose output has to be reproducible by inspection.
  [[nodiscard]] std::uint32_t Below(std::uint32_t _bound) noexcept
  {
    return _bound == 0 ? 0 : static_cast<std::uint32_t>(Next() % _bound);
  }

  /// A value in [_low, _high], inclusive at both ends.
  [[nodiscard]] std::int32_t Between(std::int32_t _low, std::int32_t _high) noexcept
  {
    if (_high <= _low)
    {
      return _low;
    }
    const auto span = static_cast<std::uint32_t>(_high - _low + 1);
    return _low + static_cast<std::int32_t>(Below(span));
  }

private:
  std::uint64_t m_state;
};

} // namespace Frontier
