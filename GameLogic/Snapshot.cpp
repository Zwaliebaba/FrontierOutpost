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

#include "Archive.h"

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

  // ---- The purse and the prices -------------------------------------------------------------------
  //
  // The viewer's own credits and nobody else's, and the costs `Match::Validate` will hold the
  // next lock to (ADR-053).
  view.m_credits = _match.PlayerAt(_player).credits;
  view.m_shipyardCost = _match.Rules().shipyardCost;
  view.m_miningStationCost = _match.Rules().miningStationCost;
  view.m_tradeLaneCost = _match.Rules().tradeLaneCost;

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

namespace
{

/// The bound every list off a socket is read back under. A declared count is a number a peer chose,
/// and reserving on it is an allocation failure waiting for a malformed record.
constexpr std::uint32_t MAXIMUM_ENTRIES = 4096;

/// Each record in a snapshot, described once (ADR-049).
///
/// These were a `Write` and a `Read` apiece -- the same fields, in the same order, listed twice.
/// Five records, ten lists, and the failure mode of a pair that drifts is not a compile error: a
/// field added to one side decodes everything after it shifted by four bytes, which is a
/// valid-looking snapshot of nonsense.
void Visit(Neuron::Archive& _archive, DigestEntry& _entry)
{
  _archive.Enumerator(_entry.kind, DigestKind::MatchEnded);
  _archive.U32(_entry.severity);
  _archive.Text(_entry.title);
  _archive.Text(_entry.detail);
  _archive.Identity(_entry.system);
  _archive.Identity(_entry.lane);
  _archive.Identity(_entry.fleet);
  _archive.Identity(_entry.other);
}

void Visit(Neuron::Archive& _archive, SnapshotSystem& _system)
{
  _archive.Identity(_system.id);
  _archive.Text(_system.name);
  _archive.I32(_system.positionX);
  _archive.I32(_system.positionY);
  _archive.Enumerator(_system.kind, SystemKind::RegionAnchor);
  _archive.Boolean(_system.live);
  _archive.U32(_system.asOfTick);
  _archive.Identity(_system.owner);
  _archive.Boolean(_system.hasShipyard);
  _archive.Boolean(_system.hasMiningStation);
  _archive.U32(_system.siegeTicks);
  _archive.U32(_system.capturedAt);
  _archive.Boolean(_system.halfYield);
}

void Visit(Neuron::Archive& _archive, SnapshotLane& _lane)
{
  _archive.Identity(_lane.id);
  _archive.Identity(_lane.a);
  _archive.Identity(_lane.b);
  _archive.U32(_lane.costTicks);
  _archive.Boolean(_lane.tradeLane);
}

void Visit(Neuron::Archive& _archive, SnapshotFleet& _fleet)
{
  _archive.Identity(_fleet.id);
  _archive.Identity(_fleet.owner);
  _archive.U32(_fleet.ships);
  _archive.Identity(_fleet.at);
  _archive.Identity(_fleet.movingFrom);
  _archive.Identity(_fleet.movingTo);
  _archive.U32(_fleet.ticksRemaining);
  _archive.Text(_fleet.preview);
  _archive.U32(_fleet.previewMine);
  _archive.U32(_fleet.previewTheirs);
  _archive.U32(_fleet.previewMineAfter);
  _archive.U32(_fleet.previewTheirsAfter);
  _archive.Boolean(_fleet.previewDefended);
}

void Visit(Neuron::Archive& _archive, SnapshotProposal& _proposal)
{
  _archive.Identity(_proposal.id);
  _archive.Identity(_proposal.from);
  _archive.Identity(_proposal.to);
  _archive.Enumerator(_proposal.kind, ProposalKind::HoldForTicks);
  _archive.Identity(_proposal.lane);
  _archive.Identity(_proposal.conditionalLane);
  _archive.U32(_proposal.ticks);
  _archive.U32(_proposal.ticksLeft);
}

void Visit(Neuron::Archive& _archive, SnapshotStanding& _standing)
{
  _archive.Identity(_standing.player);
  _archive.U32(_standing.score);
  _archive.U32(_standing.placement);
  _archive.Enumerator(_standing.status, PlayerStatus::Gone);
  _archive.U32(_standing.custodianSince);
}

/// A counted list of anything above. The count is written from the vector's size and read back
/// under the bound, which is the one place either happens.
template <typename Record> void VisitList(Neuron::Archive& _archive, std::vector<Record>& _records)
{
  const std::uint32_t count = _archive.Count(_records.size(), MAXIMUM_ENTRIES);
  if (!_archive.Writing())
  {
    _records.assign(count, Record{});
  }
  for (Record& record : _records)
  {
    Visit(_archive, record);
  }
}

} // namespace

void Snapshot::WriteDigest(Neuron::ByteWriter& _writer, const std::vector<DigestEntry>& _digest)
{
  Neuron::Archive archive{_writer};
  VisitList(archive, const_cast<std::vector<DigestEntry>&>(_digest));
}

std::vector<DigestEntry> Snapshot::ReadDigest(Neuron::ByteReader& _reader)
{
  std::vector<DigestEntry> digest;
  Neuron::Archive archive{_reader};
  VisitList(archive, digest);
  return archive.Failed() ? std::vector<DigestEntry>{} : digest;
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
  Neuron::Archive archive{_writer};
  const_cast<Snapshot*>(this)->Visit(archive);
}

Snapshot Snapshot::Read(Neuron::ByteReader& _reader)
{
  Snapshot view;
  Neuron::Archive archive{_reader};
  view.Visit(archive);
  return archive.Failed() ? Snapshot{} : view;
}

void Snapshot::Visit(Neuron::Archive& _archive)
{
  _archive.Identity(m_viewer);
  _archive.U32(m_tick);

  VisitList(_archive, m_systems);
  VisitList(_archive, m_lanes);
  VisitList(_archive, m_fleets);
  VisitList(_archive, m_proposals);
  VisitList(_archive, m_standings);

  _archive.Identity(m_regionAnchor);
  _archive.U32(m_regionOpensAt);
  _archive.U32(m_totalSystems);
  _archive.U32(m_unclaimedSystems);
  _archive.U32(m_capitalGuardTicksLeft);
  _archive.Boolean(m_finished);

  _archive.U32(m_credits);
  _archive.U32(m_shipyardCost);
  _archive.U32(m_miningStationCost);
  _archive.U32(m_tradeLaneCost);
}

} // namespace Lockstep
