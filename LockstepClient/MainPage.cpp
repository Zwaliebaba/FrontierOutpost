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

#include <queue>

namespace Lockstep
{

namespace
{

using Neuron::Color;
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
  m_state = std::move(_state);
  m_panel = Panel::None;
  m_panelSubject = EventRefs::NONE;
  m_focusedSystem = EventRefs::NONE;

  MeasureContent();
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
    m_state.match.secondsToLock = 0.0;
    m_state.orders.locked = true;
    m_panel = Panel::None;
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
      return true;
    }

    case Action::OpenFleet:
      m_panel = Panel::Destination;
      m_panelSubject = region->index;
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
        // Refused here, by the rule the lock would refuse it by (ADR-053). The rows and buttons
        // that lead here already say so and are not targets, so this is the guard behind them
        // rather than the message.
        if (!m_state.CanAffordBuild(region->index))
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
        m_state.orders.answeredProposal = region->index;
        m_state.orders.acceptedProposal = region->action == Action::AcceptProposal;
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

    case Action::OpenReplay:
      m_panel = Panel::Replay;
      m_panelSubject = static_cast<std::int32_t>(m_state.match.tick);
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
  const std::int32_t bigY = CenterTextY(0.0F, Frame::TOP_BAR_HEIGHT, FontRenderer::COUNTDOWN_SCALE);

