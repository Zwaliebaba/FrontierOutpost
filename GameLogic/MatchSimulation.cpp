// MatchSimulation.cpp -- the game, behind the interface the server drives.
//
// The only interesting thing in here is where the bytes stop. Everything above this file speaks in
// `std::vector<std::uint8_t>`; everything below speaks in `OrderSet` and `Snapshot`. That boundary
// is the seam (ADR-025), and the reason a malformed record is dropped HERE rather than passed
// through is that `ByteReader` fills a truncated record with zeros -- which would arrive at
// `Match::Validate` looking like a perfectly well-formed order from player 0.

#include "pch.h"
#include "MatchSimulation.h"

namespace Frontier
{

namespace
{

/// The match's configuration, as bytes. Every field of `MatchRules` plus the seed.
///
/// Written field by field rather than as a memory image: the same argument as `ByteWriter`'s, which
/// is that a store read back by a different build has to see what was written rather than whatever
/// the compiler laid out. A field added to `MatchRules` without being added here is a field that
/// silently reverts to its default on reload, which is why the count is checked on read.
constexpr std::uint32_t CONFIGURATION_FIELDS = 34;

void WriteRules(Neuron::ByteWriter& _writer, const MatchRules& _rules)
{
  _writer.WriteU32(CONFIGURATION_FIELDS);

  _writer.WriteU32(_rules.playerCount);
  _writer.WriteU32(_rules.matchLengthTicks);
  _writer.WriteU32(_rules.tickIntervalSeconds);
  _writer.WriteU32(_rules.capitalGuardTicks);
  _writer.WriteU32(_rules.proposalWindowTicks);
  _writer.WriteU32(_rules.custodianAbsenceTicks);
  _writer.WriteU32(_rules.siegeTicks);
  _writer.WriteU32(_rules.satellitesPerCapital);
  _writer.WriteU32(_rules.frontierSystemsPerPlayer);
  _writer.WriteU32(_rules.maximumTicksToNearestRival);
  _writer.WriteU32(_rules.frontierLaneMinimumTicks);
  _writer.WriteU32(_rules.frontierLaneMaximumTicks);
  _writer.WriteU32(_rules.maximumSeedAttempts);
  _writer.WriteU32(_rules.startingShips);
  _writer.WriteU32(_rules.startingCredits);
  _writer.WriteU32(_rules.creditsPerSystem);
  _writer.WriteU32(_rules.capitalCreditsBonus);
  _writer.WriteU32(_rules.miningStationCredits);
  _writer.WriteU32(_rules.shipsPerShipyard);
  _writer.WriteU32(_rules.internalLaneIncome);
  _writer.WriteU32(_rules.tradeLaneIncome);
  _writer.WriteU32(_rules.combatRounds);
  _writer.WriteU32(_rules.damagePercentPerRound);
  _writer.WriteU32(_rules.defenderBonusPercent);
  _writer.WriteBool(_rules.rearGuardEnabled);
  _writer.WriteU32(_rules.shipyardCost);
  _writer.WriteU32(_rules.miningStationCost);
  _writer.WriteU32(_rules.tradeLaneCost);
  _writer.WriteU32(_rules.scorePerSystem);
  _writer.WriteU32(_rules.capitalScoreBonus);
  _writer.WriteU32(_rules.garrisonDecayPercent);
  _writer.WriteU32(_rules.custodianSpoilsYieldPercent);
  _writer.WriteU32(_rules.firstWeekTicks);
  _writer.WriteU32(_rules.dominanceSharePercent);
  _writer.WriteU32(_rules.dominanceHoldTicks);
  _writer.WriteU32(_rules.scoutingRangeLanes);
  _writer.WriteU32(_rules.regionOpensAtTick);
  _writer.WriteU32(_rules.regionSiteCount);
}

[[nodiscard]] MatchRules ReadRules(Neuron::ByteReader& _reader)
{
  const std::uint32_t fields = _reader.ReadU32();
  if (fields != CONFIGURATION_FIELDS)
  {
    // A store from a build whose rules had a different shape. Loading it would produce a match
    // subtly unlike the one that was played, and the hash check would catch that -- but it would
    // report a determinism failure, which is the wrong diagnosis and the expensive one to chase.
    Neuron::Fatal("This match store was written by a different build: {} rule fields, expected {}.", fields, CONFIGURATION_FIELDS);
  }

  MatchRules rules;
  rules.playerCount = _reader.ReadU32();
  rules.matchLengthTicks = _reader.ReadU32();
  rules.tickIntervalSeconds = _reader.ReadU32();
  rules.capitalGuardTicks = _reader.ReadU32();
  rules.proposalWindowTicks = _reader.ReadU32();
  rules.custodianAbsenceTicks = _reader.ReadU32();
  rules.siegeTicks = _reader.ReadU32();
  rules.satellitesPerCapital = _reader.ReadU32();
  rules.frontierSystemsPerPlayer = _reader.ReadU32();
  rules.maximumTicksToNearestRival = _reader.ReadU32();
  rules.frontierLaneMinimumTicks = _reader.ReadU32();
  rules.frontierLaneMaximumTicks = _reader.ReadU32();
  rules.maximumSeedAttempts = _reader.ReadU32();
  rules.startingShips = _reader.ReadU32();
  rules.startingCredits = _reader.ReadU32();
  rules.creditsPerSystem = _reader.ReadU32();
  rules.capitalCreditsBonus = _reader.ReadU32();
  rules.miningStationCredits = _reader.ReadU32();
  rules.shipsPerShipyard = _reader.ReadU32();
  rules.internalLaneIncome = _reader.ReadU32();
  rules.tradeLaneIncome = _reader.ReadU32();
  rules.combatRounds = _reader.ReadU32();
  rules.damagePercentPerRound = _reader.ReadU32();
  rules.defenderBonusPercent = _reader.ReadU32();
  rules.rearGuardEnabled = _reader.ReadBool();
  rules.shipyardCost = _reader.ReadU32();
  rules.miningStationCost = _reader.ReadU32();
  rules.tradeLaneCost = _reader.ReadU32();
  rules.scorePerSystem = _reader.ReadU32();
  rules.capitalScoreBonus = _reader.ReadU32();
  rules.garrisonDecayPercent = _reader.ReadU32();
  rules.custodianSpoilsYieldPercent = _reader.ReadU32();
  rules.firstWeekTicks = _reader.ReadU32();
  rules.dominanceSharePercent = _reader.ReadU32();
  rules.dominanceHoldTicks = _reader.ReadU32();
  rules.scoutingRangeLanes = _reader.ReadU32();
  rules.regionOpensAtTick = _reader.ReadU32();
  rules.regionSiteCount = _reader.ReadU32();
  return rules;
}

} // namespace

MatchSimulation::MatchSimulation(const MatchRules& _rules, std::uint64_t _seed)
  : m_match(Match::Create(_rules, _seed))
{
  m_pending.assign(_rules.playerCount, Neuron::PlayerTurn{});
}

MatchSimulation MatchSimulation::FromConfiguration(std::span<const std::uint8_t> _configuration)
{
  Neuron::ByteReader reader{_configuration};
  const MatchRules rules = ReadRules(reader);
  const std::uint64_t seed = reader.ReadU64();

  if (reader.Failed())
  {
    Neuron::Fatal("This match store's configuration is truncated.");
  }

  return MatchSimulation{rules, seed};
}

std::int32_t MatchSimulation::PlayerCount() const
{
  return static_cast<std::int32_t>(m_match.Players().size());
}

std::uint32_t MatchSimulation::Tick() const
{
  return m_match.Tick();
}

bool MatchSimulation::IsFinished() const
{
  return m_match.IsFinished();
}

std::uint64_t MatchSimulation::Hash() const
{
  return m_match.Hash();
}

std::vector<std::uint8_t> MatchSimulation::Configuration() const
{
  Neuron::ByteWriter writer;
  WriteRules(writer, m_match.Rules());

  // The ACCEPTED seed, which is what the generator settled on after any rejections -- not the seed
  // it was first offered. Storing the first one would regenerate a galaxy the match was not played
  // on, if the rules ever changed enough to alter which seeds are accepted.
  writer.WriteU64(m_match.Seed());
  return writer.Bytes();
}

void MatchSimulation::Submit(std::int32_t _player, std::span<const std::uint8_t> _orders)
{
  if (_player < 0 || _player >= PlayerCount())
  {
    ++m_rejectedSubmissions;
    return;
  }

  // Decoded here and now, rather than at the lock, for two reasons: a malformed record is refused
  // while there is still somebody connected to tell, and a record that decodes to somebody else's
  // player id is refused outright rather than quietly replacing their orders.
  Neuron::ByteReader reader{_orders};
  const OrderSet decoded = OrderSet::Read(reader);

  if (reader.Failed() || !reader.AtEnd() || decoded.player.Index() != _player)
  {
    ++m_rejectedSubmissions;
    return;
  }

  m_pending[static_cast<std::size_t>(_player)].orders.assign(_orders.begin(), _orders.end());
}

void MatchSimulation::MarkPresent(std::int32_t _player)
{
  if (_player < 0 || _player >= PlayerCount())
  {
    return;
  }
  m_pending[static_cast<std::size_t>(_player)].present = true;
}

void MatchSimulation::Resolve()
{
  std::vector<OrderSet> orders;
  std::vector<PlayerId> present;

  for (std::size_t index = 0; index < m_pending.size(); ++index)
  {
    const Neuron::PlayerTurn& turn = m_pending[index];
    if (turn.present)
    {
      present.emplace_back(static_cast<std::int32_t>(index));
    }
    if (!turn.orders.empty())
    {
      Neuron::ByteReader reader{turn.orders};
      orders.push_back(OrderSet::Read(reader));
    }
  }

  m_match = TickResolver::Resolve(m_match, Frontier::TickInput{.orders = orders, .present = present, .presenceUnknown = false}, m_lastTick);

  // What was locked is what a store keeps. Moved rather than copied, and the pending slate is
  // cleared in the same breath -- an order that survived into the next tick would be an order the
  // player never gave twice.
  m_locked = std::move(m_pending);
  m_pending.assign(static_cast<std::size_t>(PlayerCount()), Neuron::PlayerTurn{});
}

std::vector<Neuron::PlayerTurn> MatchSimulation::LockedTurn() const
{
  return m_locked;
}

std::vector<std::uint8_t> MatchSimulation::SnapshotFor(std::int32_t _player) const
{
  if (_player < 0 || _player >= PlayerCount())
  {
    return {};
  }

  Neuron::ByteWriter writer;
  Snapshot::For(m_match, PlayerId{_player}).Write(writer);
  return writer.Bytes();
}

std::vector<std::uint8_t> MatchSimulation::DigestFor(std::int32_t _player) const
{
  if (_player < 0 || _player >= PlayerCount())
  {
    return {};
  }

  const std::vector<DigestEntry> digest = Snapshot::DigestFor(m_lastTick, PlayerId{_player});

  Neuron::ByteWriter writer;
  writer.WriteU32(static_cast<std::uint32_t>(digest.size()));
  for (const DigestEntry& entry : digest)
  {
    writer.WriteU8(static_cast<std::uint8_t>(entry.kind));
    writer.WriteU32(entry.severity);
    writer.WriteString(entry.title);
    writer.WriteString(entry.detail);
    writer.WriteI32(entry.system.Index());
    writer.WriteI32(entry.lane.Index());
    writer.WriteI32(entry.fleet.Index());
    writer.WriteI32(entry.other.Index());
  }
  return writer.Bytes();
}

} // namespace Frontier
