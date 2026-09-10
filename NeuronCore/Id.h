#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>

namespace Neuron
{

/// A typed index into the array that holds the thing it names.
///
/// Everything the simulation identifies -- a system, a lane, a fleet, a player -- lives in a
/// `std::vector` and is named by its position in it. That is a deliberate choice and ADR-018 gives
/// the reason: an index is stable within a match, cheap to sort, and free of the ordering
/// questions a pointer or a generated key would bring into a simulation that has to be
/// reproducible.
///
/// THE TAG IS THE WHOLE POINT. A `SystemId` and a `LaneId` are both indices, both are plausible in
/// the same expression, and confusing them produces a lane cost read out of a system or a lookup
/// that silently succeeds against the wrong array. Wrapping them in distinct types costs nothing
/// at run time -- this is a single `std::int32_t` and every operation is `constexpr` -- and makes
/// the compiler object to what review would not reliably catch.
///
/// R9: the TEMPLATE is engine, because "a typed index" knows nothing about this game. The tags are
/// game vocabulary and live in `GameLogic` beside the arrays they index.
template <typename Tag> class Id
{
public:
  using Underlying = std::int32_t;

  /// The absence of an id. Negative rather than a sentinel maximum, so that `IsValid()` is a sign
  /// test and an accidental use as a subscript is caught by a signed-to-unsigned conversion rather
  /// than by reading four billion elements in.
  static constexpr Underlying NONE = -1;

  constexpr Id() noexcept = default;
  explicit constexpr Id(Underlying _index) noexcept
    : m_index(_index)
  {
  }

  [[nodiscard]] constexpr Underlying Index() const noexcept
  {
    return m_index;
  }

  [[nodiscard]] constexpr bool IsValid() const noexcept
  {
    return m_index >= 0;
  }

  /// For subscripting the array this indexes. Named rather than an implicit conversion, because an
  /// implicit one would put the type safety above straight back.
  [[nodiscard]] constexpr std::size_t AsSize() const noexcept
  {
    return static_cast<std::size_t>(m_index);
  }

  /// Defaulted, and ordering is part of the contract rather than a convenience: ADR-018 requires
  /// that anything iterated into a result is iterated in a defined order, and sorting by id is how
  /// that is usually done.
  [[nodiscard]] friend constexpr bool operator==(Id _left, Id _right) noexcept = default;
  [[nodiscard]] friend constexpr std::strong_ordering operator<=>(Id _left, Id _right) noexcept = default;

private:
  Underlying m_index = NONE;
};

} // namespace Neuron
