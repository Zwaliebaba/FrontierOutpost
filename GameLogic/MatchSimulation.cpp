// MatchSimulation.cpp -- the game, behind the interface the server drives.
//
// The only interesting thing in here is where the bytes stop. Everything above this file speaks in
// `std::vector<std::uint8_t>`; everything below speaks in `OrderSet` and `Snapshot`. That boundary
// is the seam (ADR-025), and the reason a malformed record is dropped HERE rather than passed
// through is that `ByteReader` fills a truncated record with zeros -- which would arrive at
// `Match::Validate` looking like a perfectly well-formed order from player 0.

#include "pch.h"
#include "MatchSimulation.h"

#include <initializer_list>

namespace Lockstep
{

namespace
{

/// Every field of `MatchRules`, in wire order, ONCE.
///
/// The writer, the reader and the field count are all expanded from this list, so they cannot
/// disagree with each other. What the list cannot see is a field added to the struct and not to it,
/// and the `static_assert` on the struct's size below is the tripwire for that: adding a field
/// changes the size, the build fails here, and the fix is one line in this list and one number.
///
/// Written field by field rather than as a memory image: a store read back by a different build has
/// to see what was written rather than whatever the compiler laid out.
#define MATCH_RULES_FIELDS(X)    \
  X(playerCount)                 \
  X(matchLengthTicks)            \
  X(tickIntervalSeconds)         \
  X(capitalGuardTicks)           \
  X(proposalWindowTicks)         \
  X(custodianAbsenceTicks)       \
  X(siegeTicks)                  \
  X(satellitesPerCapital)        \
  X(frontierSystemsPerPlayer)    \
  X(maximumTicksToNearestRival)  \
  X(frontierLaneMinimumTicks)    \
  X(frontierLaneMaximumTicks)    \
  X(maximumSeedAttempts)         \
  X(startingShips)               \
  X(startingCredits)             \
  X(creditsPerSystem)            \
  X(capitalCreditsBonus)         \
  X(miningStationCredits)        \
  X(shipsPerShipyard)            \
  X(internalLaneIncome)          \
  X(tradeLaneIncome)             \
  X(combatRounds)                \
  X(damagePercentPerRound)       \
  X(defenderBonusPercent)        \
  X(rearGuardEnabled)            \
  X(shipyardCost)                \
  X(miningStationCost)           \
  X(tradeLaneCost)               \
  X(scorePerSystem)              \
  X(capitalScoreBonus)           \
  X(garrisonDecayPercent)        \
  X(custodianSpoilsYieldPercent) \
  X(firstWeekTicks)              \
  X(dominanceSharePercent)       \
  X(dominanceHoldTicks)          \
  X(scoutingRangeLanes)          \
  X(regionOpensAtTick)           \
  X(regionSiteCount)

/// Thirty-seven 32-bit fields and one bool, padded, on x64. If this fires, a field was added to
/// `MatchRules`: add it to `MATCH_RULES_FIELDS` in wire order, then update this number.
static_assert(sizeof(MatchRules) == 152, "MatchRules changed shape; add the field to MATCH_RULES_FIELDS and update this size");

/// Counted as a list of ones rather than as a run of `+1`. A macro whose replacement list is an
/// operator cannot be parenthesised and so cannot satisfy `bugprone-macro-parentheses`, and this
/// tree suppresses no check inline (`.clang-tidy` excludes one generated header and nothing else).
#define COUNT_ONE_FIELD(name) 1,
constexpr std::uint32_t CONFIGURATION_FIELDS =
  static_cast<std::uint32_t>(std::initializer_list<int>{MATCH_RULES_FIELDS(COUNT_ONE_FIELD)}.size());
#undef COUNT_ONE_FIELD

/// The roster byte for a seat a person sits in. Not a `BotPolicy` value and deliberately far from
/// one, so a roster read out of a truncated store cannot land on a policy by accident.
constexpr std::uint8_t HUMAN_SEAT = 0xFF;

void WriteField(Neuron::ByteWriter& _writer, std::uint32_t _value)
{
  _writer.WriteU32(_value);
}

void WriteField(Neuron::ByteWriter& _writer, bool _value)
{
  _writer.WriteBool(_value);
}

void ReadField(Neuron::ByteReader& _reader, std::uint32_t& _value)
{
  _value = _reader.ReadU32();
}

void ReadField(Neuron::ByteReader& _reader, bool& _value)
{
  _value = _reader.ReadBool();
}

void WriteRules(Neuron::ByteWriter& _writer, const MatchRules& _rules)
{
  _writer.WriteU32(CONFIGURATION_FIELDS);
#define WRITE_RULES_FIELD(name) WriteField(_writer, _rules.name);
  MATCH_RULES_FIELDS(WRITE_RULES_FIELD)
#undef WRITE_RULES_FIELD
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
#define READ_RULES_FIELD(name) ReadField(_reader, rules.name);
  MATCH_RULES_FIELDS(READ_RULES_FIELD)
#undef READ_RULES_FIELD
  return rules;
}

} // namespace

MatchSimulation::MatchSimulation(const MatchRules& _rules, std::uint64_t _seed)
  : m_match(Match::Create(_rules, _seed))
{
  m_pending.assign(_rules.playerCount, Neuron::PlayerTurn{});
  m_capitalFellAt.assign(_rules.playerCount, 0);
}

MatchSimulation::MatchSimulation(const MatchRules& _rules, std::uint64_t _seed, std::vector<std::optional<BotPolicy>> _bots)
  : MatchSimulation(_rules, _seed)
{
  m_bots = std::move(_bots);
  m_bots.resize(static_cast<std::size_t>(_rules.playerCount));
}

std::unique_ptr<MatchSimulation> MatchSimulation::FromConfiguration(std::span<const std::uint8_t> _configuration)
{
  Neuron::ByteReader reader{_configuration};
  const MatchRules rules = ReadRules(reader);
  const std::uint64_t seed = reader.ReadU64();

  // The roster is read only if it is there. A store written before bots existed ends after the
  // seed, and reloading one has to give the match it recorded -- which is one with no bots in it.
  std::vector<std::optional<BotPolicy>> bots;
  if (!reader.Failed() && !reader.AtEnd())
  {
    for (std::uint32_t player = 0; player < rules.playerCount; ++player)
    {
      const std::uint8_t style = reader.ReadU8();
      if (style == HUMAN_SEAT)
      {
        bots.emplace_back();
      }
      else if (style <= static_cast<std::uint8_t>(BotPolicy::Absentee))
      {
        bots.emplace_back(static_cast<BotPolicy>(style));
      }
      else
      {
        Neuron::Fatal("This match store names a bot style ({}) this build does not have.", style);
      }
    }
  }

  if (reader.Failed() || !reader.AtEnd())
  {
    Neuron::Fatal("This match store's configuration is not the shape this build writes.");
  }

  return std::unique_ptr<MatchSimulation>(new MatchSimulation{Reloaded{}, rules, seed, std::move(bots)});
}

MatchSimulation::MatchSimulation(Reloaded, const MatchRules& _rules, std::uint64_t _acceptedSeed,
                                 std::vector<std::optional<BotPolicy>> _bots)
  // **`Reload`, not `Create`.** The stored seed is the one the generator ACCEPTED, and `Create`
  // would treat it as a starting point and search onward from it -- a different galaxy, silently,
  // which is the one thing a match store cannot do (ADR-024).
  : m_match(Match::Reload(_rules, _acceptedSeed)),
    m_bots(std::move(_bots))
{
  m_pending.assign(_rules.playerCount, Neuron::PlayerTurn{});
  m_capitalFellAt.assign(_rules.playerCount, 0);
  m_bots.resize(static_cast<std::size_t>(_rules.playerCount));
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

  // **The roster is part of the match's identity, not part of its state.** A store reloaded without
  // it would have live seats nobody plays -- they would go absent, the custodian rules would fire,
  // and the reloaded match would diverge from the one that was stored for a reason nothing recorded.
  for (const std::optional<BotPolicy>& bot : m_bots)
  {
    writer.WriteU8(bot.has_value() ? static_cast<std::uint8_t>(*bot) : HUMAN_SEAT);
  }
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

void MatchSimulation::PlayBots()
{
  for (std::size_t index = 0; index < m_bots.size(); ++index)
  {
    if (!m_bots[index].has_value() || !m_pending[index].orders.empty())
    {
      continue;
    }

    // From the SNAPSHOT, which is the fogged view this seat would have been sent had a person been
    // in it. A bot that read `m_match` would play a game no human could play, and the whole reason
    // these policies are worth having as opponents is that they cannot (ADR-037).
    const Snapshot view = Snapshot::For(m_match, PlayerId{static_cast<std::int32_t>(index)});

    Neuron::ByteWriter writer;
    BotOrdersFor(*m_bots[index], view, m_match.Rules()).Write(writer);
    m_pending[index].orders = writer.Bytes();

    // A bot is never absent. That is the point of putting one in a seat: the custodian rules exist
    // for a person who stopped turning up, and this seat cannot.
    m_pending[index].present = true;
  }
}

void MatchSimulation::Resolve()
{
  PlayBots();

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

  m_match = TickResolver::Resolve(m_match, Lockstep::TickInput{.orders = orders, .present = present, .presenceUnknown = false}, m_lastTick);

  // What was locked is what a store keeps. Moved rather than copied, and the pending slate is
  // cleared in the same breath -- an order that survived into the next tick would be an order the
  // player never gave twice.
  m_locked = std::move(m_pending);
  m_pending.assign(static_cast<std::size_t>(PlayerCount()), Neuron::PlayerTurn{});

  RecordEvents();
}

void MatchSimulation::RecordEvents()
{
  const std::uint32_t tick = m_match.Tick();

  // ---- H3: a fleet order from somebody whose capital has already fallen --------------------------
  //
  // Read BEFORE this tick's losses are folded in, so "after" means a strictly earlier tick. A
  // player who lost their capital and moved a fleet in the same tick had not yet lost it when they
  // gave the order.
  for (std::size_t player = 0; player < m_locked.size(); ++player)
  {
    if (m_capitalFellAt[player] == 0 || m_locked[player].orders.empty())
    {
      continue;
    }

    Neuron::ByteReader reader{m_locked[player].orders};
    const OrderSet decoded = OrderSet::Read(reader);
    if (!reader.Failed() && !decoded.fleetOrders.empty())
    {
      m_events.push_back(std::format("T{} fleet-order-after-capital-fall player={} fell-at=T{} orders={}", tick, player,
                                     m_capitalFellAt[player], decoded.fleetOrders.size()));
    }
  }

  // ---- Everything the digests already know ---------------------------------------------------------
  //
  // The test plan's list, drawn from what the resolver reported rather than recomputed. A second
  // implementation of "did a capital fall" would be a second thing to keep right.
  for (std::size_t player = 0; player < m_lastTick.digests.size(); ++player)
  {
    for (const DigestEntry& entry : m_lastTick.digests[player])
    {
      switch (entry.kind)
      {
      case DigestKind::SystemLost:
        if (entry.system.IsValid() && m_match.GalaxyGraph().SystemAt(entry.system).kind == SystemKind::Capital)
        {
          m_events.push_back(std::format("T{} capital-fall player={} to={}", tick, player, entry.other.Index()));
          if (m_capitalFellAt[player] == 0)
          {
            m_capitalFellAt[player] = tick;
          }
        }
        break;

      case DigestKind::Custodian:
        // **Every player is told when somebody goes into custody**, so one event arrives here six
        // times -- and in those copies `player` is the reader while `entry.other` is the subject.
        // Logging the reader would have put six custodians in the log where there was one, and
        // named five of them wrongly. H2 is counted off these lines, so that is the whole
        // measurement rather than a cosmetic slip.
        if (entry.other.IsValid())
        {
          if (entry.other.Index() == static_cast<std::int32_t>(player))
          {
            m_events.push_back(std::format("T{} custodian player={}", tick, player));
          }
        }
        else
        {
          // The one Custodian entry with no subject is the returning player's own "you are back",
          // which is the opposite event.
          m_events.push_back(std::format("T{} custodian-ended player={}", tick, player));
        }
        break;

      case DigestKind::ProposalReceived:
        m_events.push_back(std::format("T{} proposal-sent to={} from={}", tick, player, entry.other.Index()));
        break;
      case DigestKind::ProposalAnswered:
        m_events.push_back(std::format("T{} proposal-answered player={} by={}", tick, player, entry.other.Index()));
        break;
      case DigestKind::ProposalIgnored:
        m_events.push_back(std::format("T{} proposal-ignored player={}", tick, player));
        break;
      case DigestKind::LaneOpened:
        m_events.push_back(std::format("T{} lane-opened player={} with={}", tick, player, entry.other.Index()));
        break;
      case DigestKind::LaneCanceled:
        m_events.push_back(std::format("T{} lane-canceled player={} detail=\"{}\"", tick, player, entry.detail));
        break;
      case DigestKind::Contact:
        m_events.push_back(std::format("T{} first-contact player={} with={}", tick, player, entry.other.Index()));
        break;
      case DigestKind::MatchEnded:
        m_events.push_back(std::format("T{} match-ended player={} score={}", tick, player, m_match.Players()[player].score));
        break;

      default:
        break;
      }
    }
  }

  // ---- Phase 0's watch item ------------------------------------------------------------------------
  //
  // "Log every departure that coincides with a hostile arrival at the same system." The fraction is
  // what decides whether the rear-guard round gets switched on, so it is logged every tick it is
  // non-zero rather than only when somebody remembers to ask.
  if (!m_lastTick.interceptions.empty())
  {
    m_events.push_back(std::format("T{} dancing dodged={} targeted={} rear-guard={}", tick, m_lastTick.Dodges(),
                                   m_lastTick.interceptions.size(), m_match.Rules().rearGuardEnabled ? "on" : "off"));
  }
}

std::vector<std::string> MatchSimulation::TakeEvents()
{
  std::vector<std::string> taken;
  taken.swap(m_events);
  return taken;
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

  Neuron::ByteWriter writer;
  Snapshot::WriteDigest(writer, Snapshot::DigestFor(m_lastTick, PlayerId{_player}));
  return writer.Bytes();
}

} // namespace Lockstep