  // **Amber is the deadline colour, and at zero there is no deadline left to warn about** (screen
  // 06). A countdown that stayed amber on 00:00:00 read as "hurry" to a player who could no longer
  // do anything, which is the opposite of what the number means once it has run out.
  const bool atLock = m_state.orders.locked && !m_state.match.finished;
  DrawRight(_text, cursor, bigY, countdown, atLock ? Ink::NEUTRAL_DIM : Ink::AMBER, FontRenderer::COUNTDOWN_SCALE);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(countdown, FontRenderer::COUNTDOWN_SCALE)) + 8.0F;

  const std::string lockLabel = m_state.match.finished ? std::string{"MATCH ENDED"}
                                : atLock               ? std::format("T{} LOCKED", m_state.OrdersTick())
                                                       : std::format("T{} LOCKS", m_state.OrdersTick());
  DrawRight(_text, cursor, centered, lockLabel, Ink::TEXT_MUTED);

  // ---- The left half, trimmed to what is left --------------------------------------------------
  const float titleWidth = static_cast<float>(FontRenderer::MeasurePixels("LOCKSTEP"));
  const float lineX = 16.0F + titleWidth + 10.0F;
  const float room = cursor - 14.0F - lineX;

  _text.DrawText(16, centered, "LOCKSTEP", Ink::TEXT_PRIMARY);

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
  const std::size_t columns = FontRenderer::FitCharacters(static_cast<std::uint32_t>(textWidth));

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

  // ---- The cards -----------------------------------------------------------------------------------
  const std::vector<DigestCard> cards = CardsOf(m_state);
  for (const DigestCard& card : cards)
  {
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
    _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT), lineY, Uppercased(card.title), Ink::TEXT_PRIMARY);
    if (!card.stamp.empty())
    {
      DrawRight(_text, Frame::DIGEST_WIDTH - RAIL_PADDING, lineY, card.stamp, Ink::TEXT_MUTED);
    }
    lineY += LINE_HEIGHT + 2;

    for (const std::string& detail : card.lines)
    {
      for (const std::string& line : FontRenderer::Wrap(detail, columns))
      {
        _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT), lineY, line, Ink::TEXT_DETAIL);
        lineY += LINE_HEIGHT;
      }
    }

    // ---- The verdict box ---------------------------------------------------------------------------
    //
    // Always a verdict and never a bare `A v B` (DESIGN-GUIDELINES "Copy"), and the second line
    // always says whose ships remain -- which is why the snapshot carries both sides now.
    if (!card.verdict.empty())
    {
      lineY += 4;
      const std::vector<std::string> detail = FontRenderer::Wrap(card.verdictDetail, columns - 2);
      const float boxTop = static_cast<float>(lineY) - 5.0F;
      const float boxHeight = static_cast<float>(1 + detail.size()) * static_cast<float>(LINE_HEIGHT) + 10.0F;
      _shapes.StrokeRect(TEXT_LEFT, boxTop, Frame::DIGEST_WIDTH - TEXT_LEFT - RAIL_PADDING, boxHeight, Ink::AMBER);

      _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT) + 6, lineY, card.verdict, Ink::AMBER);
      lineY += LINE_HEIGHT;
      for (const std::string& line : detail)
      {
        _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT) + 6, lineY, line, Ink::TEXT_DETAIL);
        lineY += LINE_HEIGHT;
      }
      lineY += 6;
    }

    // ---- The actions -------------------------------------------------------------------------------
    if (!card.actions.empty())
    {
      lineY += 4;
      float buttonX = TEXT_LEFT;
      const float buttonY = static_cast<float>(lineY) - 5.0F;

      for (const EventAction& action : card.actions)
      {
        // A build button has two states the other buttons do not, and it says which it is in
        // (ADR-053): QUEUED, so the next tap is known to take it back; or beyond the purse, drawn
        // dim with what is missing and not a target, because the lock would refuse it and a
        // refusal a tick later is the worst way to learn a price.
        std::string label = action.label;
        bool queued = false;
        bool unaffordable = false;
        if (action.kind == EventActionKind::QueueBuild)
        {
          queued = std::ranges::find(m_state.orders.queuedBuilds, action.target) != m_state.orders.queuedBuilds.end();
          unaffordable = !queued && !m_state.CanAffordBuild(action.target);
          if (queued)
          {
            label += " - QUEUED";
          }
          else if (unaffordable)
          {
            label += std::format(" - NEED {} MORE", BuildShortfall(action.target));
          }
        }

        const float width = static_cast<float>(FontRenderer::MeasurePixels(label)) + 12.0F;
        if (buttonX + width > Frame::DIGEST_WIDTH - RAIL_PADDING)
        {
          break;
        }

        // One filled button per card at most: the thing the digest thinks you should do.
        if (action.primary && !m_state.orders.locked && !queued && !unaffordable)
        {
          _shapes.FillRect(buttonX, buttonY, width, 18.0F, Ink::BLUE);
          _text.DrawText(static_cast<std::int32_t>(buttonX) + 6, lineY, label, Ink::APP_BACKGROUND);
        }
        else
        {
          const bool dim = m_state.orders.locked || unaffordable;
          _shapes.StrokeRect(buttonX, buttonY, width, 18.0F, queued && !dim ? Ink::BLUE : Ink::OUTLINE);
          _text.DrawText(static_cast<std::int32_t>(buttonX) + 6, lineY, label,
                         dim ? Ink::NEUTRAL_DIM : (queued ? Ink::BLUE : Ink::TEXT_PRIMARY));
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

    if (y > Frame::SCREEN_HEIGHT)
    {
      break;
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
  const std::size_t columns = FontRenderer::FitCharacters(static_cast<std::uint32_t>(contentRight - contentX));

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

  // One line of help, and only one. It says where the controls went, because a player who used the
  // old rail will look for them here first.
  const std::string help = m_state.match.finished ? std::string{"The match is over. This is what you finished with."}
                           : atLock ? std::format("Resolving T{}. Controls return with the new digest. Anything you tap now is an "
                                                  "order for T{}.",
                                                  m_state.OrdersTick(), m_state.OrdersTick() + 1)
                                    : std::string{"What goes in when the clock hits zero. Change it from the digest."};
  for (const std::string& line : FontRenderer::Wrap(help, columns))
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(y), line, atLock ? Ink::AMBER : Ink::TEXT_DETAIL);
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
  const auto row = [&](std::string_view _label, std::string_view _status, const Color& _statusColor)
  {
    const std::int32_t lineY = static_cast<std::int32_t>(y);
    const std::size_t room = FontRenderer::FitCharacters(
      static_cast<std::uint32_t>(contentRight - contentX - static_cast<float>(FontRenderer::MeasurePixels(_status)) - 8.0F));

    const std::vector<std::string> wrapped = FontRenderer::Wrap(_label, room);
    for (std::size_t index = 0; index < wrapped.size(); ++index)
    {
      _text.DrawText(static_cast<std::int32_t>(contentX), lineY + static_cast<std::int32_t>(index) * LINE_HEIGHT, wrapped[index],
                     Ink::TEXT_PRIMARY);
    }
    DrawRight(_text, contentRight, lineY, _status, _statusColor);
    y += static_cast<float>(std::max<std::size_t>(1, wrapped.size())) * static_cast<float>(LINE_HEIGHT) + 4.0F;
  };

  const auto nothing = [&](std::string_view _text2)
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(y), _text2, Ink::NEUTRAL_DIM);
    y += static_cast<float>(LINE_HEIGHT) + 4.0F;
  };

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
  for (const Fleet& fleet : m_state.fleets)
  {
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

    // The verdict tokens the design asks for -- LOSE, +DEF -- are the combat preview's, and the
    // preview is a sentence today rather than a verdict. Until the digest's verdict box is built
    // this says the fact the state actually carries: when it arrives, or that it is dug in.
    if (moving)
    {
      row(label, std::format("T{}", fleet.eta), Ink::TEXT_MUTED);
    }
    else if (fleet.status.find("incumbent") != std::string::npos)
    {
      row(label, "+DEF", Ink::BLUE);
    }
    else
    {
      row(label, "HOLD", Ink::TEXT_MUTED);
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
      row(Uppercased(build.title), std::format("QUEUED -{}", build.cost), Ink::BLUE);
    }
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
      row(Uppercased(build.title), "PROPOSE", Ink::AMBER);
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
        signal.kind == SignalKind::Concede ? Ink::RED : Ink::BLUE);
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
    row(std::format("{} {}", Uppercased(proposal.from), what), std::format("{} TICKS", proposal.ticksLeft), Ink::AMBER);
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
  };

  constexpr Color NO_ACCENT = {0, 0, 0, 0};

  std::vector<SheetRow> rows;
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
      rows.push_back(SheetRow{row.title, std::string{}, status, queued ? Ink::BLUE : NO_ACCENT,
                              m_state.orders.locked || !affordable ? EventRefs::NONE : static_cast<std::int32_t>(index)});
    }

    // A system with both buildings on it says so, rather than opening an empty sheet. The same
    // bargain the signal picker makes with an empire that has nobody to talk to.
    if (rows.empty())
    {
      rows.push_back(SheetRow{"NOTHING LEFT TO BUILD HERE", "It already has a shipyard and a mining station.", std::string{}, NO_ACCENT,
                              EventRefs::NONE});
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
                              OwnerColor(node.owner, m_state.viewer), other});
    }
    break;
  }
  case Panel::SignalList:
  {
    title = "SIGNAL - PICK ONE";
    rowAction = Action::ToggleSignal;

    for (std::size_t index = 0; index < m_state.orders.signals.size(); ++index)
    {
      const SignalRow& signal = m_state.orders.signals[index];
      const bool queued =
        std::ranges::find(m_state.orders.queuedSignals, static_cast<std::int32_t>(index)) != m_state.orders.queuedSignals.end();
      const bool armed = m_armedConcede == static_cast<std::int32_t>(index);

      // Three states, in the right-hand column: queued, armed to be queued, or neither. The armed
      // one says what the NEXT tap does rather than what this row is, which is the only warning a
      // concede gets and the only one it needs.
      rows.push_back(SheetRow{signal.title, std::string{}, queued ? "SENDING" : (armed ? "TAP AGAIN TO CONFIRM" : std::string{}),
                              armed ? Ink::AMBER : (queued ? Ink::BLUE : NO_ACCENT),
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
    break;
  }
  case Panel::Replay:
  {
    title = std::format("REPLAY TICK {}", m_panelSubject);
    // A stub, and labelled as one. The six phases are the tick resolution order from the
    // one-pager; stepping through them needs the resolved state the server has not sent yet.
    for (const char* phase : {"1. LOCK", "2. PRODUCTION", "3. MOVEMENT", "4. COMBAT", "5. CLAIMS", "6. DIGEST"})
    {
      rows.push_back(SheetRow{phase, std::string{}, std::string{}, NO_ACCENT, EventRefs::NONE});
    }
    rows.push_back(SheetRow{"NOT YET WIRED TO A RESOLVED TICK", std::string{}, std::string{}, NO_ACCENT, EventRefs::NONE});
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
  const std::size_t shown = std::min(rows.size(), SHEET_MAXIMUM_ROWS);
  const bool clipped = rows.size() > shown;

  const float paneX = Frame::DIGEST_WIDTH;
  const float paneWidth = Frame::SCREEN_WIDTH - Frame::DIGEST_WIDTH - Frame::ORDERS_WIDTH;
  const float width = paneWidth - 2.0F * SHEET_MARGIN;
  const float x = paneX + SHEET_MARGIN;

  const float listHeight = static_cast<float>(shown) * SHEET_ROW_HEIGHT + (clipped ? SHEET_CLIPPED_HEIGHT : 0.0F);
  const float height = SHEET_HEADER_HEIGHT + listHeight + SHEET_ACTION_HEIGHT;
  const float y = Frame::SCREEN_HEIGHT - SHEET_MARGIN - height;

  _shapes.FillRect(x, y, width, height, Ink::APP_BACKGROUND);
  _shapes.StrokeRect(x, y, width, height, Ink::CARD_BORDER);

  // ---- Header ----------------------------------------------------------------------------------
  _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), CenterTextY(y, SHEET_HEADER_HEIGHT), title, Ink::TEXT_PRIMARY);
  DrawRight(_text, x + width - CARD_PADDING, CenterTextY(y, SHEET_HEADER_HEIGHT), "X", Ink::TEXT_MUTED);

  // A close target the height of the header, not the width of one glyph.
  AddHit(x + width - SHEET_HEADER_HEIGHT, y, SHEET_HEADER_HEIGHT, SHEET_HEADER_HEIGHT, Action::ClosePanel, 0);
  _shapes.FillRect(x, y + SHEET_HEADER_HEIGHT, width, 1.0F, Ink::DIVIDER);

  // ---- Rows ------------------------------------------------------------------------------------
  float rowY = y + SHEET_HEADER_HEIGHT;
  for (std::size_t index = 0; index < shown; ++index)
  {
    const SheetRow& row = rows[index];
    const bool tappable = row.target != EventRefs::NONE;

    if (index > 0)
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
    _text.DrawText(static_cast<std::int32_t>(textX), titleY, row.title, tappable ? Ink::TEXT_PRIMARY : Ink::NEUTRAL_DIM);

    if (!row.detail.empty())
    {
      _text.DrawText(static_cast<std::int32_t>(textX), static_cast<std::int32_t>(rowY) + 26, row.detail, Ink::TEXT_MUTED);
    }
    if (!row.right.empty())
    {
      DrawRight(_text, x + width - CARD_PADDING, CenterTextY(rowY, SHEET_ROW_HEIGHT), row.right,
                tappable ? Ink::TEXT_DETAIL : Ink::NEUTRAL_DIM);
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
                   std::format("+{} MORE THAN THIS SHEET CAN SHOW", rows.size() - shown), Ink::NEUTRAL_DIM);
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
