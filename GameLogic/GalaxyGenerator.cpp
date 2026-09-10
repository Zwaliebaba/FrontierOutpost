// GalaxyGenerator.cpp -- the ring, and the constraints it has to satisfy.
//
// The layout is in GalaxyGenerator.h; the rules it is checked against are the one-pager's, in
// Validate. Generation and validation are deliberately separate: Validate reads only the finished
// graph, so it is a test of the RULES rather than a second run of the code that built it.

#include "pch.h"
#include "GalaxyGenerator.h"

#include "Prng.h"
#include "Turns16.h"

namespace Frontier
{

namespace
{

/// Names, embedded (R13: the executable ships alone, so there is no name file to load).
///
/// Sixty-four is enough for a twelve-player galaxy, which needs at most 12 capitals + 24 satellites
/// + 12 borders + 12 frontier = 60. They are drawn without replacement, so no match has two Vesks.
constexpr std::array<const char*, 64> SYSTEM_NAMES = {
  "Vesk",   "Orune",   "Tamsin", "Idris", "Narth",   "Pell",   "Dunmore", "Sorne",  "Okonkwo", "Kepler", "Halvorsen", "Ashen",  "Brannoc",
  "Calder", "Dothan",  "Ebbrey", "Fenn",  "Gharial", "Hollis", "Ithaca",  "Jorvik", "Kessel",  "Loden",  "Merrow",    "Nessa",  "Oberon",
  "Prahl",  "Quillon", "Riven",  "Selk",  "Torvald", "Ulme",   "Varn",    "Wexley", "Xander",  "Yarrow", "Zelen",     "Ambry",  "Belloq",
  "Cordis", "Dresh",   "Elgin",  "Faroe", "Gethen",  "Harrow", "Ivor",    "Jandal", "Kirel",   "Lasker", "Mordent",   "Nyx",    "Orrick",
  "Pyrra",  "Quarrel", "Reyne",  "Sable", "Thule",   "Umber",  "Vantage", "Wren",   "Xerev",   "Ysolde", "Zircon",    "Aubade",
};

/// Hands out names without repeating one, in an order fixed by the seed.
///
/// A Fisher-Yates shuffle over the indices rather than "draw and retry", because retry has no
/// bound: with sixty names and sixty draws the last one would be found by chance, eventually.
class NamePool
{
public:
  explicit NamePool(Neuron::Prng& _prng)
  {
    for (std::uint32_t index = 0; index < SYSTEM_NAMES.size(); ++index)
    {
      m_order[index] = index;
    }

    for (std::uint32_t index = static_cast<std::uint32_t>(SYSTEM_NAMES.size()) - 1; index > 0; --index)
    {
      const std::uint32_t swap = _prng.Below(index + 1);
      std::swap(m_order[index], m_order[swap]);
    }
  }

