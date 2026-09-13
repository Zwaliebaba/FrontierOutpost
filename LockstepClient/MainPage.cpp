// MainPage.cpp -- the ops console: digest rail, map, orders rail.
//
// Design/Screens/README.md is the spec and every number here comes from it. ADR-014 records the
// decisions the spec did not settle and the places the 8x8 font could not carry the reference's
// copy.

#include "pch.h"
#include "MainPage.h"

#include "DigestView.h"

#include "DesignTokens.h"
#include "MapRender.h"
#include "MapView.h"

#include <algorithm>
#include <queue>

namespace Lockstep
{

namespace
{

using Neuron::Color;
using Neuron::Face;
using Neuron::FontRenderer;
using Neuron::ShapeRenderer;

/// The design tokens, once (Design/Screens/README.md "Design tokens"). Alphas are the spec's
/// fractions turned into bytes: 0.04 -> 10, 0.07 -> 18, 0.10 -> 26, and so on.

/// The filled grey a locked rail wears (SCREENS.md 06). Solid rather than an outline, because at
/// the lock the rail stops being a list of things you could change and becomes a receipt.

/// The BRIGHTEST star. Every other one is drawn at a share of this alpha, down to about a third,
/// so this is the top of a range rather than the whole sky's one tone -- which is why it is higher
/// than the flat 140 the old uniform field used: the faintest star here lands at 110 and the mean
/// near 150, so the field as a whole is about as present as it was and the bright ones stand out
/// of it (ADR-033).

/// The ground grid, in design units. It runs well past the graph so the plane still has a floor
/// under it when the camera swings round to a corner, and it is square so that turning it reveals
/// no edge the front view did not have.

/// Node geometry, in WORLD units now rather than as multiples of a depth scale: the camera turns
/// a world size into a screen size, which is what makes a system genuinely larger when it is
/// nearer (ADR-017). The numbers are the reference's, read as world units.
constexpr float REGION_RADIUS = 62.0F;
/// How high the sealed region's second ring floats. The reference lifted it by the ellipse's own
/// half-height; in world units that is about a quarter of the radius.
constexpr float REGION_VOLUME_HEIGHT = 26.0F;

/// A name in the rail's voice. Labels and headers are uppercase (DESIGN-GUIDELINES "Font"), and
/// the font has no case of its own to fall back on.
[[nodiscard]] Color EventColor(EventKind _kind) noexcept
{
  switch (_kind)
  {
  case EventKind::Contact:
    return Ink::AMBER;
  case EventKind::Proposal:
    return Ink::BLUE;
  case EventKind::Loss:
    return Ink::RED;
  case EventKind::Region:
    return Ink::PURPLE;
  case EventKind::Custodian:
  case EventKind::Economy:
  case EventKind::Ignored:
  default:
    return {214, 220, 228, 89};
  }
}

/// Where a system with this id sits in the view's own list, or NONE.
///
/// **A system id and a position in `graph.systems` are different numbers** (ADR-057): the graph is
/// fogged, so the tenth system a player can see is not system ten. A `BuildRow` carries the id,
/// because an order names one; everything the screen focuses names a position.
[[nodiscard]] std::int32_t PositionOfSystem(const MatchState& _state, std::int32_t _systemId) noexcept
{
  for (std::size_t index = 0; index < _state.graph.systems.size(); ++index)
  {
    if (_state.graph.systems[index].id == _systemId)
    {
      return static_cast<std::int32_t>(index);
    }
  }
  return EventRefs::NONE;
}

/// Which system a proposal is about: the far end of the lane it offers.
///
/// **The far end and not the near one**, because the near one is this player's own and the thing
/// they have not looked at is whose border the offer arrives from. A proposal that names no lane --
/// scouting, a hold -- is about nobody's system and focuses nothing.
[[nodiscard]] std::int32_t ProposalSystem(const MatchState& _state, const Proposal& _proposal) noexcept
{
  if (_proposal.conditionalLane == EventRefs::NONE)
  {
    return EventRefs::NONE;
  }

  for (const Lane& lane : _state.graph.lanes)
  {
    if (lane.id != _proposal.conditionalLane)
    {
      continue;
    }

    const auto systems = static_cast<std::int32_t>(_state.graph.systems.size());
    if (lane.a < 0 || lane.a >= systems || lane.b < 0 || lane.b >= systems)
    {
      return EventRefs::NONE;
    }
    return _state.graph.systems[static_cast<std::size_t>(lane.a)].owner == _state.viewer ? lane.b : lane.a;
  }
  return EventRefs::NONE;
}

/// 1284 -> "1,284". The reference groups thousands and the score is the number a player checks
/// first, so it is grouped here rather than left as a run of digits.
[[nodiscard]] std::string FormatScore(std::uint32_t _score)
{
  std::string digits = std::to_string(_score);
  for (std::size_t at = digits.size(); at > 3;)
  {
    at -= 3;
    digits.insert(at, ",");
  }
  return digits;
}

/// 4 -> "4TH". Ordinals, because "4 / 12" reads as a fraction and placement is not one.
[[nodiscard]] std::string FormatPlacement(std::uint32_t _placement)
{
  const std::uint32_t lastTwo = _placement % 100;
  const char* suffix = "TH";
  if (lastTwo < 11 || lastTwo > 13)
  {
    switch (_placement % 10)
    {
    case 1:
      suffix = "ST";
      break;
    case 2:
      suffix = "ND";
      break;
    case 3:
      suffix = "RD";
      break;
    default:
      break;
    }
  }
  return std::to_string(_placement) + suffix;
}

} // namespace

void MainPage::Create(MatchState _state)
{
  // What the player had open, so that a state arriving does not shut it (ADR-065). A sheet is where
  // somebody is in the middle of deciding something, and the tick landing under them is not a
  // reason to take it away -- it is the reason they opened it.
  const Panel wasOpen = m_panel;
  const std::int32_t wasSubject = m_panelSubject;
  const std::int32_t wasSubjectId = m_panelSubjectId;

  m_state = std::move(_state);
  m_panel = Panel::None;
  m_panelSubject = EventRefs::NONE;
  m_panelSubjectId = EventRefs::NONE;
  m_focusedSystem = EventRefs::NONE;

  // The arming is an index into the signal list, and the list is recomposed with the state. Kept,
  // it would be a row armed that nobody armed.
  m_armedConcede = EventRefs::NONE;

  // A digest is replaced wholesale every tick, so nothing about how the last one was being READ
  // survives it: page three is nowhere in the new one, and the rival whose card was open may have
  // no card at all (ADR-061).
  m_digestPage = 0;
  m_expandedActor = NOBODY;

  MeasureContent();
  ReopenPanel(wasOpen, wasSubjectId, wasSubject);
}

void MainPage::ReopenPanel(Panel _panel, std::int32_t _subjectId, std::int32_t _subject)
{
  switch (_panel)
  {
  case Panel::BuildList:
  {
    // Still yours, or there is nothing to build on it. `Action::OpenSystem` applies the same rule
    // (ADR-058) and this is the same question asked a tick later.
    const std::int32_t at = PositionOfSystem(m_state, _subjectId);
    if (at == EventRefs::NONE || m_state.graph.systems[static_cast<std::size_t>(at)].owner != m_state.viewer)
    {
      return;
    }
    m_panel = Panel::BuildList;
    m_panelSubject = at;
    m_panelSubjectId = _subjectId;
    return;
  }

  case Panel::Destination:
  {
    for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
    {
      if (m_state.fleets[index].id != _subjectId || m_state.fleets[index].owner != m_state.viewer)
      {
        continue;
      }
      m_panel = Panel::Destination;
      m_panelSubject = static_cast<std::int32_t>(index);
      m_panelSubjectId = _subjectId;
      return;
    }
    return;
  }

  case Panel::SignalList:
    // Composed from the new snapshot, and never empty -- a concede is always on it (ADR-039).
    m_panel = Panel::SignalList;
    m_panelSubject = 0;
    return;

  case Panel::Replay:
    // About the tick it named, which a newer tick does not make untrue.
    m_panel = Panel::Replay;
    m_panelSubject = _subject;
    return;

  case Panel::None:
  default:
    return;
  }
}

std::string MainPage::LockSentence() const
{
  return std::format("Resolving T{}. Controls return with the new digest. Anything you tap now is an order for T{}.", m_state.OrdersTick(),
                     m_state.OrdersTick() + 1);
}

void MainPage::MeasureContent()
{
  // What the camera has to fit: every system, and the sealed region out to its rim. Measured
  // once, from the graph, because the galaxy the server sends will not be the one in the fixture
  // and a distance tuned to this sample would frame nothing else (ADR-017).
  float minX = 0.0F;
  float maxX = 0.0F;
  float minY = 0.0F;
  float maxY = 0.0F;
  bool first = true;

  for (std::size_t index = 0; index < m_state.graph.systems.size(); ++index)
  {
    const SystemNode& node = m_state.graph.systems[index];
    // The region anchor draws no node but the region around it is 62 units wide, so it counts for
    // its rim rather than for its centre.
    const float reach = HasFlag(node.flags, SystemFlags::RegionAnchor) ? REGION_RADIUS : 0.0F;

    if (first)
    {
      minX = node.positionX - reach;
      maxX = node.positionX + reach;
      minY = node.positionY - reach;
      maxY = node.positionY + reach;
      first = false;
      continue;
    }
    minX = std::min(minX, node.positionX - reach);
    maxX = std::max(maxX, node.positionX + reach);
    minY = std::min(minY, node.positionY - reach);
    maxY = std::max(maxY, node.positionY + reach);
  }

  if (first)
  {
    m_contentCenter = {0.0F, 0.0F, 0.0F};
    m_contentRadius = 1.0F;
    return;
  }

  const float centerDesignX = (minX + maxX) * 0.5F;
  const float centerDesignY = (minY + maxY) * 0.5F;
  m_contentCenter = MapView::Ground(centerDesignX, centerDesignY);

  const float halfWidth = (maxX - minX) * 0.5F;
  const float halfDepth = (maxY - minY) * 0.5F;
  // The radius IN THE GROUND PLANE, which is what makes the framing independent of yaw: spin the
  // camera and the content is exactly this wide from every direction. Height is passed separately,
  // because a galaxy is flat and pretending otherwise wastes a quarter of the pane (MapView.h).
  m_contentRadius = std::sqrt(halfWidth * halfWidth + halfDepth * halfDepth);
}

std::string MainPage::FormatCountdown(double _seconds)
{
  // **Rounded UP, and that is the whole of it.** Truncating showed `00:00:00` for the entire last
  // second, while the rail still said UNLOCKED and still took edits -- so the screen said the
  // deadline had passed and then went on accepting orders, which is precisely the confusion screen
  // 06 exists to remove. With a ceiling, `00:00:01` means there is still a second of it and
  // `00:00:00` means there is not, so the clock reading zero and the rail reading LOCKED are the
  // same event.
  const auto total = static_cast<std::int64_t>(std::ceil(std::max(0.0, _seconds)));
  const std::int64_t hours = total / 3600;
  const std::int64_t minutes = (total % 3600) / 60;
  const std::int64_t remainder = total % 60;
  return std::format("{:02}:{:02}:{:02}", hours, minutes, remainder);
}

std::uint32_t MainPage::TicksTo(std::int32_t _fromSystem, std::int32_t _toSystem) const
{
  // Dijkstra over lane costs. Lane cost is authored per edge and is not a distance (one-pager),
  // so a shortest path here is a shortest TIME and that is what the picker shows.
  constexpr std::uint32_t UNREACHABLE = std::numeric_limits<std::uint32_t>::max();
  if (_fromSystem < 0 || _toSystem < 0)
  {
    return UNREACHABLE;
  }

  std::vector<std::uint32_t> best(m_state.graph.systems.size(), UNREACHABLE);
  using Entry = std::pair<std::uint32_t, std::int32_t>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> frontier;

  best[static_cast<std::size_t>(_fromSystem)] = 0;
  frontier.emplace(0U, _fromSystem);

  while (!frontier.empty())
  {
    const auto [cost, at] = frontier.top();
    frontier.pop();
    if (at == _toSystem)
    {
      return cost;
    }
    if (cost > best[static_cast<std::size_t>(at)])
    {
      continue;
    }

    for (const Lane& lane : m_state.graph.lanes)
    {
      std::int32_t other = EventRefs::NONE;
      if (lane.a == at)
      {
        other = lane.b;
      }
      else if (lane.b == at)
      {
        other = lane.a;
      }
      if (other == EventRefs::NONE)
      {
        continue;
      }

      const std::uint32_t through = cost + lane.cost;
      if (through < best[static_cast<std::size_t>(other)])
      {
        best[static_cast<std::size_t>(other)] = through;
        frontier.emplace(through, other);
      }
    }
  }

  return best[static_cast<std::size_t>(_toSystem)];
}

void MainPage::Update(double _elapsedSeconds)
{
  // Before the lock check, and deliberately: a fleet crossing a lane is crossing it while the tick
  // resolves, and a route that froze the moment the orders locked would say the opposite
  // (ADR-055).
  m_animationSeconds += static_cast<float>(_elapsedSeconds);

  if (m_state.orders.locked)
  {
    return;
  }

  m_state.match.secondsToLock -= _elapsedSeconds;
  if (m_state.match.secondsToLock <= 0.0)
  {
    // The lock. Everything the player has edited goes together -- fleet moves, builds and the
    // answer to the proposal -- and nothing on the rail is editable afterwards (one-pager: orders
    // are hidden until they lock). What happens NEXT is the server's: it resolves the tick and
    // sends a new digest. The client does not resolve anything, so the countdown simply stops.
    //
    // **An open sheet stays open** (ADR-065). It goes inert like everything else, and it says so in
    // its own header; closing it would take the board away from somebody mid-decision at the one
    // moment they can do nothing about it.
    m_state.match.secondsToLock = 0.0;
    m_state.orders.locked = true;
  }
}

bool MainPage::HandleDrag(const Neuron::PointerInput::Drag& _drag)
{
  // The gesture belongs to whatever was under the PRESS. A drag that began on the map keeps
  // rotating it as the finger crosses onto a rail, and a drag that began on a rail never starts.
  const float mapLeft = Frame::DIGEST_WIDTH;
  const float mapRight = Frame::SCREEN_WIDTH - Frame::ORDERS_WIDTH;
  const bool startedOnMap =
    _drag.originXPixels >= mapLeft && _drag.originXPixels < mapRight && _drag.originYPixels >= Frame::TOP_BAR_HEIGHT;
  if (!startedOnMap)
  {
    return false;
  }

  // Both axes now. Horizontal orbits the camera around the galaxy, vertical raises and lowers it
  // -- which is the difference the turntable could not express and the reason it did not feel
  // like a camera (ADR-017).
  m_mapView.Drag(_drag.deltaXPixels, _drag.deltaYPixels);
  return true;
}

void MainPage::AddHit(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, Action _action, std::int32_t _index)
{
  m_hits.push_back(HitRegion{_xPixels, _yPixels, _widthPixels, _heightPixels, _action, _index});
}

std::int32_t MainPage::RailRowUnderPointer() const noexcept
{
  for (std::size_t index = 0; index < m_railRows.size(); ++index)
  {
    const RailRow& row = m_railRows[index];
    const bool inside = m_pointerXPixels >= row.x && m_pointerXPixels < row.x + row.width && m_pointerYPixels >= row.y &&
                        m_pointerYPixels < row.y + row.height;
    if (inside)
    {
      return static_cast<std::int32_t>(index);
    }
  }
  return EventRefs::NONE;
}

bool MainPage::SetPointer(float _xPixels, float _yPixels)
{
  m_pointerXPixels = _xPixels;
  m_pointerYPixels = _yPixels;

  // Tested against the PREVIOUS frame's rows, exactly as a tap is: layout and hit testing are the
  // same code, so there is only one list and it is a frame old.
  const std::int32_t under = RailRowUnderPointer();
  if (under == m_hoveredRailRow)
  {
    return false;
  }
  m_hoveredRailRow = under;
  return true;
}

bool MainPage::Animating() const noexcept
{
  return std::ranges::any_of(m_state.fleets,
                             [](const Fleet& _fleet) { return _fleet.order == FleetStance::Move && _fleet.from != _fleet.to; });
}

std::uint32_t MainPage::BuildShortfall(std::int32_t _index) const noexcept
{
  if (_index < 0 || _index >= static_cast<std::int32_t>(m_state.orders.builds.size()))
  {
    return 0;
  }
  const std::uint32_t needed = m_state.orders.QueuedBuildCost() + m_state.orders.builds[static_cast<std::size_t>(_index)].cost;
  return needed > m_state.player.credits ? needed - m_state.player.credits : 0;
}

bool MainPage::HandleTap(float _xPixels, float _yPixels)
{
  // Reverse order, so the panel drawn last is hit first. Painter's order and hit order are the
  // same list read the two ways round, which is what stops a modal from being clickable-through.
  for (auto region = m_hits.rbegin(); region != m_hits.rend(); ++region)
  {
    const bool inside =
      _xPixels >= region->x && _xPixels < region->x + region->width && _yPixels >= region->y && _yPixels < region->y + region->height;
    if (!inside)
    {
      continue;
    }

    const bool editable = !m_state.orders.locked;
    switch (region->action)
    {
    case Action::FocusEvent:
    {
      // Tapping a digest event focuses what it is about. Reading and acting are the same gesture:
      // the event says a rival is at Kepler-Reach, and the tap puts Kepler-Reach under your eye.
      //
      // Bounds-checked, because the index came from a card and a card can be synthetic. This read
      // was unguarded and a -1 crashed the client; the region that produced it is gone now, and
      // this stays so the next one cannot.
      if (region->index < 0 || region->index >= static_cast<std::int32_t>(m_state.digest.size()))
      {
        return true;
      }
      const DigestEvent& event = m_state.digest[static_cast<std::size_t>(region->index)];
      m_focusedSystem = event.refs.system;
      m_panel = Panel::None;
      return true;
    }

    case Action::FocusSystem:
    {
      // Bounds-checked against the SYSTEMS, which is the array this index names.
      if (region->index < 0 || region->index >= static_cast<std::int32_t>(m_state.graph.systems.size()))
      {
        return true;
      }
      m_focusedSystem = region->index;
      m_panel = Panel::None;
      return true;
    }
    case Action::OpenSystem:
    {
      // **A build sheet opens on a system you HOLD, and on nothing else** (ADR-058). You cannot
      // build on somebody else's ground -- `Match::Validate` refuses it as `NotYourSystem` -- so a
      // sheet over a rival's capital is a list of orders that system cannot take.
      //
      // A rival's system still focuses, because the tap has to do something visible: a control
      // that silently ignores you is the defect this screen has already been bitten by twice.
      m_focusedSystem = region->index;
      m_armedConcede = EventRefs::NONE;

      const bool yours = region->index >= 0 && region->index < static_cast<std::int32_t>(m_state.graph.systems.size()) &&
                         m_state.graph.systems[static_cast<std::size_t>(region->index)].owner == m_state.viewer;
      m_panel = yours ? Panel::BuildList : Panel::None;
      m_panelSubject = yours ? region->index : EventRefs::NONE;
      m_panelSubjectId = yours ? m_state.graph.systems[static_cast<std::size_t>(region->index)].id : EventRefs::NONE;
      return true;
    }

    case Action::OpenFleet:
      if (region->index < 0 || region->index >= static_cast<std::int32_t>(m_state.fleets.size()))
      {
        return true;
      }
      m_panel = Panel::Destination;
      m_panelSubject = region->index;
      m_panelSubjectId = m_state.fleets[static_cast<std::size_t>(region->index)].id;
      return true;

    case Action::ToggleBuild:
    {
      if (!editable)
      {
        return true;
      }
      auto& queued = m_state.orders.queuedBuilds;
      const auto found = std::find(queued.begin(), queued.end(), region->index);
      if (found == queued.end())
      {
        // Refused here, by the rules the lock would refuse it by (ADR-053, ADR-069). The rows and
        // buttons that lead here already say so and are not targets, so this is the guard behind
        // them rather than the message -- and it is the only place `queuedBuilds` grows, which is
        // what makes it the guard rather than one of several.
        const bool rising = region->index >= 0 && region->index < static_cast<std::int32_t>(m_state.orders.builds.size()) &&
                            m_state.orders.builds[static_cast<std::size_t>(region->index)].rising;
        if (rising || !m_state.CanAffordBuild(region->index))
        {
          return true;
        }
        queued.push_back(region->index);
      }
      else
      {
        queued.erase(found);
      }
      return true;
    }

    case Action::OpenSignals:
      m_panel = Panel::SignalList;
      m_panelSubject = 0;
      m_panelSubjectId = EventRefs::NONE;
      m_armedConcede = EventRefs::NONE;
      return true;

    case Action::ToggleSignal:
    {
      if (!editable || region->index < 0 || region->index >= static_cast<std::int32_t>(m_state.orders.signals.size()))
      {
        return true;
      }

      auto& queued = m_state.orders.queuedSignals;
      const auto found = std::find(queued.begin(), queued.end(), region->index);
      if (found != queued.end())
      {
        // Taking one back, including a concede that was queued a moment ago. It has not resolved,
        // so it is still an edit like any other.
        queued.erase(found);
        m_armedConcede = EventRefs::NONE;
        return true;
      }

      // Conceding takes two taps on the same row. The first arms it and the row says so; anything
      // else disarms it.
      if (m_state.orders.signals[static_cast<std::size_t>(region->index)].kind == SignalKind::Concede && m_armedConcede != region->index)
      {
        m_armedConcede = region->index;
        return true;
      }

      queued.push_back(region->index);
      m_armedConcede = EventRefs::NONE;
      return true;
    }

    case Action::AcceptProposal:
    case Action::DeclineProposal:
      // Answering a proposal IS an order. It is recorded here and committed at the lock with the
      // fleet moves and the builds, which is what makes agreement move at the same speed as
      // betrayal (one-pager, decision 3).
      if (editable)
      {
        // One answer per offer (ADR-068). Answering the same one again replaces its answer rather
        // than sending two, which is what makes changing an answer before the lock one tap.
        const bool accepted = region->action == Action::AcceptProposal;
        const auto existing = std::find_if(m_state.orders.answers.begin(), m_state.orders.answers.end(),
                                           [region](const ProposalAnswer& _answer) { return _answer.proposal == region->index; });
        if (existing != m_state.orders.answers.end())
        {
          existing->accepted = accepted;
        }
        else
        {
          m_state.orders.answers.push_back(ProposalAnswer{.proposal = region->index, .accepted = accepted});
        }
      }
      return true;

    case Action::ChooseDestination:
      if (editable && m_panelSubject >= 0)
      {
        Fleet& fleet = m_state.fleets[static_cast<std::size_t>(m_panelSubject)];
        const std::int32_t origin = fleet.order == FleetStance::Move ? fleet.to : fleet.from;
        fleet.from = origin;
        fleet.to = region->index;
        fleet.order = FleetStance::Move;
        fleet.progress = 0.0F;
        fleet.eta = m_state.OrdersTick() + TicksTo(origin, region->index) - 1;
        fleet.status = std::format("ordered - ETA T{}", fleet.eta);
      }
      m_panel = Panel::None;
      return true;

    case Action::ToggleActorCard:
      // One open at a time, so tapping a second card's title closes the first. The column has room
      // for one card's worth of lines and paging two open cards apart is not reading them.
      m_expandedActor = m_expandedActor == region->index ? NOBODY : region->index;
      return true;

    case Action::ShowDigestPage:
      m_digestPage = static_cast<std::size_t>(std::max(0, region->index));
      return true;

    case Action::OpenReplay:
      m_panel = Panel::Replay;
      m_panelSubject = static_cast<std::int32_t>(m_state.match.tick);
      m_panelSubjectId = EventRefs::NONE;
      return true;

    case Action::ClosePanel:
      m_panel = Panel::None;
      return true;

    case Action::None:
    default:
      break;
    }
  }

  return false;
}

void MainPage::DrawWorld(ShapeRenderer& _shapes, FontRenderer& _text)
{
  m_hits.clear();

  _shapes.FillRect(0.0F, 0.0F, Frame::SCREEN_WIDTH, Frame::SCREEN_HEIGHT, Ink::APP_BACKGROUND);

  // THE MAP GOES FIRST, and the rails are painted over it. With the authored curve the map could
  // not leave its pane; a camera can put a projected label or a lane anywhere on the screen, so the
  // rails' own opaque backgrounds are what confine it (ADR-017).
  //
  // It lives in `MapRender` now (ADR-045) and hands back what it drew rather than reaching into
  // this page's hit list: the map knows a system from a fleet, and this knows what tapping one
  // does.
  const MapFrame frame{.state = m_state,
                       .view = m_mapView,
                       .sky = m_sky,
                       .contentCenter = m_contentCenter,
                       .contentRadius = m_contentRadius,
                       .focusedSystem = m_focusedSystem,
                       .animationSeconds = m_animationSeconds};

  for (const MapHit& hit : Lockstep::DrawMap(_shapes, _text, frame))
  {
    const bool isSystem = hit.system != EventRefs::NONE;
    AddHit(hit.x, hit.y, hit.width, hit.height, isSystem ? Action::OpenSystem : Action::OpenFleet, isSystem ? hit.system : hit.fleet);
  }
}

void MainPage::DrawInterface(ShapeRenderer& _shapes, FontRenderer& _text)
{
  DrawTopBar(_shapes, _text);
  DrawDigestRail(_shapes, _text);
  DrawLocksRail(_shapes, _text);
  DrawPanel(_shapes, _text);
}

void MainPage::DrawTopBar(ShapeRenderer& _shapes, FontRenderer& _text)
{
  const std::int32_t centered = CenterTextY(0.0F, Frame::TOP_BAR_HEIGHT);
  _shapes.FillRect(0.0F, 0.0F, Frame::SCREEN_WIDTH, Frame::TOP_BAR_HEIGHT, Ink::APP_BACKGROUND);
  _shapes.FillRect(0.0F, Frame::TOP_BAR_HEIGHT - 1.0F, Frame::SCREEN_WIDTH, 1.0F, Ink::CARD_BORDER);

  // ---- The right-hand block goes first, and it decides how much room the left one gets ----------
  //
  // The left half is a sentence that grows -- the match id, the day, the player and system counts,
  // and an end time when there is one -- and the right half is a fixed set of facts laid out from
  // the edge inwards. Drawn in the obvious order, the sentence ran under the countdown: `ENDS 22
  // SEP 18:00Z` and `T47 LOCKS` printed on top of each other, which is what a fixed 1280 costs when
  // one side is authored and the other is data. So the right side is measured first and the left is
  // trimmed to fit in front of it.
  // The right group is laid out right to left, because it is anchored to the frame edge and its
  // widest member -- the leader's name -- is the one that changes.
  float cursor = Frame::SCREEN_WIDTH - 16.0F;

  const std::string replayLabel = std::format("REPLAY T{}", m_state.match.tick);
  const float replayWidth = 10.0F + 7.0F + 6.0F + static_cast<float>(FontRenderer::MeasurePixels(replayLabel)) + 10.0F;
  const float replayX = cursor - replayWidth;
  _shapes.StrokeRect(replayX, 13.0F, replayWidth, 22.0F, Ink::OUTLINE);
  // The one glyph the font does not have and does not need: the replay triangle is geometry, as
  // it is in the reference (README "Assets").
  _shapes.FillTriangle(replayX + 10.0F, 19.0F, replayX + 17.0F, 24.0F, replayX + 10.0F, 29.0F, Ink::TEXT_PRIMARY);
  _text.DrawText(static_cast<std::int32_t>(replayX + 23.0F), centered, replayLabel, Ink::TEXT_PRIMARY);
  AddHit(replayX, 13.0F, replayWidth, 22.0F, Action::OpenReplay, 0);
  cursor = replayX - 14.0F;

  _shapes.FillRect(cursor, 13.0F, 1.0F, 22.0F, Ink::CARD_BORDER);
  cursor -= 15.0F;

  // **Only when the leader is somebody else** (ADR-056). "Public score, the leader is always
  // visible" is the anti-snowball, and it is about knowing who is ahead of you -- so when that is
  // you, the line says your own score back to you next to the chip that already says `1ST / 6`,
  // and `LDR YOU 0` is three words for a fact the bar states twice over.
  const bool someoneElseLeads = m_state.player.placement != 1 && !m_state.player.leader.name.empty();
  if (someoneElseLeads)
  {
    const std::string leaderLine = std::format("LDR {} {}", m_state.player.leader.name, FormatScore(m_state.player.leader.score));
    DrawRight(_text, cursor, centered, leaderLine, Ink::TEXT_MUTED);
    cursor -= static_cast<float>(FontRenderer::MeasurePixels(leaderLine)) + 8.0F;
  }

  const std::string placement = std::format("{} / {}", FormatPlacement(m_state.player.placement), m_state.player.playerCount);
  const float chipWidth = static_cast<float>(FontRenderer::MeasurePixels(placement)) + 14.0F;
  _shapes.StrokeRect(cursor - chipWidth, 14.0F, chipWidth, 20.0F, Ink::OUTLINE);
  _text.DrawText(static_cast<std::int32_t>(cursor - chipWidth + 7.0F), centered, placement, Ink::TEXT_PRIMARY);
  cursor -= chipWidth + 8.0F;

  const std::string score = FormatScore(m_state.player.score);
  DrawRight(_text, cursor, centered, score, Ink::TEXT_PRIMARY);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(score)) + 8.0F;

