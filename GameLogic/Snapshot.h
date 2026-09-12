#pragma once

#include "Match.h"
#include "TickLog.h"

#include "Archive.h"
#include "ByteReader.h"
#include "ByteWriter.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Lockstep
{

/// A system as one player sees it.
///
/// `live` distinguishes what is visible now from what is remembered. ADR-022 keeps a system known
/// at its LAST-SEEN state once seen, with the tick stamped, rather than letting it go dark -- so
/// `asOfTick` is the honest label on everything below it, and the client greys a system whose
/// stamp is old.
struct SnapshotSystem
{
  SystemId id;
  std::string name;
  std::int32_t positionX = 0;
  std::int32_t positionY = 0;
  SystemKind kind = SystemKind::Frontier;

  bool live = false;
  std::uint32_t asOfTick = 0;
  PlayerId owner;
  bool hasShipyard = false;
  bool hasMiningStation = false;

  /// Siege and custodian marks, reported only for a system the player can see NOW. A remembered
  /// system does not report a siege that may have ended three ticks ago.
  std::uint32_t siegeTicks = 0;
  std::uint32_t capturedAt = 0;
  bool halfYield = false;
};

/// A lane between two systems the player knows about.
struct SnapshotLane
{
  LaneId id;
  SystemId a;
  SystemId b;
  std::uint32_t costTicks = 1;
  /// Whether an open trade lane runs here. Lanes are public -- "lanes are public; cancelling one is
  /// a tell" -- so this is reported whether or not the player is on it.
  bool tradeLane = false;
};

/// A fleet the player can see.
///
/// A fleet in transit is PUBLIC once departed, by the one-pager's rule: commitment is blind at the
/// moment of choice and visible afterwards. A fleet parked at a system is visible only if the
/// system is.
struct SnapshotFleet
{
  FleetId id;
  PlayerId owner;
  std::uint32_t ships = 0;
  SystemId at;
  SystemId movingFrom;
  SystemId movingTo;
  std::uint32_t ticksRemaining = 0;

  /// What a fight at the destination would do, from visible information only. Empty when there is
  /// nothing to fight. Computed here (`TickResolver::Preview`) rather than on the client, which
  /// does not link `GameLogic` and must not learn a rule to phrase a number.
  std::string preview;

  /// The same fight as NUMBERS, so the client can say what it means.
  ///
  /// **The server owns the arithmetic and the client owns the sentence**, which is the split
  /// ADR-021 implies: `preview` is a phrase, and a phrase cannot be re-worded by a screen that
  /// wants to say "YOU LOSE" first. The design is explicit that a preview must be a verdict and
  /// never a bare `A v B`, and that it must always state *whose* ships remain, which a phrase
  /// reporting only the viewer's survivors cannot.
  ///
  /// Zero everywhere when there is nothing to fight, which is the same condition as an empty
  /// `preview`.
  std::uint32_t previewMine = 0;
  std::uint32_t previewTheirs = 0;
  std::uint32_t previewMineAfter = 0;
  std::uint32_t previewTheirsAfter = 0;
  /// Whether the defender holds the system, and so fights with the incumbent's bonus.
  bool previewDefended = false;
};

struct SnapshotProposal
{
  ProposalId id;
  PlayerId from;
  PlayerId to;
  ProposalKind kind = ProposalKind::OpenLane;
  LaneId lane;
  LaneId conditionalLane;
  std::uint32_t ticks = 0;
  std::uint32_t ticksLeft = 0;
};

struct SnapshotStanding
{
  PlayerId player;
  std::uint32_t score = 0;
  std::uint32_t placement = 0;
  PlayerStatus status = PlayerStatus::Active;
  std::uint32_t custodianSince = 0;
};

/// Everything one player is entitled to know, at one tick.
///
/// **The shape is `Lockstep::MatchState`'s**, which is the client's view model, but this is not
/// that type and must not become it: `GameLogic` is server-side and the client never links it
/// (AGENTS.md §2), so the two vocabularies meet in the executable's composition root and nowhere
/// else. What they share is a shape, not a header.
///
/// THE TEST THAT MATTERS IS THE NEGATIVE ONE. A snapshot is what gets sent, so anything in here
/// that the player should not know is a leak that no amount of client discipline can fix.
class Snapshot
{
public:
  /// Builds the view for one player from the authoritative state.
  [[nodiscard]] static Snapshot For(const Match& _match, PlayerId _player);

  /// The player's digest for the tick just resolved, sorted as the resolver sorted it.
  [[nodiscard]] static std::vector<DigestEntry> DigestFor(const TickLog& _log, PlayerId _player);

  /// A digest, encoded and decoded. Both halves live here so they cannot drift into different
  /// files and disagree about a field.
  static void WriteDigest(Neuron::ByteWriter& _writer, const std::vector<DigestEntry>& _digest);
  [[nodiscard]] static std::vector<DigestEntry> ReadDigest(Neuron::ByteReader& _reader);

  [[nodiscard]] PlayerId Viewer() const noexcept
  {
    return m_viewer;
  }
  [[nodiscard]] std::uint32_t Tick() const noexcept
  {
    return m_tick;
  }
  [[nodiscard]] const std::vector<SnapshotSystem>& Systems() const noexcept
  {
    return m_systems;
  }
  [[nodiscard]] const std::vector<SnapshotLane>& Lanes() const noexcept
  {
    return m_lanes;
  }
  [[nodiscard]] const std::vector<SnapshotFleet>& Fleets() const noexcept
  {
    return m_fleets;
  }
  [[nodiscard]] const std::vector<SnapshotProposal>& Proposals() const noexcept
  {
    return m_proposals;
  }
  [[nodiscard]] const std::vector<SnapshotStanding>& Standings() const noexcept
  {
    return m_standings;
  }
  [[nodiscard]] SystemId RegionAnchor() const noexcept
  {
    return m_regionAnchor;
  }
  [[nodiscard]] std::uint32_t RegionOpensAt() const noexcept
  {
    return m_regionOpensAt;
  }

  /// The authoritative counts, which are NOT what the player can see.
  ///
  /// "41 systems" over a map showing eleven is not an inconsistency, it is fog -- and the one-pager
  /// wants the player to know how much galaxy there is even before they have found it.
  [[nodiscard]] std::uint32_t TotalSystems() const noexcept
  {
    return m_totalSystems;
  }
  [[nodiscard]] std::uint32_t UnclaimedSystems() const noexcept
  {
    return m_unclaimedSystems;
  }

  [[nodiscard]] std::uint32_t CapitalGuardTicksLeft() const noexcept
  {
    return m_capitalGuardTicksLeft;
  }
  [[nodiscard]] bool IsFinished() const noexcept
  {
    return m_finished;
  }

  /// Whether this snapshot mentions a system at all. The negative test asks this.
  [[nodiscard]] bool Knows(SystemId _system) const;

  /// The system if this snapshot carries it, and `nullptr` if the viewer cannot see it.
  ///
  /// A null return is not an error: it is fog, and every caller has to decide what to do about a
  /// system it has only heard of through a lane. The bots (`BotPolicy`) treat it as "not worth
  /// walking toward"; the scripted match treats it as "cannot judge this lane".
  [[nodiscard]] const SnapshotSystem* System(SystemId _system) const;

  void Write(Neuron::ByteWriter& _writer) const;

  /// What a snapshot is made of, in wire order, for both directions at once (ADR-049). Private
  /// because it is the format and not the interface: `Write` and `Read` are what callers use.
  [[nodiscard]] static Snapshot Read(Neuron::ByteReader& _reader);

private:
  void Visit(Neuron::Archive& _archive);

public:
private:
  PlayerId m_viewer;
  std::uint32_t m_tick = 0;

  std::vector<SnapshotSystem> m_systems;
  std::vector<SnapshotLane> m_lanes;
  std::vector<SnapshotFleet> m_fleets;
  std::vector<SnapshotProposal> m_proposals;
  std::vector<SnapshotStanding> m_standings;

  SystemId m_regionAnchor;
  std::uint32_t m_regionOpensAt = 0;
  std::uint32_t m_totalSystems = 0;
  std::uint32_t m_unclaimedSystems = 0;
  std::uint32_t m_capitalGuardTicksLeft = 0;
  bool m_finished = false;
};

} // namespace Lockstep
