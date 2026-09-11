// Snapshot.cpp -- what one player is entitled to know, and nothing beyond it.
//
// The thing to keep in mind while editing this file: EVERY FIELD ADDED HERE GETS SENT. There is no
// second filter downstream. A convenience field that "the client needs anyway" is a field a player
// can read out of the wire, and the one-pager's whole diplomacy layer rests on not knowing what a
// rival is doing until they commit.
//
// Visibility itself is decided in the resolver (`Reckon`) and stored on the match, not recomputed
// here. That split matters: what a player has seen is STATE, it is in the hash, and two machines
// that disagreed about it would build two different snapshots of the same tick.

#include "pch.h"
#include "Snapshot.h"

#include <array>

#include "TickResolver.h"

namespace Lockstep
{

namespace
{

/// "14 v 11 (+def) - 6 left", in the form the orders rail draws.
/// The preview, as numbers and as the phrase the rail has always shown.
///
/// One walk of the fleets rather than two, because the caller wants both and they are the same
/// fight. `outEntry.preview` stays what it was so nothing that reads it changes; the numbers
/// beside it are what a verdict is built from.
void DescribePreview(const Match& _match, PlayerId _viewer, SystemId _destination, std::uint32_t _ships, SnapshotFleet& _outEntry)
{
  const std::array<MeleeSide, 1> incoming = {MeleeSide{.player = _viewer, .ships = _ships, .incumbent = false}};
  const std::vector<MeleeSide> after = TickResolver::Preview(_match, _destination, incoming);

  std::uint32_t defenders = 0;
  bool defenderIsIncumbent = false;
  std::uint32_t mine = 0;
  std::uint32_t theirs = 0;
  for (const MeleeSide& side : after)
  {
    if (side.player == _viewer)
    {
      mine = side.ships;
    }
    else
    {
      // **Summed, not taken.** More than one rival can be standing on the same system, and a
      // verdict that reported only the first would understate what the player is flying into.
      theirs += side.ships;
    }
  }

  // The "before" numbers come from the match rather than from the result, since the result is
  // after the fight.
  for (std::size_t index = 0; index < _match.Fleets().size(); ++index)
  {
    const MatchFleet& fleet = _match.Fleets()[index];
    if (!fleet.destroyed && !fleet.InTransit() && fleet.at == _destination && fleet.owner != _viewer)
    {
      defenders += fleet.ships;
      defenderIsIncumbent = true;
    }
  }

  if (defenders == 0)
  {
    return;
  }

  _outEntry.preview = std::format("{} v {}{} - {} left", _ships, defenders, defenderIsIncumbent ? " (+def)" : "", mine);
  _outEntry.previewMine = _ships;
  _outEntry.previewTheirs = defenders;
  _outEntry.previewMineAfter = mine;
  _outEntry.previewTheirsAfter = theirs;
  _outEntry.previewDefended = defenderIsIncumbent;
}

} // namespace

Snapshot Snapshot::For(const Match& _match, PlayerId _player)
{
  Snapshot view;
  if (!_match.HasPlayer(_player))
  {
    return view;
  }

  view.m_viewer = _player;
  view.m_tick = _match.Tick();
  view.m_regionAnchor = _match.GalaxyGraph().RegionAnchor();
  view.m_regionOpensAt = _match.Rules().regionOpensAtTick;
  view.m_finished = _match.IsFinished();

  view.m_capitalGuardTicksLeft = _match.Tick() < _match.Rules().capitalGuardTicks ? _match.Rules().capitalGuardTicks - _match.Tick() : 0;

  // ---- Systems ------------------------------------------------------------------------------------
  //
  // Only what has ever been seen. A system never seen is not in the list at all rather than in it
  // and blank -- a blank entry still tells the player it exists and where.
  const std::vector<SeenSystem>& seen = _match.SeenBy(_player);
  for (std::size_t index = 0; index < _match.Systems().size(); ++index)
  {
    if (!seen[index].known)
    {
      continue;
    }

    const SystemId id{static_cast<std::int32_t>(index)};
    const GalaxySystem& system = _match.GalaxyGraph().SystemAt(id);
    const SystemState& state = _match.SystemAt(id);

    SnapshotSystem entry;
    entry.id = id;
    entry.name = system.name;
    entry.positionX = system.positionX;
    entry.positionY = system.positionY;
    entry.kind = system.kind;
    entry.live = seen[index].live;
    entry.asOfTick = seen[index].asOfTick;

    // Remembered systems report what was true when they were last seen. Live ones report now.
    entry.owner = seen[index].live ? state.owner : seen[index].owner;
    entry.hasShipyard = seen[index].live ? state.hasShipyard : seen[index].hadShipyard;
    entry.hasMiningStation = seen[index].live ? state.hasMiningStation : seen[index].hadMiningStation;

    if (seen[index].live)
    {
      entry.siegeTicks = state.siegeTicks;
      entry.capturedAt = state.capturedAt;
      entry.halfYield = state.halfYield;
    }

    view.m_systems.push_back(std::move(entry));
  }

  // ---- Lanes --------------------------------------------------------------------------------------
  //
  // A lane is reported when the player knows both of its ends. Knowing one end and not the other
  // would draw a line into the dark, which is a different kind of information leak: it says
  // something is there.
  for (std::size_t index = 0; index < _match.GalaxyGraph().Lanes().size(); ++index)
  {
    const GalaxyLane& lane = _match.GalaxyGraph().Lanes()[index];
    if (!seen[lane.a.AsSize()].known || !seen[lane.b.AsSize()].known)
    {
      continue;
    }

    const LaneId id{static_cast<std::int32_t>(index)};
    view.m_lanes.push_back(
      SnapshotLane{.id = id, .a = lane.a, .b = lane.b, .costTicks = lane.costTicks, .tradeLane = _match.IsTradeLane(id)});
  }

  // ---- Fleets -------------------------------------------------------------------------------------
  for (std::size_t index = 0; index < _match.Fleets().size(); ++index)
  {
    const MatchFleet& fleet = _match.Fleets()[index];
    if (fleet.destroyed)
    {
      continue;
    }

    const bool mine = fleet.owner == _player;

    // "Fleets in transit are public once departed" -- commitment is blind at the moment of choice
    // and visible afterwards, which is what makes reading a rival's allocation a skill.
    const bool publicInTransit = fleet.InTransit();

    // A parked fleet is visible only where the player can see. Not `known`: a remembered system
    // does not report a garrison that may have left three ticks ago.
    const bool parkedAndVisible = !fleet.InTransit() && fleet.at.IsValid() && seen[fleet.at.AsSize()].live;

    if (!mine && !publicInTransit && !parkedAndVisible)
    {
      continue;
    }

    SnapshotFleet entry;
    entry.id = FleetId{static_cast<std::int32_t>(index)};
    entry.owner = fleet.owner;
    entry.ships = fleet.ships;
    entry.at = fleet.at;
    entry.movingFrom = fleet.movingFrom;
    entry.movingTo = fleet.movingTo;
    entry.ticksRemaining = fleet.ticksRemaining;

    if (mine && fleet.InTransit() && fleet.movingTo.IsValid())
    {
      DescribePreview(_match, _player, fleet.movingTo, fleet.ships, entry);
    }

    view.m_fleets.push_back(std::move(entry));
  }

  // ---- Proposals ----------------------------------------------------------------------------------
  //
  // Only the ones this player is a party to. An offer between two other empires is exactly the kind
  // of thing the design wants read from lanes and movements rather than handed over.
  for (const OpenProposal& proposal : _match.Proposals())
  {
    if (proposal.from != _player && proposal.to != _player)
    {
      continue;
    }

    const std::uint32_t closesAt = proposal.openedAt + _match.Rules().proposalWindowTicks;
    view.m_proposals.push_back(SnapshotProposal{.id = proposal.id,
                                                .from = proposal.from,
                                                .to = proposal.to,
                                                .kind = proposal.kind,
                                                .lane = proposal.lane,
                                                .conditionalLane = proposal.conditionalLane,
                                                .ticks = proposal.ticks,
                                                .ticksLeft = closesAt > _match.Tick() ? closesAt - _match.Tick() : 0});
  }

  // ---- Standings ----------------------------------------------------------------------------------
  //
  // Everybody's, for everybody. "Public score. The leader is always visible" is the anti-snowball,
  // and it only works if it is public -- this is the one thing in the snapshot that is deliberately
  // not fogged.
  const std::vector<PlayerId> placements = _match.Placements();
  for (std::size_t index = 0; index < _match.Players().size(); ++index)
  {
    const PlayerId player{static_cast<std::int32_t>(index)};
    const PlayerState& state = _match.Players()[index];

    const auto place =
      static_cast<std::uint32_t>(std::distance(placements.begin(), std::find(placements.begin(), placements.end(), player)) + 1);

    view.m_standings.push_back(SnapshotStanding{
      .player = player, .score = state.score, .placement = place, .status = state.status, .custodianSince = state.custodianSince});
  }

  view.m_totalSystems = _match.GalaxyGraph().SystemCount();
  for (const SystemState& system : _match.Systems())
  {
    if (!system.owner.IsValid())
    {
      ++view.m_unclaimedSystems;
    }
  }

  return view;
}

std::vector<DigestEntry> Snapshot::DigestFor(const TickLog& _log, PlayerId _player)
{
  if (!_player.IsValid() || _player.AsSize() >= _log.digests.size())
  {
    return {};
  }
  return _log.digests[_player.AsSize()];
}

void Snapshot::WriteDigest(Neuron::ByteWriter& _writer, const std::vector<DigestEntry>& _digest)
{
  _writer.WriteU32(static_cast<std::uint32_t>(_digest.size()));
  for (const DigestEntry& entry : _digest)
  {
    _writer.WriteU8(static_cast<std::uint8_t>(entry.kind));
    _writer.WriteU32(entry.severity);
    _writer.WriteString(entry.title);
    _writer.WriteString(entry.detail);
    _writer.WriteI32(entry.system.Index());
    _writer.WriteI32(entry.lane.Index());
    _writer.WriteI32(entry.fleet.Index());
    _writer.WriteI32(entry.other.Index());
  }
}

std::vector<DigestEntry> Snapshot::ReadDigest(Neuron::ByteReader& _reader)
{
  /// The same bound and the same reason as everywhere else: this count came off a socket.
  constexpr std::uint32_t MAXIMUM_ENTRIES = 4096;

  const std::uint32_t declared = _reader.ReadU32();
  if (_reader.Failed() || declared > MAXIMUM_ENTRIES)
  {
    return {};
  }

  std::vector<DigestEntry> digest;
  digest.reserve(declared);
  for (std::uint32_t index = 0; index < declared; ++index)
  {
    DigestEntry entry;
    entry.kind = static_cast<DigestKind>(_reader.ReadU8());
    entry.severity = _reader.ReadU32();
    entry.title = _reader.ReadString();
    entry.detail = _reader.ReadString();
    entry.system = SystemId{_reader.ReadI32()};
    entry.lane = LaneId{_reader.ReadI32()};
    entry.fleet = FleetId{_reader.ReadI32()};
    entry.other = PlayerId{_reader.ReadI32()};

    if (_reader.Failed())
    {
      return {};
    }
    digest.push_back(std::move(entry));
  }
  return digest;
}

bool Snapshot::Knows(SystemId _system) const
{
  for (const SnapshotSystem& system : m_systems)
  {
    if (system.id == _system)
    {
      return true;
    }
  }
  return false;
}

const SnapshotSystem* Snapshot::System(SystemId _system) const
{
  for (const SnapshotSystem& system : m_systems)
  {
    if (system.id == _system)
    {
      return &system;
    }
  }
  return nullptr;
}

void Snapshot::Write(Neuron::ByteWriter& _writer) const
{
  _writer.WriteI32(m_viewer.Index());
  _writer.WriteU32(m_tick);

  _writer.WriteU32(static_cast<std::uint32_t>(m_systems.size()));
  for (const SnapshotSystem& system : m_systems)
  {
    _writer.WriteI32(system.id.Index());
    _writer.WriteString(system.name);
    _writer.WriteI32(system.positionX);
    _writer.WriteI32(system.positionY);
    _writer.WriteU8(static_cast<std::uint8_t>(system.kind));
    _writer.WriteBool(system.live);
    _writer.WriteU32(system.asOfTick);
    _writer.WriteI32(system.owner.Index());
    _writer.WriteBool(system.hasShipyard);
    _writer.WriteBool(system.hasMiningStation);
    _writer.WriteU32(system.siegeTicks);
    _writer.WriteU32(system.capturedAt);
    _writer.WriteBool(system.halfYield);
  }

  _writer.WriteU32(static_cast<std::uint32_t>(m_lanes.size()));
  for (const SnapshotLane& lane : m_lanes)
  {
    _writer.WriteI32(lane.id.Index());
    _writer.WriteI32(lane.a.Index());
    _writer.WriteI32(lane.b.Index());
    _writer.WriteU32(lane.costTicks);
    _writer.WriteBool(lane.tradeLane);
  }

  _writer.WriteU32(static_cast<std::uint32_t>(m_fleets.size()));
  for (const SnapshotFleet& fleet : m_fleets)
  {
    _writer.WriteI32(fleet.id.Index());
    _writer.WriteI32(fleet.owner.Index());
    _writer.WriteU32(fleet.ships);
    _writer.WriteI32(fleet.at.Index());
    _writer.WriteI32(fleet.movingFrom.Index());
    _writer.WriteI32(fleet.movingTo.Index());
    _writer.WriteU32(fleet.ticksRemaining);
    _writer.WriteString(fleet.preview);
    _writer.WriteU32(fleet.previewMine);
    _writer.WriteU32(fleet.previewTheirs);
    _writer.WriteU32(fleet.previewMineAfter);
    _writer.WriteU32(fleet.previewTheirsAfter);
    _writer.WriteU8(fleet.previewDefended ? 1U : 0U);
  }

  _writer.WriteU32(static_cast<std::uint32_t>(m_proposals.size()));
  for (const SnapshotProposal& proposal : m_proposals)
  {
    _writer.WriteI32(proposal.id.Index());
    _writer.WriteI32(proposal.from.Index());
    _writer.WriteI32(proposal.to.Index());
    _writer.WriteU8(static_cast<std::uint8_t>(proposal.kind));
    _writer.WriteI32(proposal.lane.Index());
    _writer.WriteI32(proposal.conditionalLane.Index());
    _writer.WriteU32(proposal.ticks);
    _writer.WriteU32(proposal.ticksLeft);
  }

  _writer.WriteU32(static_cast<std::uint32_t>(m_standings.size()));
  for (const SnapshotStanding& standing : m_standings)
  {
    _writer.WriteI32(standing.player.Index());
    _writer.WriteU32(standing.score);
    _writer.WriteU32(standing.placement);
    _writer.WriteU8(static_cast<std::uint8_t>(standing.status));
    _writer.WriteU32(standing.custodianSince);
  }

  _writer.WriteI32(m_regionAnchor.Index());
  _writer.WriteU32(m_regionOpensAt);
  _writer.WriteU32(m_totalSystems);
  _writer.WriteU32(m_unclaimedSystems);
  _writer.WriteU32(m_capitalGuardTicksLeft);
  _writer.WriteBool(m_finished);
}

Snapshot Snapshot::Read(Neuron::ByteReader& _reader)
{
  /// The same bound and the same reason as `OrderSet::Read`: a declared count is a number off a
  /// socket, and reserving on it is an allocation failure waiting for a malformed record.
  constexpr std::uint32_t MAXIMUM_ENTRIES = 4096;
  const auto count = [&_reader]
  {
    const std::uint32_t declared = _reader.ReadU32();
    return declared > MAXIMUM_ENTRIES ? 0U : declared;
  };

  Snapshot view;
  view.m_viewer = PlayerId{_reader.ReadI32()};
  view.m_tick = _reader.ReadU32();

  const std::uint32_t systemCount = count();
  view.m_systems.reserve(systemCount);
  for (std::uint32_t index = 0; index < systemCount; ++index)
  {
    SnapshotSystem system;
    system.id = SystemId{_reader.ReadI32()};
    system.name = _reader.ReadString();
    system.positionX = _reader.ReadI32();
    system.positionY = _reader.ReadI32();
    system.kind = static_cast<SystemKind>(_reader.ReadU8());
    system.live = _reader.ReadBool();
    system.asOfTick = _reader.ReadU32();
    system.owner = PlayerId{_reader.ReadI32()};
    system.hasShipyard = _reader.ReadBool();
    system.hasMiningStation = _reader.ReadBool();
    system.siegeTicks = _reader.ReadU32();
    system.capturedAt = _reader.ReadU32();
    system.halfYield = _reader.ReadBool();
    view.m_systems.push_back(std::move(system));
  }

  const std::uint32_t laneCount = count();
  view.m_lanes.reserve(laneCount);
  for (std::uint32_t index = 0; index < laneCount; ++index)
  {
    SnapshotLane lane;
    lane.id = LaneId{_reader.ReadI32()};
    lane.a = SystemId{_reader.ReadI32()};
    lane.b = SystemId{_reader.ReadI32()};
    lane.costTicks = _reader.ReadU32();
    lane.tradeLane = _reader.ReadBool();
    view.m_lanes.push_back(lane);
  }

  const std::uint32_t fleetCount = count();
  view.m_fleets.reserve(fleetCount);
  for (std::uint32_t index = 0; index < fleetCount; ++index)
  {
    SnapshotFleet fleet;
    fleet.id = FleetId{_reader.ReadI32()};
    fleet.owner = PlayerId{_reader.ReadI32()};
    fleet.ships = _reader.ReadU32();
    fleet.at = SystemId{_reader.ReadI32()};
    fleet.movingFrom = SystemId{_reader.ReadI32()};
    fleet.movingTo = SystemId{_reader.ReadI32()};
    fleet.ticksRemaining = _reader.ReadU32();
    fleet.preview = _reader.ReadString();
    fleet.previewMine = _reader.ReadU32();
    fleet.previewTheirs = _reader.ReadU32();
    fleet.previewMineAfter = _reader.ReadU32();
    fleet.previewTheirsAfter = _reader.ReadU32();
    fleet.previewDefended = _reader.ReadU8() != 0U;
    view.m_fleets.push_back(std::move(fleet));
  }

  const std::uint32_t proposalCount = count();
  view.m_proposals.reserve(proposalCount);
  for (std::uint32_t index = 0; index < proposalCount; ++index)
  {
    SnapshotProposal proposal;
    proposal.id = ProposalId{_reader.ReadI32()};
    proposal.from = PlayerId{_reader.ReadI32()};
    proposal.to = PlayerId{_reader.ReadI32()};
    proposal.kind = static_cast<ProposalKind>(_reader.ReadU8());
    proposal.lane = LaneId{_reader.ReadI32()};
    proposal.conditionalLane = LaneId{_reader.ReadI32()};
    proposal.ticks = _reader.ReadU32();
    proposal.ticksLeft = _reader.ReadU32();
    view.m_proposals.push_back(proposal);
  }

  const std::uint32_t standingCount = count();
  view.m_standings.reserve(standingCount);
  for (std::uint32_t index = 0; index < standingCount; ++index)
  {
    SnapshotStanding standing;
    standing.player = PlayerId{_reader.ReadI32()};
    standing.score = _reader.ReadU32();
    standing.placement = _reader.ReadU32();
    standing.status = static_cast<PlayerStatus>(_reader.ReadU8());
    standing.custodianSince = _reader.ReadU32();
    view.m_standings.push_back(standing);
  }

  view.m_regionAnchor = SystemId{_reader.ReadI32()};
  view.m_regionOpensAt = _reader.ReadU32();
  view.m_totalSystems = _reader.ReadU32();
  view.m_unclaimedSystems = _reader.ReadU32();
  view.m_capitalGuardTicksLeft = _reader.ReadU32();
  view.m_finished = _reader.ReadBool();
  return view;
}

} // namespace Lockstep