  DrawRight(_text, cursor, centered, "SCORE", Ink::TEXT_MUTED);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels("SCORE")) + 14.0F;

  _shapes.FillRect(cursor, 13.0F, 1.0F, 22.0F, Ink::CARD_BORDER);
  cursor -= 15.0F;

  // The purse, beside the score and in the same weight (ADR-053). It is the number every build
  // on the screen is priced against, and it belongs where the eye already goes for the score
  // rather than inside a sentence on the production card.
  const std::string credits = std::format("{} CR", m_state.player.credits);
  DrawRight(_text, cursor, centered, credits, Ink::TEXT_PRIMARY);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(credits)) + 14.0F;

  _shapes.FillRect(cursor, 13.0F, 1.0F, 22.0F, Ink::CARD_BORDER);
  cursor -= 15.0F;

  // The countdown is the one thing on the screen drawn at 2x, and it is amber because amber is
  // the warning colour: this is the deadline every order on the rail is racing (README "Frame").
  const std::string countdown = m_state.match.finished ? std::string{"--:--:--"} : FormatCountdown(m_state.match.secondsToLock);
  const std::int32_t bigY = CenterTextY(0.0F, Frame::TOP_BAR_HEIGHT, FontRenderer::DEFAULT_FACE, FontRenderer::COUNTDOWN_SCALE);

  // **Amber is the deadline colour, and at zero there is no deadline left to warn about** (screen
  // 06). A countdown that stayed amber on 00:00:00 read as "hurry" to a player who could no longer
  // do anything, which is the opposite of what the number means once it has run out.
  const bool atLock = m_state.orders.locked && !m_state.match.finished;
  DrawRight(_text, cursor, bigY, countdown, atLock ? Ink::NEUTRAL_DIM : Ink::AMBER, Face::MonoSemiBold, FontRenderer::COUNTDOWN_SCALE);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(countdown, FontRenderer::DEFAULT_FACE, FontRenderer::COUNTDOWN_SCALE)) + 8.0F;

  const std::string lockLabel = m_state.match.finished ? std::string{"MATCH ENDED"}
                                : atLock               ? std::format("T{} LOCKED", m_state.OrdersTick())
                                                       : std::format("T{} LOCKS", m_state.OrdersTick());
  DrawRight(_text, cursor, centered, lockLabel, Ink::TEXT_MUTED);

  // ---- The left half, trimmed to what is left --------------------------------------------------
  const float titleWidth = static_cast<float>(FontRenderer::MeasurePixels("LOCKSTEP"));
  const float lineX = 16.0F + titleWidth + 10.0F;
  const float room = cursor - 14.0F - lineX;

  _text.DrawText(16, centered, "LOCKSTEP", Ink::TEXT_PRIMARY, Face::MonoMedium);

  // "DAY 12/21" rather than "DAY 12 / 21", and the countdown and replay labels use T-notation: at
  // 8px the reference's spelled-out bar is 63px wider than the frame (ADR-014).
  //
  // `M0419 - D12/21 - 12 PLAYERS - 61 SYSTEMS` (SCREENS.md 01). The census moved up here from the
  // map pane, where it was a caption on a picture; on the top bar it sits with the other facts
  // about the match that do not change from tick to tick.
  //
  // Dropped a clause at a time rather than clipped mid-word: every version below is a true and
  // readable line, and the end time goes before the census because a player who wants the end date
  // can read it off the day counter. The end time is also dropped rather than left dangling when
  // the state has none -- a match generated without a server has no schedule to report
  // (GeneratedMatch.h), and "- ENDS" followed by nothing reads as a truncation bug.
  const std::string census = std::format("{} PLAYERS - {} SYSTEMS", m_state.player.playerCount, m_state.totalSystems);
  const std::string stem = std::format("M{} - D{}/{}", m_state.match.id, m_state.match.day, m_state.match.totalDays);

  std::vector<std::string> candidates;
  if (!m_state.match.endsAt.empty())
  {
    candidates.push_back(std::format("{} - {} - ENDS {}", stem, census, m_state.match.endsAt));
  }
  candidates.push_back(std::format("{} - {}", stem, census));
  candidates.push_back(stem);

  for (const std::string& candidate : candidates)
  {
    if (static_cast<float>(FontRenderer::MeasurePixels(candidate)) <= room || &candidate == &candidates.back())
    {
      _text.DrawText(static_cast<std::int32_t>(lineX), centered, candidate, Ink::TEXT_MUTED);

      // A disconnected client says so, in the one place a player is already looking. Everything
      // else on this screen is the last thing the server said, and without this there is no way to
      // tell that from the current thing the server is saying.
      if (!m_state.connected)
      {
        const float offlineX = lineX + static_cast<float>(FontRenderer::MeasurePixels(candidate)) + 12.0F;
        if (offlineX + static_cast<float>(FontRenderer::MeasurePixels("RECONNECTING")) < cursor - 14.0F)
        {
          _text.DrawText(static_cast<std::int32_t>(offlineX), centered, "RECONNECTING", Ink::RED);
        }
      }
      break;
    }
  }
}

