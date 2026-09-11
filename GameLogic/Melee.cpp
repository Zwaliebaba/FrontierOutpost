// Melee.cpp -- the arithmetic of one fight, and nothing else.
//
// Lifted out of TickResolver when the orders rail needed to preview a battle (4X-01 step 8). It
// was already written to be a pure function of its inputs -- ADR-021 requires that, because a
// preview is only honest if the resolver has no inputs the client cannot see -- so lifting it was
// a move rather than a rewrite.

#include "pch.h"
#include "Melee.h"

namespace Frontier
{

void ResolveMelee(const MatchRules& _rules, std::span<MeleeSide> _sides)
{
  if (_sides.size() < 2)
  {
    return;
  }

  for (std::uint32_t round = 0; round < _rules.combatRounds; ++round)
  {
    // Effective strength, with the incumbent's bonus. Recomputed each round from the survivors, so
    // a side that is losing also hits less hard -- which is what makes reinforcing a losing fight a
    // real decision rather than free.
    std::vector<std::uint64_t> effective(_sides.size(), 0);
    std::uint64_t total = 0;
    for (std::size_t side = 0; side < _sides.size(); ++side)
    {
      const std::uint64_t bonus = _sides[side].incumbent ? _rules.defenderBonusPercent : 100U;
      effective[side] = (static_cast<std::uint64_t>(_sides[side].ships) * bonus) / 100U;
      total += effective[side];
    }

    // What each side deals, and where it lands: across the enemies in proportion to THEIR strength,
    // so a large enemy absorbs a large share and cannot hide behind a small one.
    std::vector<std::uint64_t> incoming(_sides.size(), 0);
    for (std::size_t attacker = 0; attacker < _sides.size(); ++attacker)
    {
      const std::uint64_t output = (effective[attacker] * _rules.damagePercentPerRound) / 100U;
      const std::uint64_t enemies = total - effective[attacker];
      if (output == 0 || enemies == 0)
      {
        continue;
      }
      for (std::size_t defender = 0; defender < _sides.size(); ++defender)
      {
        if (defender != attacker)
        {
          incoming[defender] += (output * effective[defender]) / enemies;
        }
      }
    }

    // Applied only now. Every number above came from the start of the round.
    bool anyLoss = false;
    for (std::size_t side = 0; side < _sides.size(); ++side)
    {
      const auto lost = static_cast<std::uint32_t>(std::min<std::uint64_t>(incoming[side], _sides[side].ships));
      _sides[side].ships -= lost;
      anyLoss = anyLoss || lost > 0;
    }

    std::size_t standing = 0;
    for (const MeleeSide& side : _sides)
    {
      standing += side.ships > 0 ? 1 : 0;
    }

    // Either it is over, or nobody can hurt anybody -- integer division truncates, so two tiny
    // fleets can reach a state where further rounds change nothing. Stopping is what makes that a
    // stalemate rather than a loop.
    if (standing < 2 || !anyLoss)
    {
      break;
    }
  }
}

} // namespace Frontier