  [[nodiscard]] std::string Take()
  {
    ASSERT_TEXT(m_taken < SYSTEM_NAMES.size(), L"More systems than names. Add names, or generate fewer systems.");
    return SYSTEM_NAMES[m_order[m_taken++]];
  }

private:
  std::array<std::uint32_t, SYSTEM_NAMES.size()> m_order = {};
  std::size_t m_taken = 0;
};

/// A position on an ellipse about the design-space centre, nudged.
[[nodiscard]] GalaxySystem PlaceOnRing(Neuron::Prng& _prng, Neuron::Turns16 _angle, std::int32_t _radiusX, std::int32_t _radiusY)
{
  GalaxySystem placed;
  placed.positionX = GalaxyGenerator::CENTER_X + Neuron::OffsetAlong(_radiusX, Neuron::Cosine(_angle)) +
                     _prng.Between(-GalaxyGenerator::POSITION_JITTER, GalaxyGenerator::POSITION_JITTER);
  placed.positionY = GalaxyGenerator::CENTER_Y + Neuron::OffsetAlong(_radiusY, Neuron::Sine(_angle)) +
                     _prng.Between(-GalaxyGenerator::POSITION_JITTER, GalaxyGenerator::POSITION_JITTER);
  return placed;
}

} // namespace

const char* Describe(GalaxyRejection _rejection) noexcept
{
  switch (_rejection)
  {
  case GalaxyRejection::None:
    return "accepted";
  case GalaxyRejection::PlayerCountOutOfRange:
    return "player count is outside the 6-12 the design is drawn for";
  case GalaxyRejection::Disconnected:
    return "some system cannot be reached from some other";
  case GalaxyRejection::RivalTooFar:
    return "a capital has no rival capital within the required ticks";
  case GalaxyRejection::ClusterLaneNotOneTick:
    return "a lane inside one starting cluster does not cost one tick";
  case GalaxyRejection::FrontierLaneOutOfBand:
    return "a lane leaving a cluster is outside the frontier cost band";
  case GalaxyRejection::RegionUnreachable:
    return "the sealed region was not placed, or nothing reaches it";
  default:
    return "unknown";
  }
}

GalaxyRejection GalaxyGenerator::TryGenerate(const MatchRules& _rules, std::uint64_t _seed, Galaxy& _outGalaxy)
{
  if (_rules.playerCount < MINIMUM_PLAYERS || _rules.playerCount > MAXIMUM_PLAYERS)
  {
    return GalaxyRejection::PlayerCountOutOfRange;
  }

  _outGalaxy = Galaxy{};
  Neuron::Prng prng{_seed};
  NamePool names{prng};

  const std::uint32_t players = _rules.playerCount;

  // ---- The sealed region, at the centre --------------------------------------------------------
  //
  // The centre is the fairness decision. The test plan's Phase 2 asks that "no empire is
  // consistently positioned to dominate the region at opening"; a region nearer one capital than
  // another fails that before anybody plays. It is placed first so its id is stable at zero, which
  // makes a galaxy easier to read in a debugger and costs nothing.
  GalaxySystem regionSystem;
  regionSystem.name = "Sealed Region";
  regionSystem.kind = SystemKind::RegionAnchor;
  regionSystem.positionX = CENTER_X;
  regionSystem.positionY = CENTER_Y;
  const SystemId region = _outGalaxy.AddSystem(std::move(regionSystem));

  // ---- Capitals, evenly spaced on the outer ring ------------------------------------------------
  std::vector<SystemId> capitals;
  capitals.reserve(players);
  for (std::uint32_t player = 0; player < players; ++player)
  {
    const Neuron::Turns16 angle = Neuron::EvenlySpaced(player, players);
    GalaxySystem capital = PlaceOnRing(prng, angle, CAPITAL_RING_RADIUS_X, CAPITAL_RING_RADIUS_Y);
    capital.name = names.Take();
    capital.kind = SystemKind::Capital;
    capital.owner = PlayerId{static_cast<PlayerId::Underlying>(player)};
    capital.startingCluster = capital.owner;
    capitals.push_back(_outGalaxy.AddSystem(std::move(capital)));
  }

  // ---- Satellites, a tick from their capital ----------------------------------------------------
  for (std::uint32_t player = 0; player < players; ++player)
  {
    const Neuron::Turns16 capitalAngle = Neuron::EvenlySpaced(player, players);
    std::vector<SystemId> cluster;

    for (std::uint32_t satellite = 0; satellite < _rules.satellitesPerCapital; ++satellite)
    {
      // Fanned out AWAY from the centre, so a cluster sits behind its capital rather than between
      // it and the contested middle. It is the one-pager's dense start: your own systems are the
      // ones at your back.
      const auto spread = static_cast<Neuron::Turns16>(Neuron::EvenlySpaced(satellite + 1, _rules.satellitesPerCapital + 2) / 6);
      const auto outward = static_cast<Neuron::Turns16>(capitalAngle + spread - Neuron::EvenlySpaced(1, 12));

      GalaxySystem world = PlaceOnRing(prng, outward, CAPITAL_RING_RADIUS_X + SATELLITE_RADIUS, CAPITAL_RING_RADIUS_Y + SATELLITE_RADIUS);
      world.name = names.Take();
      world.kind = SystemKind::Satellite;
      world.startingCluster = PlayerId{static_cast<PlayerId::Underlying>(player)};
      const SystemId placed = _outGalaxy.AddSystem(std::move(world));

      // One tick from the capital, and one tick from the satellite before it: a starting cluster is
      // tight by rule, not by distance (one-pager, "Dense start, sparse frontier").
      (void)_outGalaxy.AddLane(capitals[player], placed, 1);
      if (!cluster.empty())
      {
        (void)_outGalaxy.AddLane(cluster.back(), placed, 1);
      }
      cluster.push_back(placed);
    }
  }

  // ---- Borders, and the three-tick rival ---------------------------------------------------------
  //
  // A border sits between two capitals and belongs to the earlier one's cluster. That asymmetry is
  // what makes the distance work out: capital -> border costs one because it is inside the cluster,
  // border -> the next capital costs two because it leaves it, and the two capitals are therefore
  // exactly three ticks apart -- "the generator guarantees each capital a rival capital within
  // three ticks" (one-pager), satisfied by construction rather than by luck.
  std::vector<SystemId> borders;
  borders.reserve(players);
  for (std::uint32_t player = 0; player < players; ++player)
  {
    const Neuron::Turns16 between =
      static_cast<Neuron::Turns16>(Neuron::EvenlySpaced(player, players) + Neuron::EvenlySpaced(1, players * 2));

    GalaxySystem border = PlaceOnRing(prng, between, CAPITAL_RING_RADIUS_X, CAPITAL_RING_RADIUS_Y);
    border.name = names.Take();
    border.kind = SystemKind::Border;
    border.startingCluster = PlayerId{static_cast<PlayerId::Underlying>(player)};
    borders.push_back(_outGalaxy.AddSystem(std::move(border)));
  }

  for (std::uint32_t player = 0; player < players; ++player)
  {
    const std::uint32_t next = (player + 1) % players;
    (void)_outGalaxy.AddLane(capitals[player], borders[player], 1);
    (void)_outGalaxy.AddLane(borders[player], capitals[next], _rules.frontierLaneMinimumTicks);
  }

  // ---- The frontier, inside the ring --------------------------------------------------------------
  const std::uint32_t frontierCount = std::max(_rules.frontierSystemsPerPlayer * players, 3U);
  std::vector<SystemId> frontier;
  frontier.reserve(frontierCount);
  for (std::uint32_t index = 0; index < frontierCount; ++index)
  {
    const Neuron::Turns16 angle = Neuron::EvenlySpaced(index, frontierCount);
    GalaxySystem world = PlaceOnRing(prng, angle, FRONTIER_RING_RADIUS_X, FRONTIER_RING_RADIUS_Y);
    world.name = names.Take();
    world.kind = SystemKind::Frontier;
    frontier.push_back(_outGalaxy.AddSystem(std::move(world)));
  }

  const auto frontierCost = [&prng, &_rules]()
  {
    return static_cast<std::uint32_t>(
      prng.Between(static_cast<std::int32_t>(_rules.frontierLaneMinimumTicks), static_cast<std::int32_t>(_rules.frontierLaneMaximumTicks)));
  };

  for (std::uint32_t index = 0; index < frontierCount; ++index)
  {
    // Ringed together, so the middle is somewhere a fleet can move through rather than a set of
    // dead ends hanging off the empires.
    (void)_outGalaxy.AddLane(frontier[index], frontier[(index + 1) % frontierCount], frontierCost());

    // Out to the nearest border, by angle. Integer arithmetic: which border's slice this frontier
    // system's angle falls in.
    const std::uint32_t nearestBorder = (index * players) / frontierCount;
    (void)_outGalaxy.AddLane(frontier[index], borders[nearestBorder], frontierCost());

    // In to the region. Every second one, so the region has several approaches without the middle
    // becoming a star with one hub.
    if ((index % 2) == 0)
    {
      (void)_outGalaxy.AddLane(frontier[index], region, frontierCost());
    }
  }

  return Validate(_outGalaxy, _rules);
}

GalaxyRejection GalaxyGenerator::Validate(const Galaxy& _galaxy, const MatchRules& _rules)
{
  if (!_galaxy.IsConnected())
  {
    return GalaxyRejection::Disconnected;
  }

  const SystemId region = _galaxy.RegionAnchor();
  if (!region.IsValid() || _galaxy.LanesAt(region).empty())
  {
    return GalaxyRejection::RegionUnreachable;
  }

  // Lane costs, against the two bands the one-pager states. A lane whose endpoints were generated
  // into the same starting cluster is "inside a starting cluster" and costs one tick; anything else
  // leaves a cluster and is a frontier lane.
  for (const GalaxyLane& lane : _galaxy.Lanes())
  {
    const GalaxySystem& from = _galaxy.SystemAt(lane.a);
    const GalaxySystem& to = _galaxy.SystemAt(lane.b);
    const bool sameCluster = from.startingCluster.IsValid() && from.startingCluster == to.startingCluster;

    if (sameCluster)
    {
      if (lane.costTicks != 1)
      {
        return GalaxyRejection::ClusterLaneNotOneTick;
      }
    }
    else if (lane.costTicks < _rules.frontierLaneMinimumTicks || lane.costTicks > _rules.frontierLaneMaximumTicks)
    {
      return GalaxyRejection::FrontierLaneOutOfBand;
    }
  }

  // Every capital needs a rival within the stated ticks. One sweep per capital rather than a pair
  // at a time; at twelve capitals that is twelve Dijkstras over sixty nodes.
  const std::vector<SystemId> capitals = _galaxy.Capitals();
  for (const SystemId capital : capitals)
  {
    const std::vector<std::uint32_t> reach = _galaxy.ShortestPathTicksFrom(capital);

    std::uint32_t nearestRival = Galaxy::UNREACHABLE;
    for (const SystemId rival : capitals)
    {
      if (rival != capital)
      {
        nearestRival = std::min(nearestRival, reach[rival.AsSize()]);
      }
    }

    if (nearestRival > _rules.maximumTicksToNearestRival)
    {
      return GalaxyRejection::RivalTooFar;
    }
  }

  return GalaxyRejection::None;
}

GeneratedGalaxy GalaxyGenerator::Generate(const MatchRules& _rules, std::uint64_t _firstSeed)
{
  GeneratedGalaxy result;

  for (std::uint32_t attempt = 0; attempt < _rules.maximumSeedAttempts; ++attempt)
  {
    // Attempts walk the seed through the PRNG's own mixer rather than by adding one, so that two
    // matches started a second apart do not get two galaxies that differ only in their jitter.
    const std::uint64_t seed = Neuron::Prng{_firstSeed + attempt}.Next();

    const GalaxyRejection rejection = TryGenerate(_rules, seed, result.galaxy);
    if (rejection == GalaxyRejection::None)
    {
      result.seed = seed;
      result.rejectedSeeds = attempt;
      return result;
    }

    if (attempt + 1 == _rules.maximumSeedAttempts)
    {
      // Not bad luck. The layout above satisfies the constraints by construction, so a full run of
      // refusals means the rules and the generator disagree -- which is a defect, and a broken
      // invariant is fatal in this tree (Debug.h).
      Neuron::Fatal("Galaxy generation refused {} seeds for {} players: {}.", _rules.maximumSeedAttempts, _rules.playerCount,
                    Describe(rejection));
    }
  }

  return result;
}

} // namespace Frontier