MainPage::CardLayout MainPage::LayoutCard(const DigestCard& _card, std::uint32_t _widthPixels) const
{
  CardLayout layout;

  // **An actor card is collapsed unless it is the open one** (ADR-061). It is the only card whose
  // lines are a LIST -- one per event the rival produced -- so it is the only one whose body can be
  // dropped without dropping a fact the card is the only record of: the title still names the
  // rival, the stamp still counts them, and opening it is one tap away.
  layout.collapsible = _card.actor != NOBODY;
  const bool collapsed = layout.collapsible && _card.actor != m_expandedActor;

  if (!collapsed)
  {
    for (const std::string& detail : _card.lines)
    {
      for (std::string& line : FontRenderer::WrapToWidth(detail, _widthPixels))
      {
        layout.details.push_back(std::move(line));
      }
    }
  }

  layout.hasVerdict = !_card.verdict.empty();
  if (layout.hasVerdict)
  {
    // The verdict box is inset two characters from the card's text column. It was two COLUMNS
    // until 2026-09-13; two advances is the same inset while the font is fixed-pitch, and the
    // one that still means "two characters" when it is not (ADR-073).
    layout.verdictDetail = FontRenderer::WrapToWidth(_card.verdictDetail, _widthPixels - 2U * FontRenderer::AdvancePixels());
  }
  layout.hasActions = !_card.actions.empty();

  // The same arithmetic the draw below walks, in one expression: the title block, a line per detail,
  // the verdict box, the action row, and the gap to the next card's divider.
  const auto lines = static_cast<float>(LINE_HEIGHT);
  layout.height = 11.0F + lines + 2.0F + static_cast<float>(layout.details.size()) * lines + 4.0F;
  if (layout.hasVerdict)
  {
    layout.height += 4.0F + (1.0F + static_cast<float>(layout.verdictDetail.size())) * lines + 6.0F;
  }
  if (layout.hasActions)
  {
    layout.height += 4.0F + lines + 4.0F;
  }
  return layout;
}

void MainPage::DrawDigestRail(ShapeRenderer& _shapes, FontRenderer& _text)
{
  // **The digest is the order surface** (ADR-034, SCREENS.md 01). Every event carries what can be
  // done about it, because the thing a player wants to do is always about something that happened,
  // and a menu somewhere else is a second place to look.
  _shapes.FillRect(0.0F, Frame::TOP_BAR_HEIGHT, Frame::DIGEST_WIDTH, Frame::SCREEN_HEIGHT - Frame::TOP_BAR_HEIGHT, Ink::APP_BACKGROUND);
  _shapes.FillRect(Frame::DIGEST_WIDTH - 1.0F, Frame::TOP_BAR_HEIGHT, 1.0F, Frame::SCREEN_HEIGHT - Frame::TOP_BAR_HEIGHT, Ink::CARD_BORDER);

  const std::int32_t headerY = static_cast<std::int32_t>(Frame::TOP_BAR_HEIGHT) + 12;
  const bool returning = m_state.unreadTicks >= 2;

  if (returning)
  {
    // `SINCE YOU LOOKED - T43 > T46` and a chip. It is the first line a returning player reads and
    // it says how much of the match happened without them.
    _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), headerY,
                   std::format("SINCE YOU LOOKED - T{} > T{}", m_state.lastSeenTick, m_state.match.tick), Ink::TEXT_MUTED);

    const std::string chip = std::format("{} TICKS", m_state.unreadTicks);
    const float chipWidth = static_cast<float>(FontRenderer::MeasurePixels(chip)) + 14.0F;
    _shapes.StrokeRect(Frame::DIGEST_WIDTH - RAIL_PADDING - chipWidth, static_cast<float>(headerY) - 5.0F, chipWidth, 18.0F, Ink::AMBER);
    _text.DrawText(static_cast<std::int32_t>(Frame::DIGEST_WIDTH - RAIL_PADDING - chipWidth + 7.0F), headerY, chip, Ink::AMBER);
  }
  else
  {
    _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), headerY, std::format("DIGEST - TICK {}", m_state.match.tick), Ink::TEXT_MUTED);

    // At the lock the right-hand figure stops being a count of what is here and becomes the tick
    // that is being resolved. It is the only thing on this column that changes at zero, and it is
    // what says the digest below is about to be replaced rather than simply short.
    const bool pending = m_state.orders.locked && !m_state.match.finished;
    DrawRight(_text, Frame::DIGEST_WIDTH - RAIL_PADDING, headerY,
              pending ? std::format("T{} PENDING", m_state.OrdersTick()) : std::format("{} EVENTS", m_state.digest.size()),
              pending ? Ink::AMBER : Ink::TEXT_MUTED);
  }

  constexpr float TEXT_LEFT = RAIL_PADDING + 8.0F + 10.0F;
  const float textWidth = Frame::DIGEST_WIDTH - TEXT_LEFT - RAIL_PADDING;
  const auto cardWidth = static_cast<std::uint32_t>(textWidth);

  float y = Frame::TOP_BAR_HEIGHT + 28.0F;

  // ---- The delta -----------------------------------------------------------------------------------
  const DigestDelta delta = DeltaOf(m_state);
  if (delta.Any())
  {
    // Two cells to a row, rounded up. The division is integer ON PURPOSE -- three cells is two rows
    // -- and it is done before the conversion rather than inside it, because a `/` under a
    // `static_cast<float>` reads like a float division somebody got wrong.
    const std::size_t rows = (delta.cells.size() + 1) / 2;
    const float boxHeight = static_cast<float>(rows) * static_cast<float>(LINE_HEIGHT) + 12.0F;
    _shapes.StrokeRect(RAIL_PADDING, y, Frame::DIGEST_WIDTH - 2.0F * RAIL_PADDING, boxHeight, Ink::AMBER);

    // Two columns, because four short facts in one line wrap badly at 8px and four stacked lines
    // are a list rather than a summary.
    for (std::size_t index = 0; index < delta.cells.size(); ++index)
    {
      const float cellX = RAIL_PADDING + 8.0F + static_cast<float>(index % 2) * (Frame::DIGEST_WIDTH - 2.0F * RAIL_PADDING) * 0.5F;
      const std::int32_t cellY = static_cast<std::int32_t>(y) + 6 + static_cast<std::int32_t>(index / 2) * LINE_HEIGHT;
      _text.DrawText(static_cast<std::int32_t>(cellX), cellY, delta.cells[index],
                     delta.cells[index].front() == '-' ? Ink::RED : Ink::AMBER);
    }
    y += boxHeight + 6.0F;
  }

  // ---- The cards, and which of them are on this page -------------------------------------------------
  //
  // **Nothing scrolls (ADR-052 option C), so a stack that does not fit is PAGED** (ADR-061). The
  // whole stack is measured first, because a page break has to fall between two cards and the only
  // way to know where one card ends is to have worked out how tall it is.
  const std::vector<DigestCard> cards = CardsOf(m_state);
  std::vector<CardLayout> layouts;
  layouts.reserve(cards.size());
  float stackHeight = 0.0F;
  for (const DigestCard& card : cards)
  {
    layouts.push_back(LayoutCard(card, cardWidth));
    stackHeight += layouts.back().height;
  }

  const float cardsTop = y;
  const bool paged = stackHeight > Frame::SCREEN_HEIGHT - cardsTop;
  const float room = Frame::SCREEN_HEIGHT - cardsTop - (paged ? DIGEST_PAGE_HEIGHT : 0.0F);

  // The first card of each page. A page always takes at least one card, even one taller than the
  // column: a card that fits nowhere is still better read cut off than not drawn at all.
  std::vector<std::size_t> pageStarts{0};
  float used = 0.0F;
  for (std::size_t index = 0; index < layouts.size(); ++index)
  {
    if (used > 0.0F && used + layouts[index].height > room)
    {
      pageStarts.push_back(index);
      used = 0.0F;
    }
    used += layouts[index].height;
  }

  // **The leading card is always on page one**, which is what keeps the standing moves reachable
  // (ADR-056): they are attached to `cards.front()` and page one starts there by construction.
  m_digestPage = std::min(m_digestPage, pageStarts.size() - 1);
  const std::size_t firstCard = pageStarts[m_digestPage];
  const std::size_t lastCard = m_digestPage + 1 < pageStarts.size() ? pageStarts[m_digestPage + 1] : cards.size();

  for (std::size_t cardIndex = firstCard; cardIndex < lastCard; ++cardIndex)
  {
    const DigestCard& card = cards[cardIndex];
    const CardLayout& layout = layouts[cardIndex];
    const Color accent = EventColor(card.kind);
    const float top = y;

    // Where this card's hits begin. The card as a whole is tappable -- reading and focusing are the
    // same gesture -- but its buttons sit inside it, and `HandleTap` reads the list BACKWARDS so
    // that the thing drawn last wins. A card-wide region appended after the buttons therefore
    // swallows every one of them, which is exactly what happened: tapping BUILD focused the event
    // instead, and the only reason it looked like it worked is that any handled tap sends the
    // order set. The card's region is inserted here instead, in front of its own buttons.
    const std::size_t cardHitsBegin = m_hits.size();

    _shapes.FillRect(0.0F, y, Frame::DIGEST_WIDTH - 1.0F, 1.0F, Ink::DIVIDER);
    std::int32_t lineY = static_cast<std::int32_t>(y) + 11;

    _shapes.FillEllipse(RAIL_PADDING + 4.0F, static_cast<float>(lineY) + 4.0F, 4.0F, 4.0F, accent);
    _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT), lineY, Uppercased(card.title), Ink::TEXT_PRIMARY, Face::MonoMedium);
    if (!card.stamp.empty())
    {
      DrawRight(_text, Frame::DIGEST_WIDTH - RAIL_PADDING, lineY, card.stamp, Ink::TEXT_MUTED);
    }

    // The title of an actor card opens and closes it. Registered here rather than after the card's
    // own region, so that it wins: the region is INSERTED at `cardHitsBegin` below, which puts
    // everything added during the card in front of it in the reverse walk `HandleTap` makes.
    if (layout.collapsible)
    {
      AddHit(0.0F, top, Frame::DIGEST_WIDTH - 1.0F, DIGEST_TITLE_HEIGHT, Action::ToggleActorCard, card.actor);
    }
    lineY += LINE_HEIGHT + 2;

    for (const std::string& line : layout.details)
    {
      _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT), lineY, line, Ink::TEXT_DETAIL, Face::SansRegular);
      lineY += LINE_HEIGHT;
    }

    // ---- The verdict box ---------------------------------------------------------------------------
    //
    // Always a verdict and never a bare `A v B` (DESIGN-GUIDELINES "Copy"), and the second line
    // always says whose ships remain -- which is why the snapshot carries both sides now.
    if (layout.hasVerdict)
    {
      lineY += 4;
      const std::vector<std::string>& detail = layout.verdictDetail;
      const float boxTop = static_cast<float>(lineY) - 5.0F;
      const float boxHeight = static_cast<float>(1 + detail.size()) * static_cast<float>(LINE_HEIGHT) + 10.0F;
      _shapes.StrokeRect(TEXT_LEFT, boxTop, Frame::DIGEST_WIDTH - TEXT_LEFT - RAIL_PADDING, boxHeight, Ink::AMBER);

      _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT) + 6, lineY, card.verdict, Ink::AMBER);
      lineY += LINE_HEIGHT;
      for (const std::string& line : detail)
      {
        _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT) + 6, lineY, line, Ink::TEXT_DETAIL, Face::SansRegular);
        lineY += LINE_HEIGHT;
      }
      lineY += 6;
    }

    // ---- The actions -------------------------------------------------------------------------------
    if (layout.hasActions)
    {
      lineY += 4;
      float buttonX = TEXT_LEFT;
      const float buttonY = static_cast<float>(lineY) - 5.0F;

      for (const EventAction& action : card.actions)
      {
        // `committed` is "this is already in the orders this tick goes in with", which two kinds of
        // button can be and the rest cannot. It is drawn the same way for both: outlined in blue,
        // never the filled primary, and still a target, because every order is editable until the
        // lock.
        //
        // A build button has two states the other buttons do not, and it says which it is in
        // (ADR-053): QUEUED, so the next tap is known to take it back; or beyond the purse, drawn
        // dim with what is missing and not a target, because the lock would refuse it and a
        // refusal a tick later is the worst way to learn a price.
        std::string label = action.label;
        bool committed = false;
        bool unaffordable = false;
        if (action.kind == EventActionKind::QueueBuild)
        {
          committed = std::ranges::find(m_state.orders.queuedBuilds, action.target) != m_state.orders.queuedBuilds.end();
          unaffordable = !committed && !m_state.CanAffordBuild(action.target);
          if (committed)
          {
            label += " - QUEUED";
          }
          else if (unaffordable)
          {
            label += std::format(" - NEED {} MORE", BuildShortfall(action.target));
          }
        }

        // An answered offer says which way it was answered, in the past tense against the other
        // button's imperative, and the pair stays tappable so that changing an answer is one tap
        // (ADR-068).
        if (action.kind == EventActionKind::AcceptProposal || action.kind == EventActionKind::DeclineProposal)
        {
          const auto answered = std::ranges::find_if(m_state.orders.answers, [&action](const ProposalAnswer& _answer)
                                                     { return _answer.proposal == action.target; });
          const bool thisWay =
            answered != m_state.orders.answers.end() && answered->accepted == (action.kind == EventActionKind::AcceptProposal);
          if (thisWay)
          {
            label = answered->accepted ? "ACCEPTED" : "DECLINED";
            committed = true;
          }
        }

        const float width = static_cast<float>(FontRenderer::MeasurePixels(label)) + 12.0F;
        if (buttonX + width > Frame::DIGEST_WIDTH - RAIL_PADDING)
        {
          break;
        }

        // One filled button per card at most: the thing the digest thinks you should do.
        if (action.primary && !m_state.orders.locked && !committed && !unaffordable)
        {
          _shapes.FillRect(buttonX, buttonY, width, 18.0F, Ink::BLUE);
          _text.DrawText(static_cast<std::int32_t>(buttonX) + 6, lineY, label, Ink::APP_BACKGROUND);
        }
        else
        {
          const bool dim = m_state.orders.locked || unaffordable;
          _shapes.StrokeRect(buttonX, buttonY, width, 18.0F, committed && !dim ? Ink::BLUE : Ink::OUTLINE);
          _text.DrawText(static_cast<std::int32_t>(buttonX) + 6, lineY, label,
                         dim ? Ink::NEUTRAL_DIM : (committed ? Ink::BLUE : Ink::TEXT_PRIMARY));
        }

        if ((!m_state.orders.locked && !unaffordable) || action.kind == EventActionKind::Focus)
        {
          AddHit(buttonX, buttonY, width, 18.0F, ActionFor(action.kind), action.target);
        }
        buttonX += width + 6.0F;
      }
      lineY += LINE_HEIGHT + 4;
    }

    y = static_cast<float>(lineY) + 4.0F;

    // Only when there is something to focus. The card a tick-zero digest shows is synthetic -- it
    // reports that nothing has happened and carries the opening moves -- so it leads no event, and
    // a `FocusEvent` for event number -1 is an out-of-bounds read that took the whole client down.
    if (card.leadEvent != EventRefs::NONE)
    {
      m_hits.insert(m_hits.begin() + static_cast<std::ptrdiff_t>(cardHitsBegin),
                    HitRegion{0.0F, top, Frame::DIGEST_WIDTH - 1.0F, y - top, Action::FocusEvent, card.leadEvent});
    }
  }

  // ---- The page band -------------------------------------------------------------------------------
  //
  // At the foot of the column, where the stack it is about ends. `1 / 3 - MORE >` is the ops-console
  // form -- numbers first, ` - ` between facts -- and `< PREV` appears only once there is a page to
  // go back to, so the band never offers a direction that does nothing.
  if (paged)
  {
    const float bandY = Frame::SCREEN_HEIGHT - DIGEST_PAGE_HEIGHT;
    const std::int32_t bandText = CenterTextY(bandY, DIGEST_PAGE_HEIGHT);
    _shapes.FillRect(0.0F, bandY, Frame::DIGEST_WIDTH - 1.0F, 1.0F, Ink::DIVIDER);

    if (m_digestPage > 0)
    {
      _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), bandText, "< PREV", Ink::TEXT_MUTED);
      AddHit(0.0F, bandY, Frame::DIGEST_WIDTH * 0.5F, DIGEST_PAGE_HEIGHT, Action::ShowDigestPage,
             static_cast<std::int32_t>(m_digestPage) - 1);
    }

    const bool more = m_digestPage + 1 < pageStarts.size();
    const std::string count = std::format("{} / {}", m_digestPage + 1, pageStarts.size());
    DrawRight(_text, Frame::DIGEST_WIDTH - RAIL_PADDING, bandText, more ? count + " - MORE >" : count, Ink::TEXT_MUTED);
    if (more)
    {
      AddHit(Frame::DIGEST_WIDTH * 0.5F, bandY, Frame::DIGEST_WIDTH * 0.5F, DIGEST_PAGE_HEIGHT, Action::ShowDigestPage,
             static_cast<std::int32_t>(m_digestPage) + 1);
    }
  }
}

/// Which screen action a digest button performs. The two enums are separate on purpose: what an
/// event OFFERS is a fact about the match (`MatchState`), and what a tap DOES is a fact about this
/// screen, and the state has no business knowing the second.
MainPage::Action MainPage::ActionFor(EventActionKind _kind) noexcept
{
  switch (_kind)
  {
  case EventActionKind::RedirectFleet:
    return Action::OpenFleet;
  case EventActionKind::QueueBuild:
    return Action::ToggleBuild;
  case EventActionKind::AcceptProposal:
    return Action::AcceptProposal;
  case EventActionKind::DeclineProposal:
    return Action::DeclineProposal;
  case EventActionKind::Focus:
  default:
    // MAP carries the system it points at, not the event it sits on.
    return Action::FocusSystem;
  }
}

void MainPage::DrawLocksRail(ShapeRenderer& _shapes, FontRenderer& _text)
{
  // **Nothing here is a control, and that is the point of the redesign** (SCREENS.md 01). This rail
  // used to own the buttons; now every order is given on the event that caused it, and this is a
  // read-only answer to one question: what goes in when the clock hits zero. A player who reads
  // only this column still knows what they have committed.
  const float railX = Frame::SCREEN_WIDTH - Frame::ORDERS_WIDTH;
  const float contentX = railX + RAIL_PADDING;
  const float contentRight = Frame::SCREEN_WIDTH - RAIL_PADDING;
  const auto railWidth = static_cast<std::uint32_t>(contentRight - contentX);

  m_railRows.clear();

  _shapes.FillRect(railX, Frame::TOP_BAR_HEIGHT, Frame::ORDERS_WIDTH, Frame::SCREEN_HEIGHT - Frame::TOP_BAR_HEIGHT, Ink::APP_BACKGROUND);
  _shapes.FillRect(railX, Frame::TOP_BAR_HEIGHT, 1.0F, Frame::SCREEN_HEIGHT - Frame::TOP_BAR_HEIGHT, Ink::CARD_BORDER);

  const bool atLock = m_state.orders.locked && !m_state.match.finished;

  const std::int32_t headerY = static_cast<std::int32_t>(Frame::TOP_BAR_HEIGHT) + 12;
  _text.DrawText(static_cast<std::int32_t>(contentX), headerY,
                 m_state.match.finished ? std::string{"FINAL"} : std::format("LOCKS T{}", m_state.OrdersTick()), Ink::TEXT_MUTED);

  if (atLock)
  {
    // A filled chip rather than a word (screen 06). LOCKED in muted grey was the same weight as
    // UNLOCKED in amber and read as a label; filled, it reads as a state the rail is IN.
    const auto chipWidth = static_cast<float>(FontRenderer::MeasurePixels("LOCKED")) + 12.0F;
    _shapes.FillRect(contentRight - chipWidth, static_cast<float>(headerY) - 4.0F, chipWidth, 16.0F, Ink::LOCKED_FILL);
    _text.DrawText(static_cast<std::int32_t>(contentRight - chipWidth) + 6, headerY, "LOCKED", Ink::APP_BACKGROUND);
  }
  else
  {
    DrawRight(_text, contentRight, headerY, m_state.match.finished ? "MATCH ENDED" : "UNLOCKED",
              m_state.match.finished ? Ink::RED : Ink::AMBER);
  }

  float y = Frame::TOP_BAR_HEIGHT + 28.0F;

  // One line of help, and only one. It says what the column is and what its rows do, because a
  // player who used the old rail will look for the controls here first.
  //
  // A branch rather than a chained ternary, and that is about the formatter rather than the code:
  // clang-format 18 and 22 align the second `?` of a chain differently, so an expression written
  // that way is one the tree cannot be clean under both at once, and CI's is 18 (`build.yml`).
  std::string help{"What goes in when the clock hits zero. Tap a row to go to what it is about."};
  if (m_state.match.finished)
  {
    help = "The match is over. This is what you finished with.";
  }
  else if (atLock)
  {
    help = LockSentence();
  }
  for (const std::string& line : FontRenderer::WrapToWidth(help, railWidth))
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(y), line, atLock ? Ink::AMBER : Ink::TEXT_DETAIL,
                   atLock ? Face::SansMedium : Face::SansRegular);
    y += static_cast<float>(LINE_HEIGHT);
  }
  y += 6.0F;

  const auto section = [&](std::string_view _label, std::string_view _count)
  {
    _shapes.FillRect(railX + 1.0F, y, Frame::ORDERS_WIDTH - 1.0F, 1.0F, Ink::DIVIDER);
    _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(y) + 8, _label, Ink::TEXT_MUTED);
    DrawRight(_text, contentRight, static_cast<std::int32_t>(y) + 8, _count, Ink::TEXT_MUTED);
    y += 22.0F;
  };

  /// A row: what it is on the left, where it stands on the right. The status carries the colour --
  /// it is the half a player scans down the column for.
  ///
  /// **A row is a link to what it is about** (ADR-060). It gives no order -- the digest is still
  /// the order surface -- it takes the eye to the thing the row names, which is the question a
  /// player reading this column keeps having to answer somewhere else. `Action::None` is a row with
  /// nothing to point at, and it is not a target and draws no hover.
  const auto row = [&](std::string_view _label, std::string_view _status, const Color& _statusColor, Action _action, std::int32_t _index)
  {
    const std::int32_t lineY = static_cast<std::int32_t>(y);
    const auto room = static_cast<std::uint32_t>(contentRight - contentX - static_cast<float>(FontRenderer::MeasurePixels(_status)) - 8.0F);

    const std::vector<std::string> wrapped = FontRenderer::WrapToWidth(_label, room);
    const float height = static_cast<float>(std::max<std::size_t>(1, wrapped.size())) * static_cast<float>(LINE_HEIGHT) + 4.0F;
    const bool target = _action != Action::None;

    if (target)
    {
      const bool hovered = m_pointerXPixels >= railX && m_pointerYPixels >= y && m_pointerYPixels < y + height;
      if (hovered)
      {
        _shapes.FillRect(railX + 1.0F, y, Frame::ORDERS_WIDTH - 1.0F, height, Ink::HOVER_FILL);
      }
      AddHit(railX, y, Frame::ORDERS_WIDTH, height, _action, _index);
      m_railRows.push_back(RailRow{railX, y, Frame::ORDERS_WIDTH, height});
    }

    for (std::size_t index = 0; index < wrapped.size(); ++index)
    {
      _text.DrawText(static_cast<std::int32_t>(contentX), lineY + static_cast<std::int32_t>(index) * LINE_HEIGHT, wrapped[index],
                     Ink::TEXT_PRIMARY);
    }
    DrawRight(_text, contentRight, lineY, _status, _statusColor);
    y += height;
  };

  const auto nothing = [&](std::string_view _text2)
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(y), _text2, Ink::NEUTRAL_DIM);
    y += static_cast<float>(LINE_HEIGHT) + 4.0F;
  };

  // **Every row below is focus-only once the orders are locked or the match is over**, matching
  // every other control on the screen: a tap still moves the eye, and nothing opens a sheet that
  // could take an order for a tick that is already resolving (ADR-060, screen 06).
  const bool navigateOnly = m_state.orders.locked || m_state.match.finished;

  // ---- FLEETS ------------------------------------------------------------------------------------
  std::uint32_t yours = 0;
  for (const Fleet& fleet : m_state.fleets)
  {
    yours += fleet.owner == m_state.viewer ? 1U : 0U;
  }
  section("FLEETS", std::to_string(yours));

  if (yours == 0)
  {
    nothing("- none -");
  }
  for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
  {
    const Fleet& fleet = m_state.fleets[index];
    if (fleet.owner != m_state.viewer)
    {
      continue;
    }

    const bool moving = fleet.eta > 0 && fleet.to != fleet.from;
    const SystemNode* destination = fleet.to >= 0 && fleet.to < static_cast<std::int32_t>(m_state.graph.systems.size())
                                      ? &m_state.graph.systems[static_cast<std::size_t>(fleet.to)]
                                      : nullptr;
    const std::string where = destination == nullptr ? std::string{} : Uppercased(destination->name);

    // `FLT3 14 > KEPLER-REACH` moving, `FLT1 9 HOLD VESK` standing (SCREENS.md 01).
    const std::string label = moving ? std::format("{} {} > {}", Uppercased(fleet.name), fleet.ships, where)
                                     : std::format("{} {} HOLD {}", Uppercased(fleet.name), fleet.ships, where);

    // A fleet under way opens its destination picker, which is what tapping its marker on the map
    // does; one standing still focuses where it is standing. Both are the same question -- where is
    // this fleet, and where is it going -- asked from the column that lists them (ADR-060).
    const Action fleetAction = navigateOnly ? Action::FocusSystem : (moving ? Action::OpenFleet : Action::FocusSystem);
    const std::int32_t fleetTarget = fleetAction == Action::OpenFleet ? static_cast<std::int32_t>(index) : fleet.to;

    // The verdict tokens the design asks for -- LOSE, +DEF -- are the combat preview's, and the
    // preview is a sentence today rather than a verdict. Until the digest's verdict box is built
    // this says the fact the state actually carries: when it arrives, or that it is dug in.
    if (moving)
    {
      row(label, std::format("T{}", fleet.eta), Ink::TEXT_MUTED, fleetAction, fleetTarget);
    }
    else if (fleet.status.find("incumbent") != std::string::npos)
    {
      row(label, "+DEF", Ink::BLUE, fleetAction, fleetTarget);
    }
    else
    {
      row(label, "HOLD", Ink::TEXT_MUTED, fleetAction, fleetTarget);
    }
  }

  // ---- BUILDS ------------------------------------------------------------------------------------
  //
  // The purse on the header and the price on every queued row, so the column adds up in front of
  // the player (ADR-053): what is queued, what it takes, and what is left when the clock hits zero.
  section("BUILDS", std::format("{} AVAIL - {} CR", m_state.orders.availableBuilds, m_state.player.credits));

  if (m_state.orders.queuedBuilds.empty())
  {
    nothing("- nothing queued -");
  }
  for (const std::int32_t queued : m_state.orders.queuedBuilds)
  {
    if (queued >= 0 && queued < static_cast<std::int32_t>(m_state.orders.builds.size()))
    {
      const BuildRow& build = m_state.orders.builds[static_cast<std::size_t>(queued)];

      // To the sheet that queued it, which is where it is taken back (ADR-060). `BuildRow::system`
      // is an id and `OpenSystem` names a position, so the row has to look the system up.
      const std::int32_t at = PositionOfSystem(m_state, build.system);
      const Action buildAction = at == EventRefs::NONE ? Action::None : (navigateOnly ? Action::FocusSystem : Action::OpenSystem);
      row(Uppercased(build.title), std::format("QUEUED -{}", build.cost), Ink::BLUE, buildAction, at);
    }
  }

  // **What is already rising, with the tick it lands on** (ADR-069), in the form FLEETS above uses
  // for a fleet under way: the rail is the receipt of everything this player has committed to, and
  // a build that is paid for and in flight is exactly that. It is listed after the queue because
  // the queue is what THIS lock will take and this is what an earlier one already did.
  for (const BuildRow& build : m_state.orders.builds)
  {
    if (!build.rising)
    {
      continue;
    }
    const std::int32_t at = PositionOfSystem(m_state, build.system);
    const Action buildAction = at == EventRefs::NONE ? Action::None : (navigateOnly ? Action::FocusSystem : Action::OpenSystem);
    row(Uppercased(build.title), std::format("T{}", build.completesAt), Ink::TEXT_MUTED, buildAction, at);
  }
  if (!m_state.orders.queuedBuilds.empty())
  {
    const std::uint32_t spent = m_state.orders.QueuedBuildCost();
    nothing(std::format("- {} cr left at the lock -", spent <= m_state.player.credits ? m_state.player.credits - spent : 0));
  }
  for (const BuildRow& build : m_state.orders.builds)
  {
    if (build.isTradeLane)
    {
      row(Uppercased(build.title), "PROPOSE", Ink::AMBER, Action::None, EventRefs::NONE);
    }
  }

  // ---- SIGNALS -----------------------------------------------------------------------------------
  //
  // What is going OUT this tick. This rail said "- none sent -" for as long as it existed, because
  // the client had no model of an offer leaving; ADR-039 gave it one, and the count on the right is
  // the way in -- it is the only section header on this rail that is a control.
  const std::int32_t signalsY = static_cast<std::int32_t>(y);
  section("SIGNALS", m_state.orders.locked ? std::string{"LOCKED"} : std::format("{} TO SEND >", m_state.orders.availableSignals));
  if (!m_state.orders.locked)
  {
    AddHit(railX, static_cast<float>(signalsY), Frame::ORDERS_WIDTH, 22.0F, Action::OpenSignals, 0);
  }

  if (m_state.orders.queuedSignals.empty())
  {
    nothing("- none sent -");
  }
  for (const std::int32_t queued : m_state.orders.queuedSignals)
  {
    if (queued < 0 || queued >= static_cast<std::int32_t>(m_state.orders.signals.size()))
    {
      continue;
    }
    const SignalRow& signal = m_state.orders.signals[static_cast<std::size_t>(queued)];

    // A concede is red on the rail and nothing else is. It is the one row here that ends the
    // player's match rather than changing it.
    row(Uppercased(signal.title), signal.kind == SignalKind::Concede ? "CONCEDE" : "SENDING",
        signal.kind == SignalKind::Concede ? Ink::RED : Ink::BLUE, Action::None, EventRefs::NONE);
  }

  // ---- PROPOSALS ---------------------------------------------------------------------------------
  section("PROPOSALS", std::format("{} OPEN", m_state.proposals.size()));

  if (m_state.proposals.empty())
  {
    nothing("- none -");
  }
  for (const Proposal& proposal : m_state.proposals)
  {
    const char* what = proposal.type == ProposalType::OpenLane        ? "LANE"
                       : proposal.type == ProposalType::ShareScouting ? "SCOUTING"
                                                                      : "HOLD FIRE";

    // An offer about a lane focuses the lane's far end; an offer about a map or a truce is about
    // no system at all and so is read rather than tapped.
    const std::int32_t about = ProposalSystem(m_state, proposal);

    // The rail is the receipt of what goes in at the lock, so an answered offer says the answer
    // rather than the countdown it is no longer waiting out (ADR-068).
    const std::size_t index = static_cast<std::size_t>(&proposal - m_state.proposals.data());
    const auto answered = std::ranges::find_if(m_state.orders.answers, [index](const ProposalAnswer& _answer)
                                               { return _answer.proposal == static_cast<std::int32_t>(index); });
    const bool isAnswered = answered != m_state.orders.answers.end();
    const std::string status = isAnswered ? (answered->accepted ? "ACCEPTED" : "DECLINED") : std::format("{} TICKS", proposal.ticksLeft);

    row(std::format("{} {}", Uppercased(proposal.from), what), status, isAnswered ? Ink::BLUE : Ink::AMBER,
        about == EventRefs::NONE ? Action::None : Action::FocusSystem, about);
  }

  // ---- The footer --------------------------------------------------------------------------------
  //
  // Pinned to the bottom rather than following the sections, because it is the one line that is
  // true whatever else the rail says: all three columns go in together (one-pager, decision 3).
  const float footerY = Frame::SCREEN_HEIGHT - 30.0F;
  _shapes.FillRect(railX + 1.0F, footerY, Frame::ORDERS_WIDTH - 1.0F, 1.0F, Ink::DIVIDER);
  const std::int32_t footerText = static_cast<std::int32_t>(footerY) + 11;
  if (m_state.match.finished)
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), footerText, "NOTHING MORE LOCKS", Ink::TEXT_MUTED);
    DrawRight(_text, contentRight, footerText, std::format("T{} FINAL", m_state.match.tick), Ink::RED);
  }
  else if (atLock)
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), footerText, "LOCKED TOGETHER", Ink::TEXT_MUTED);
    DrawRight(_text, contentRight, footerText, std::format("T{} RESOLVING", m_state.OrdersTick()), Ink::NEUTRAL_DIM);
  }
  else
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), footerText, "ALL LOCK TOGETHER", Ink::TEXT_MUTED);
    DrawRight(_text, contentRight, footerText, FormatCountdown(m_state.match.secondsToLock), Ink::AMBER);
  }
}

/// A panel is a SHEET at the bottom of the map pane, and every row is a 44-pixel target.
///
/// **Redesigned touch-first on 2026-09-12** (ADR-052). It was a 300-pixel card floating in the
/// middle of the pane with 20-pixel rows carrying one string each. Three things were wrong with
/// that and only the first is about fingers: a 20-pixel row is half the smallest target anybody
/// hits reliably; a card in the middle of the pane covers the systems the choice is about and puts
/// the choice under the hand making it; and one string per row meant the destination picker could
/// say where a fleet could go and not what was there or whose it was.
///
/// Anchored to the bottom, the map above it stays readable, the thumb reaches it, and a row has
/// room for the three things a move is actually decided on -- where, how far, and whose.
void MainPage::DrawPanel(ShapeRenderer& _shapes, FontRenderer& _text)
{
  if (m_panel == Panel::None)
  {
    return;
  }

  /// What one row of a sheet says. The destination picker needs all four; a panel with nothing to
  /// put in a field leaves it empty, which is what draws a single-line row.
  struct SheetRow
  {
    std::string title;
    /// The second line, under the title. Empty draws a single-line row.
    std::string detail;
    /// Right-aligned, and the thing the eye scans a column of rows for.
    std::string right;
    /// The owner square. Fully transparent is no square, for a row that is not about a player.
    Color accent;
    /// What tapping this row acts on, or `EventRefs::NONE` for a row that is only read.
    std::int32_t target;
    /// A 22px section band rather than a 44px row: a label over what follows it, never a target
    /// (ADR-064). It counts against the six-row cap, because it takes the room a row would.
    bool band = false;
    /// Whether this row is said in the loss colour. The one row on any sheet that cannot be taken
    /// back once it resolves, and nothing else.
    bool alarm = false;
  };

  constexpr Color NO_ACCENT = {0, 0, 0, 0};

  std::vector<SheetRow> rows;

  /// Whether the LAST row composed must be drawn whatever else is dropped. Only the concede sets
  /// it: every other row on every sheet is equal, and the first six win.
  bool lastRowMustSurvive = false;
  std::string title;

  // What tapping a row does. It differs per panel, and it used to not exist: every row went to
  // `ChooseDestination`, so the BUILD panel listed two things a player could not tap.
  Action rowAction = Action::ChooseDestination;

  switch (m_panel)
  {
  case Panel::BuildList:
  {
    if (m_panelSubject < 0 || m_panelSubject >= static_cast<std::int32_t>(m_state.graph.systems.size()))
    {
      return;
    }
    const SystemNode& node = m_state.graph.systems[static_cast<std::size_t>(m_panelSubject)];
    title = std::format("BUILD - {}", Uppercased(node.name));
    rowAction = Action::ToggleBuild;

    // **This system's buildings and nobody else's** (ADR-058). `Orders::builds` is the whole
    // empire's list -- the rail counts it, and the digest offers from it -- so drawing it whole
    // under a title naming ONE system offered `Shipyard - Pell` on the sheet for Dothan.
    //
    // `BuildRow::system` is a system id and `m_panelSubject` is a position in the view's own list,
    // which are different numbers for the same system (ADR-057). The node carries both.
    for (std::size_t index = 0; index < m_state.orders.builds.size(); ++index)
    {
      const BuildRow& row = m_state.orders.builds[index];
      if (row.system != node.id)
      {
        continue;
      }

      // A system already building offers nothing and says why, with the tick it lands on. It is
      // not a target: the lock refuses a second order on it (ADR-069), and a row that looks live
      // and does nothing is the defect this screen has been bitten by twice.
      if (row.rising)
      {
        rows.push_back(SheetRow{row.title, "It cannot take another order until this lands", std::format("DONE T{}", row.completesAt),
                                Ink::BLUE, EventRefs::NONE});
        continue;
      }

      const bool queued =
        std::ranges::find(m_state.orders.queuedBuilds, static_cast<std::int32_t>(index)) != m_state.orders.queuedBuilds.end();

      // A queued row says so in the right-hand column rather than inside its own title: tapping it
      // again is how you take it back, and the status is what the eye is scanning the column for.
      // An unqueued row carries its price there instead, and one the purse cannot cover says what
      // is missing and is not a target (ADR-053).
      const bool affordable = queued || m_state.CanAffordBuild(static_cast<std::int32_t>(index));
      const std::string status = queued ? std::string{"QUEUED"}
                                 : affordable
                                   ? std::format("{} CR", row.cost)
                                   : std::format("{} CR - NEED {} MORE", row.cost, BuildShortfall(static_cast<std::int32_t>(index)));

      // The second line is what the level BUYS and what it COSTS IN TICKS -- the two numbers a
      // player weighs a shipyard level against a mining level with, and the reason both tables are
      // on the wire (ADR-053, ADR-069). The server wrote the sentence; this only places it.
      rows.push_back(SheetRow{row.title, row.detail, status, queued ? Ink::BLUE : NO_ACCENT,
                              m_state.orders.locked || !affordable ? EventRefs::NONE : static_cast<std::int32_t>(index)});
    }

    // A system with both buildings on it says so, rather than opening an empty sheet. The same
    // bargain the signal picker makes with an empire that has nobody to talk to.
    if (rows.empty())
    {
      rows.push_back(
        SheetRow{"NOTHING LEFT TO BUILD HERE", "Both buildings are at their top level.", std::string{}, NO_ACCENT, EventRefs::NONE});
    }
    break;
  }
  case Panel::Destination:
  {
    const Fleet& fleet = m_state.fleets[static_cast<std::size_t>(m_panelSubject)];
    title = std::format("MOVE {} - PICK LANE", Uppercased(fleet.name));
    const std::int32_t origin = fleet.order == FleetStance::Move ? fleet.to : fleet.from;

    // Lane-constrained: only the systems this fleet can actually reach along an edge, and the tick
    // it would arrive. A destination picker that offered anything else would be offering a move
    // the graph cannot express (one-pager, "Shape of a game").
    for (const Lane& lane : m_state.graph.lanes)
    {
      std::int32_t other = EventRefs::NONE;
      if (lane.a == origin)
      {
        other = lane.b;
      }
      else if (lane.b == origin)
      {
        other = lane.a;
      }
      if (other == EventRefs::NONE || HasFlag(m_state.graph.systems[static_cast<std::size_t>(other)].flags, SystemFlags::RegionAnchor))
      {
        continue;
      }

      const SystemNode& node = m_state.graph.systems[static_cast<std::size_t>(other)];

      // Whose it is, in the words the digest uses. This is the line the old picker had nowhere to
      // put, and it is the one that decides whether a lane is an expansion or a fight.
      std::string held;
      if (node.owner == NOBODY)
      {
        held = "UNCLAIMED";
      }
      else if (node.owner == m_state.viewer)
      {
        held = "YOURS";
      }
      else if (node.owner < static_cast<OwnerId>(m_state.players.size()))
      {
        held = m_state.players[static_cast<std::size_t>(node.owner)].label;
      }
      else
      {
        held = "RIVAL";
      }
      // **What is standing there, which is what the lane is a fight or an expansion by.** Every
      // hostile fleet parked on a system is an incumbent by the time a fleet ordered this tick
      // lands on it -- the rule `TickResolver::Preview` applies -- so anything here fights with the
      // defender's bonus and the row says so in the words the verdict box uses (ADR-063).
      std::uint32_t garrison = 0;
      for (const Fleet& standing : m_state.fleets)
      {
        const bool hostile = standing.owner != NOBODY && standing.owner != m_state.viewer;
        if (hostile && standing.from == standing.to && standing.to == other)
        {
          garrison += standing.ships;
        }
      }
      if (garrison > 0)
      {
        held += std::format(" - {} +DEF", garrison);
      }

      if (HasFlag(node.flags, SystemFlags::Capital))
      {
        held += " - CAPITAL";
      }
      if (HasFlag(node.flags, SystemFlags::Contested))
      {
        held += " - CONTESTED";
      }

      rows.push_back(SheetRow{Uppercased(node.name), held,
                              std::format("{} - ETA T{}", lane.cost == 1 ? std::string{"1 TICK"} : std::format("{} TICKS", lane.cost),
                                          m_state.OrdersTick() + lane.cost - 1),
                              OwnerColor(node.owner, m_state.viewer), m_state.orders.locked ? EventRefs::NONE : other});
    }
    break;
  }
  case Panel::SignalList:
  {
    title = "SIGNAL - PICK ONE";
    rowAction = Action::ToggleSignal;

    // **The concede is lifted out and put back at the end, under a band of its own** (ADR-064). It
    // is always the last row `ComposeSignals` writes, so this does not move it -- what it buys is
    // the 22 pixels between it and `Hold fire 3 ticks - P2`, which is the row a thumb aiming at one
    // of them would otherwise hit by being a target-height out.
    std::int32_t concede = EventRefs::NONE;
    for (std::size_t index = 0; index < m_state.orders.signals.size(); ++index)
    {
      if (m_state.orders.signals[index].kind == SignalKind::Concede)
      {
        concede = static_cast<std::int32_t>(index);
      }
    }

    for (std::size_t index = 0; index < m_state.orders.signals.size(); ++index)
    {
      if (static_cast<std::int32_t>(index) == concede)
      {
        continue;
      }

      const SignalRow& signal = m_state.orders.signals[index];
      const bool queued =
        std::ranges::find(m_state.orders.queuedSignals, static_cast<std::int32_t>(index)) != m_state.orders.queuedSignals.end();

      rows.push_back(SheetRow{signal.title, std::string{}, queued ? "SENDING" : std::string{}, queued ? Ink::BLUE : NO_ACCENT,
                              m_state.orders.locked ? EventRefs::NONE : static_cast<std::int32_t>(index)});
    }

    if (m_state.orders.signals.empty())
    {
      rows.push_back(SheetRow{"NOTHING TO SAY YET", "Offers need a border, or a neighbour you have actually met.", std::string{}, NO_ACCENT,
                              EventRefs::NONE});
    }
    else if (m_state.orders.availableSignals > static_cast<std::uint32_t>(m_state.orders.signals.size()))
    {
      rows.push_back(SheetRow{std::format("+{} MORE THAN THIS SHEET CAN SHOW",
                                          m_state.orders.availableSignals - static_cast<std::uint32_t>(m_state.orders.signals.size())),
                              std::string{}, std::string{}, NO_ACCENT, EventRefs::NONE});
    }

    if (concede != EventRefs::NONE)
    {
      const SignalRow& signal = m_state.orders.signals[static_cast<std::size_t>(concede)];
      const bool queued = std::ranges::find(m_state.orders.queuedSignals, concede) != m_state.orders.queuedSignals.end();
      const bool armed = m_armedConcede == concede;

      // The band is a label and a label must not cost a row anything. Added only when it and the
      // row under it both fit inside the cap; beyond that the red text carries the warning alone.
      if (rows.size() + 2 <= SHEET_MAXIMUM_ROWS)
      {
        rows.push_back(SheetRow{.title = "CONCEDE", .accent = NO_ACCENT, .target = EventRefs::NONE, .band = true});
      }

      // Red from the first tap, and the armed row says what the NEXT tap does rather than what this
      // row is -- the only warning a concede gets and the only one it needs.
      lastRowMustSurvive = true;
      rows.push_back(SheetRow{.title = signal.title,
                              .right = queued ? "SENDING" : (armed ? "TAP AGAIN TO CONFIRM" : std::string{}),
                              .accent = armed || queued ? Ink::RED : NO_ACCENT,
                              .target = m_state.orders.locked ? EventRefs::NONE : concede,
                              .alarm = armed || queued});
    }
    break;
  }
  case Panel::Replay:
  {
    // **A stub says so in its title.** The six phases are the tick resolution order from the
    // one-pager and stepping through them needs `PhaseRecord`s the snapshot does not carry, so this
    // sheet lists what a replay would walk and nothing more. It said that in a seventh row, which
    // the six-row cap then clipped into `+1 MORE THAN THIS SHEET CAN SHOW` -- a sheet reporting an
    // overflow it did not have, about a row explaining that there is nothing here.
    title = std::format("REPLAY TICK {} - NOT YET WIRED", m_panelSubject);
    for (const char* phase : {"1. LOCK", "2. PRODUCTION", "3. MOVEMENT", "4. COMBAT", "5. CLAIMS", "6. DIGEST"})
    {
      rows.push_back(SheetRow{phase, std::string{}, std::string{}, NO_ACCENT, EventRefs::NONE});
    }
    break;
  }
  case Panel::None:
  default:
    return;
  }

  // ---- The sheet -------------------------------------------------------------------------------
  //
  // More rows than fit are REPORTED rather than dropped. A picker that quietly forgets a lane is a
  // picker that cannot be trusted about the ones it did show.
  //
  // **THE CONCEDE KEEPS THE LAST VISIBLE SLOT** (ADR-064, extended 2026-09-13). It is composed last
  // and the sheet draws the first six, so a player with six offers on the table had no concede row
  // at all -- the one control on this screen that must always be reachable, gone precisely when the
  // board is busy enough to want it. The offers it displaces are counted in the `+N` line like any
  // other. Found by `ConcedingTakesTwoTapsOnTheSameRow` when ADR-069 changed how the bots expand
  // and seat zero's sixth offer arrived.
  const std::size_t clippedBefore = rows.size();
  if (lastRowMustSurvive && rows.size() > SHEET_MAXIMUM_ROWS)
  {
    SheetRow survivor = std::move(rows.back());
    rows.resize(SHEET_MAXIMUM_ROWS - 1);
    rows.push_back(std::move(survivor));
  }

  const std::size_t shown = std::min(rows.size(), SHEET_MAXIMUM_ROWS);
  const bool clipped = clippedBefore > shown;

  const float paneX = Frame::DIGEST_WIDTH;
  const float paneWidth = Frame::SCREEN_WIDTH - Frame::DIGEST_WIDTH - Frame::ORDERS_WIDTH;
  const float width = paneWidth - 2.0F * SHEET_MARGIN;
  const float x = paneX + SHEET_MARGIN;

  // Summed rather than multiplied, because a band is 22 and a row is 44 and both count as one of
  // the six (ADR-064).
  float listHeight = clipped ? SHEET_CLIPPED_HEIGHT : 0.0F;
  for (std::size_t index = 0; index < shown; ++index)
  {
    listHeight += rows[index].band ? SHEET_BAND_HEIGHT : SHEET_ROW_HEIGHT;
  }

  // **At the lock the sheet stays and goes inert** (ADR-065). Its rows are already not targets --
  // every panel above passes `EventRefs::NONE` while the orders are locked -- so what is left is to
  // say why, in the rail's own words and in the rail's amber.
  const bool atLock = m_state.orders.locked && !m_state.match.finished;
  const std::vector<std::string> lockHelp =
    atLock ? FontRenderer::WrapToWidth(LockSentence(), static_cast<std::uint32_t>(width - 2.0F * CARD_PADDING))
           : std::vector<std::string>{};
  const float helpHeight = lockHelp.empty() ? 0.0F : static_cast<float>(lockHelp.size()) * static_cast<float>(LINE_HEIGHT) + 12.0F;

  const float height = SHEET_HEADER_HEIGHT + helpHeight + listHeight + SHEET_ACTION_HEIGHT;
  const float y = Frame::SCREEN_HEIGHT - SHEET_MARGIN - height;

  _shapes.FillRect(x, y, width, height, Ink::APP_BACKGROUND);
  _shapes.StrokeRect(x, y, width, height, Ink::CARD_BORDER);

  // ---- Header ----------------------------------------------------------------------------------
  _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), CenterTextY(y, SHEET_HEADER_HEIGHT), title, Ink::TEXT_PRIMARY,
                 Face::MonoMedium);
  DrawRight(_text, x + width - CARD_PADDING, CenterTextY(y, SHEET_HEADER_HEIGHT), "X", Ink::TEXT_MUTED);

  // The same filled grey chip the locks rail wears, in the header's own status position -- clear of
  // the `X`'s 36-pixel corner, which is a target and must not have a chip drawn into it.
  if (atLock)
  {
    const auto chipWidth = static_cast<float>(FontRenderer::MeasurePixels("LOCKED")) + 12.0F;
    const float chipX = x + width - SHEET_HEADER_HEIGHT - chipWidth;
    _shapes.FillRect(chipX, y + 10.0F, chipWidth, 16.0F, Ink::LOCKED_FILL);
    _text.DrawText(static_cast<std::int32_t>(chipX) + 6, CenterTextY(y, SHEET_HEADER_HEIGHT), "LOCKED", Ink::APP_BACKGROUND);
  }

  // A close target the height of the header, not the width of one glyph.
  AddHit(x + width - SHEET_HEADER_HEIGHT, y, SHEET_HEADER_HEIGHT, SHEET_HEADER_HEIGHT, Action::ClosePanel, 0);
  _shapes.FillRect(x, y + SHEET_HEADER_HEIGHT, width, 1.0F, Ink::DIVIDER);

  // ---- Why nothing here does anything ----------------------------------------------------------
  if (!lockHelp.empty())
  {
    std::int32_t helpY = static_cast<std::int32_t>(y + SHEET_HEADER_HEIGHT) + 6;
    for (const std::string& line : lockHelp)
    {
      _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), helpY, line, Ink::AMBER, Face::SansMedium);
      helpY += LINE_HEIGHT;
    }
    _shapes.FillRect(x, y + SHEET_HEADER_HEIGHT + helpHeight, width, 1.0F, Ink::DIVIDER);
  }

  // ---- Rows ------------------------------------------------------------------------------------
  float rowY = y + SHEET_HEADER_HEIGHT + helpHeight;
  for (std::size_t index = 0; index < shown; ++index)
  {
    const SheetRow& row = rows[index];
    const bool tappable = row.target != EventRefs::NONE;

    // A band is a label over what follows it, drawn like the rails' section headers: a rule, then
    // the label, and nothing to tap.
    if (row.band)
    {
      _shapes.FillRect(x + CARD_PADDING, rowY, width - 2.0F * CARD_PADDING, 1.0F, Ink::DIVIDER);
      _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), CenterTextY(rowY, SHEET_BAND_HEIGHT), row.title, Ink::TEXT_MUTED);
      rowY += SHEET_BAND_HEIGHT;
      continue;
    }

    // No second rule directly under a band's: one line is a section header and two is a box.
    if (index > 0 && !rows[index - 1].band)
    {
      _shapes.FillRect(x + CARD_PADDING, rowY, width - 2.0F * CARD_PADDING, 1.0F, Ink::DIVIDER);
    }

    float textX = x + CARD_PADDING;
    if (row.accent.alpha != 0)
    {
      _shapes.FillRect(textX, rowY + 18.0F, 8.0F, 8.0F, row.accent);
      textX += 16.0F;
    }

    // One line centres in the row; two sit either side of its middle. THE ROW HEIGHT DOES NOT
    // CHANGE with the content -- a column of rows of one height is what a finger aims at.
    const std::int32_t titleY = row.detail.empty() ? CenterTextY(rowY, SHEET_ROW_HEIGHT) : static_cast<std::int32_t>(rowY) + 12;
    const Color titleColor = !tappable ? Ink::NEUTRAL_DIM : (row.alarm ? Ink::RED : Ink::TEXT_PRIMARY);
    _text.DrawText(static_cast<std::int32_t>(textX), titleY, row.title, titleColor);

    if (!row.detail.empty())
    {
      _text.DrawText(static_cast<std::int32_t>(textX), static_cast<std::int32_t>(rowY) + 26, row.detail, Ink::TEXT_MUTED,
                     Face::SansRegular);
    }
    if (!row.right.empty())
    {
      DrawRight(_text, x + width - CARD_PADDING, CenterTextY(rowY, SHEET_ROW_HEIGHT), row.right,
                !tappable ? Ink::NEUTRAL_DIM : (row.alarm ? Ink::RED : Ink::TEXT_DETAIL));
    }

    if (tappable)
    {
      AddHit(x, rowY, width, SHEET_ROW_HEIGHT, rowAction, row.target);
    }
    rowY += SHEET_ROW_HEIGHT;
  }

  if (clipped)
  {
    _shapes.FillRect(x + CARD_PADDING, rowY, width - 2.0F * CARD_PADDING, 1.0F, Ink::DIVIDER);
    _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), CenterTextY(rowY, SHEET_CLIPPED_HEIGHT),
                   std::format("+{} MORE THAN THIS SHEET CAN SHOW", clippedBefore - shown), Ink::NEUTRAL_DIM);
    rowY += SHEET_CLIPPED_HEIGHT;
  }

  // ---- Cancel ----------------------------------------------------------------------------------
  //
  // A bar as well as the header's X. The X is where a mouse expects it and the bar is where a thumb
  // already is, and closing a sheet opened by mistake is the commonest thing done to one.
  _shapes.FillRect(x, rowY, width, 1.0F, Ink::DIVIDER);
  DrawCentered(_text, x + width * 0.5F, CenterTextY(rowY, SHEET_ACTION_HEIGHT), "CANCEL", Ink::TEXT_MUTED);
  AddHit(x, rowY, width, SHEET_ACTION_HEIGHT, Action::ClosePanel, 0);
}

} // namespace Lockstep
