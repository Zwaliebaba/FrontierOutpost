// MainPage.cpp -- the ops console: digest rail, map, orders rail.
//
// Design/Screens/README.md is the spec and every number here comes from it. ADR-014 records the
// decisions the spec did not settle and the places the 8x8 font could not carry the reference's
// copy.

#include "pch.h"
#include "MainPage.h"

#include "DigestView.h"

#include "MapView.h"

#include <queue>

namespace Frontier
{

namespace
{

using Neuron::Color;
using Neuron::FontRenderer;
using Neuron::ShapeRenderer;

/// The design tokens, once (Design/Screens/README.md "Design tokens"). Alphas are the spec's
/// fractions turned into bytes: 0.04 -> 10, 0.07 -> 18, 0.10 -> 26, and so on.
constexpr Color APP_BACKGROUND = {11, 14, 20, 255};
constexpr Color MAP_TOP = {8, 10, 16, 255};
constexpr Color MAP_MIDDLE = {16, 22, 36, 255};
constexpr Color MAP_BOTTOM = {11, 14, 20, 255};
constexpr Color CARD_FILL = {255, 255, 255, 10};
constexpr Color CARD_BORDER = {255, 255, 255, 26};
constexpr Color DIVIDER = {255, 255, 255, 18};
constexpr Color OUTLINE = {255, 255, 255, 51};
constexpr Color HOVER_FILL = {255, 255, 255, 20};

constexpr Color TEXT_PRIMARY = {240, 243, 247, 255};
constexpr Color TEXT_MUTED = {214, 220, 228, 140};
constexpr Color TEXT_DETAIL = {214, 220, 228, 153};

constexpr Color BLUE = {94, 196, 255, 255};
constexpr Color AMBER = {255, 196, 87, 255};
constexpr Color RED = {255, 110, 96, 255};
constexpr Color PURPLE = {170, 140, 255, 255};
constexpr Color NEUTRAL_DIM = {214, 220, 228, 115};

constexpr Color GRID_LINE = {94, 196, 255, 18};
constexpr Color HORIZON_GLOW = {94, 196, 255, 26};
constexpr Color HORIZON_GLOW_RIM = {94, 196, 255, 0};
/// The BRIGHTEST star. Every other one is drawn at a share of this alpha, down to about a third,
/// so this is the top of a range rather than the whole sky's one tone -- which is why it is higher
/// than the flat 140 the old uniform field used: the faintest star here lands at 110 and the mean
/// near 150, so the field as a whole is about as present as it was and the bright ones stand out
/// of it (ADR-033).
constexpr Color STAR = {214, 220, 228, 220};
constexpr Color LANE_PLAIN = {214, 220, 228, 71};

constexpr float SCREEN_WIDTH = 1280.0F;
constexpr float SCREEN_HEIGHT = 720.0F;

constexpr float TWO_PI = 6.28318530717958647692F;

/// The ground grid, in design units. It runs well past the graph so the plane still has a floor
/// under it when the camera swings round to a corner, and it is square so that turning it reveals
/// no edge the front view did not have.
constexpr float GRID_MIN_DESIGN = -240.0F;
constexpr float GRID_MAX_DESIGN = 1040.0F;
constexpr float GRID_STEP = 80.0F;
constexpr std::uint32_t GRID_LINES_ACROSS = static_cast<std::uint32_t>((GRID_MAX_DESIGN - GRID_MIN_DESIGN) / GRID_STEP);

/// Node geometry, in WORLD units now rather than as multiples of a depth scale: the camera turns
/// a world size into a screen size, which is what makes a system genuinely larger when it is
/// nearer (ADR-017). The numbers are the reference's, read as world units.
constexpr float NODE_RADIUS = 4.5F * 1.15F;
constexpr float CAPITAL_RADIUS = 6.0F * 1.15F;
constexpr float STEM_HEIGHT = 20.0F;
constexpr float CAPITAL_STEM_HEIGHT = 30.0F;
constexpr float SITE_PIN_HEIGHT = 14.0F;
constexpr float SHADOW_WIDE = 2.2F;
constexpr float SHADOW_TALL = 0.9F;
constexpr float HALO_SCALE = 2.4F;
constexpr float RING_SCALE = 2.2F;
constexpr float FLEET_HOVER = 14.0F;
constexpr float REGION_RADIUS = 62.0F;
/// How high the sealed region's second ring floats. The reference lifted it by the ellipse's own
/// half-height; in world units that is about a quarter of the radius.
constexpr float REGION_VOLUME_HEIGHT = 26.0F;

/// A name in the rail's voice. Labels and headers are uppercase (DESIGN-GUIDELINES "Font"), and
/// the font has no case of its own to fall back on.
[[nodiscard]] std::string Uppercased(std::string_view _text)
{
  std::string out{_text};
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char _c) { return static_cast<char>(std::toupper(_c)); });
  return out;
}

[[nodiscard]] Color WithAlpha(const Color& _color, std::uint8_t _alpha) noexcept
{
  return Color{_color.red, _color.green, _color.blue, _alpha};
}

[[nodiscard]] Color EventColor(EventKind _kind) noexcept
{
  switch (_kind)
  {
  case EventKind::Contact:
    return AMBER;
  case EventKind::Proposal:
    return BLUE;
  case EventKind::Loss:
    return RED;
  case EventKind::Region:
    return PURPLE;
  case EventKind::Custodian:
  case EventKind::Economy:
  case EventKind::Ignored:
  default:
    return {214, 220, 228, 89};
  }
}

/// A whole-pixel y for a line of text vertically centred in a band.
[[nodiscard]] std::int32_t CenterTextY(float _bandTop, float _bandHeight, std::uint32_t _scale = FontRenderer::DEFAULT_SCALE) noexcept
{
  const float glyph = static_cast<float>(FontRenderer::GlyphHeightPixels(_scale));
  return static_cast<std::int32_t>(std::floor(_bandTop + (_bandHeight - glyph) * 0.5F));
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
  const auto total = static_cast<std::int64_t>(std::max(0.0, _seconds));
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
  const float mapLeft = DIGEST_WIDTH;
  const float mapRight = SCREEN_WIDTH - ORDERS_WIDTH;
  const bool startedOnMap = _drag.originXPixels >= mapLeft && _drag.originXPixels < mapRight && _drag.originYPixels >= TOP_BAR_HEIGHT;
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
    case Action::OpenSystem:
      m_focusedSystem = region->index;
      m_panel = Panel::BuildList;
      m_panelSubject = region->index;
      return true;

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
        queued.push_back(region->index);
      }
      else
      {
        queued.erase(found);
      }
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

void MainPage::DrawCentered(FontRenderer& _text, float _centerXPixels, std::int32_t _yPixels, std::string_view _string, const Color& _color,
                            std::uint32_t _scale)
{
  const auto width = static_cast<float>(FontRenderer::MeasurePixels(_string, _scale));
  _text.DrawText(static_cast<std::int32_t>(std::lround(_centerXPixels - width * 0.5F)), _yPixels, _string, _color, _scale);
}

void MainPage::DrawRight(FontRenderer& _text, float _rightXPixels, std::int32_t _yPixels, std::string_view _string, const Color& _color,
                         std::uint32_t _scale)
{
  const auto width = static_cast<float>(FontRenderer::MeasurePixels(_string, _scale));
  _text.DrawText(static_cast<std::int32_t>(std::lround(_rightXPixels - width)), _yPixels, _string, _color, _scale);
}

void MainPage::DrawWorld(ShapeRenderer& _shapes, FontRenderer& _text)
{
  m_hits.clear();

  _shapes.FillRect(0.0F, 0.0F, SCREEN_WIDTH, SCREEN_HEIGHT, APP_BACKGROUND);

  // THE MAP GOES FIRST, and the rails are painted over it. With the authored curve the map could
  // not leave its pane; a camera can put a projected label or a lane anywhere on the screen, so the
  // rails' own opaque backgrounds are what confine it (ADR-017).
  DrawMap(_shapes, _text);
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
  const std::int32_t centered = CenterTextY(0.0F, TOP_BAR_HEIGHT);
  _shapes.FillRect(0.0F, 0.0F, SCREEN_WIDTH, TOP_BAR_HEIGHT, APP_BACKGROUND);
  _shapes.FillRect(0.0F, TOP_BAR_HEIGHT - 1.0F, SCREEN_WIDTH, 1.0F, CARD_BORDER);

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
  float cursor = SCREEN_WIDTH - 16.0F;

  const std::string replayLabel = std::format("REPLAY T{}", m_state.match.tick);
  const float replayWidth = 10.0F + 7.0F + 6.0F + static_cast<float>(FontRenderer::MeasurePixels(replayLabel)) + 10.0F;
  const float replayX = cursor - replayWidth;
  _shapes.StrokeRect(replayX, 13.0F, replayWidth, 22.0F, OUTLINE);
  // The one glyph the font does not have and does not need: the replay triangle is geometry, as
  // it is in the reference (README "Assets").
  _shapes.FillTriangle(replayX + 10.0F, 19.0F, replayX + 17.0F, 24.0F, replayX + 10.0F, 29.0F, TEXT_PRIMARY);
  _text.DrawText(static_cast<std::int32_t>(replayX + 23.0F), centered, replayLabel, TEXT_PRIMARY);
  AddHit(replayX, 13.0F, replayWidth, 22.0F, Action::OpenReplay, 0);
  cursor = replayX - 14.0F;

  _shapes.FillRect(cursor, 13.0F, 1.0F, 22.0F, CARD_BORDER);
  cursor -= 15.0F;

  const std::string leaderLine = std::format("LDR {} {}", m_state.player.leader.name, FormatScore(m_state.player.leader.score));
  DrawRight(_text, cursor, centered, leaderLine, TEXT_MUTED);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(leaderLine)) + 8.0F;

  const std::string placement = std::format("{} / {}", FormatPlacement(m_state.player.placement), m_state.player.playerCount);
  const float chipWidth = static_cast<float>(FontRenderer::MeasurePixels(placement)) + 14.0F;
  _shapes.StrokeRect(cursor - chipWidth, 14.0F, chipWidth, 20.0F, OUTLINE);
  _text.DrawText(static_cast<std::int32_t>(cursor - chipWidth + 7.0F), centered, placement, TEXT_PRIMARY);
  cursor -= chipWidth + 8.0F;

  const std::string score = FormatScore(m_state.player.score);
  DrawRight(_text, cursor, centered, score, TEXT_PRIMARY);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(score)) + 8.0F;

  DrawRight(_text, cursor, centered, "SCORE", TEXT_MUTED);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels("SCORE")) + 14.0F;

  _shapes.FillRect(cursor, 13.0F, 1.0F, 22.0F, CARD_BORDER);
  cursor -= 15.0F;

  // The countdown is the one thing on the screen drawn at 2x, and it is amber because amber is
  // the warning colour: this is the deadline every order on the rail is racing (README "Frame").
  const std::string countdown = m_state.match.finished ? std::string{"--:--:--"} : FormatCountdown(m_state.match.secondsToLock);
  const std::int32_t bigY = CenterTextY(0.0F, TOP_BAR_HEIGHT, FontRenderer::COUNTDOWN_SCALE);
  DrawRight(_text, cursor, bigY, countdown, AMBER, FontRenderer::COUNTDOWN_SCALE);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(countdown, FontRenderer::COUNTDOWN_SCALE)) + 8.0F;

  DrawRight(_text, cursor, centered, m_state.match.finished ? std::string{"MATCH ENDED"} : std::format("T{} LOCKS", m_state.OrdersTick()),
            TEXT_MUTED);

  // ---- The left half, trimmed to what is left --------------------------------------------------
  const float titleWidth = static_cast<float>(FontRenderer::MeasurePixels("FRONTIER OUTPOST"));
  const float lineX = 16.0F + titleWidth + 10.0F;
  const float room = cursor - 14.0F - lineX;

  _text.DrawText(16, centered, "FRONTIER OUTPOST", TEXT_PRIMARY);

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
      _text.DrawText(static_cast<std::int32_t>(lineX), centered, candidate, TEXT_MUTED);

      // A disconnected client says so, in the one place a player is already looking. Everything
      // else on this screen is the last thing the server said, and without this there is no way to
      // tell that from the current thing the server is saying.
      if (!m_state.connected)
      {
        const float offlineX = lineX + static_cast<float>(FontRenderer::MeasurePixels(candidate)) + 12.0F;
        if (offlineX + static_cast<float>(FontRenderer::MeasurePixels("RECONNECTING")) < cursor - 14.0F)
        {
          _text.DrawText(static_cast<std::int32_t>(offlineX), centered, "RECONNECTING", RED);
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
  _shapes.FillRect(0.0F, TOP_BAR_HEIGHT, DIGEST_WIDTH, SCREEN_HEIGHT - TOP_BAR_HEIGHT, APP_BACKGROUND);
  _shapes.FillRect(DIGEST_WIDTH - 1.0F, TOP_BAR_HEIGHT, 1.0F, SCREEN_HEIGHT - TOP_BAR_HEIGHT, CARD_BORDER);

  const std::int32_t headerY = static_cast<std::int32_t>(TOP_BAR_HEIGHT) + 12;
  const bool returning = m_state.unreadTicks >= 2;

  if (returning)
  {
    // `SINCE YOU LOOKED - T43 > T46` and a chip. It is the first line a returning player reads and
    // it says how much of the match happened without them.
    _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), headerY,
                   std::format("SINCE YOU LOOKED - T{} > T{}", m_state.lastSeenTick, m_state.match.tick), TEXT_MUTED);

    const std::string chip = std::format("{} TICKS", m_state.unreadTicks);
    const float chipWidth = static_cast<float>(FontRenderer::MeasurePixels(chip)) + 14.0F;
    _shapes.StrokeRect(DIGEST_WIDTH - RAIL_PADDING - chipWidth, static_cast<float>(headerY) - 5.0F, chipWidth, 18.0F, AMBER);
    _text.DrawText(static_cast<std::int32_t>(DIGEST_WIDTH - RAIL_PADDING - chipWidth + 7.0F), headerY, chip, AMBER);
  }
  else
  {
    _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), headerY, std::format("DIGEST - TICK {}", m_state.match.tick), TEXT_MUTED);
    DrawRight(_text, DIGEST_WIDTH - RAIL_PADDING, headerY, std::format("{} EVENTS", m_state.digest.size()), TEXT_MUTED);
  }

  constexpr float TEXT_LEFT = RAIL_PADDING + 8.0F + 10.0F;
  const float textWidth = DIGEST_WIDTH - TEXT_LEFT - RAIL_PADDING;
  const std::size_t columns = FontRenderer::FitCharacters(static_cast<std::uint32_t>(textWidth));

  float y = TOP_BAR_HEIGHT + 28.0F;

  // ---- The delta -----------------------------------------------------------------------------------
  const DigestDelta delta = DeltaOf(m_state);
  if (delta.Any())
  {
    // Two cells to a row, rounded up. The division is integer ON PURPOSE -- three cells is two rows
    // -- and it is done before the conversion rather than inside it, because a `/` under a
    // `static_cast<float>` reads like a float division somebody got wrong.
    const std::size_t rows = (delta.cells.size() + 1) / 2;
    const float boxHeight = static_cast<float>(rows) * static_cast<float>(LINE_HEIGHT) + 12.0F;
    _shapes.StrokeRect(RAIL_PADDING, y, DIGEST_WIDTH - 2.0F * RAIL_PADDING, boxHeight, AMBER);

    // Two columns, because four short facts in one line wrap badly at 8px and four stacked lines
    // are a list rather than a summary.
    for (std::size_t index = 0; index < delta.cells.size(); ++index)
    {
      const float cellX = RAIL_PADDING + 8.0F + static_cast<float>(index % 2) * (DIGEST_WIDTH - 2.0F * RAIL_PADDING) * 0.5F;
      const std::int32_t cellY = static_cast<std::int32_t>(y) + 6 + static_cast<std::int32_t>(index / 2) * LINE_HEIGHT;
      _text.DrawText(static_cast<std::int32_t>(cellX), cellY, delta.cells[index], delta.cells[index].front() == '-' ? RED : AMBER);
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

    _shapes.FillRect(0.0F, y, DIGEST_WIDTH - 1.0F, 1.0F, DIVIDER);
    std::int32_t lineY = static_cast<std::int32_t>(y) + 11;

    _shapes.FillEllipse(RAIL_PADDING + 4.0F, static_cast<float>(lineY) + 4.0F, 4.0F, 4.0F, accent);
    _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT), lineY, Uppercased(card.title), TEXT_PRIMARY);
    if (!card.stamp.empty())
    {
      DrawRight(_text, DIGEST_WIDTH - RAIL_PADDING, lineY, card.stamp, TEXT_MUTED);
    }
    lineY += LINE_HEIGHT + 2;

    for (const std::string& detail : card.lines)
    {
      for (const std::string& line : FontRenderer::Wrap(detail, columns))
      {
        _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT), lineY, line, TEXT_DETAIL);
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
      _shapes.StrokeRect(TEXT_LEFT, boxTop, DIGEST_WIDTH - TEXT_LEFT - RAIL_PADDING, boxHeight, AMBER);

      _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT) + 6, lineY, card.verdict, AMBER);
      lineY += LINE_HEIGHT;
      for (const std::string& line : detail)
      {
        _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT) + 6, lineY, line, TEXT_DETAIL);
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
        const float width = static_cast<float>(FontRenderer::MeasurePixels(action.label)) + 12.0F;
        if (buttonX + width > DIGEST_WIDTH - RAIL_PADDING)
        {
          break;
        }

        // One filled button per card at most: the thing the digest thinks you should do.
        if (action.primary && !m_state.orders.locked)
        {
          _shapes.FillRect(buttonX, buttonY, width, 18.0F, BLUE);
          _text.DrawText(static_cast<std::int32_t>(buttonX) + 6, lineY, action.label, APP_BACKGROUND);
        }
        else
        {
          _shapes.StrokeRect(buttonX, buttonY, width, 18.0F, OUTLINE);
          _text.DrawText(static_cast<std::int32_t>(buttonX) + 6, lineY, action.label, m_state.orders.locked ? NEUTRAL_DIM : TEXT_PRIMARY);
        }

        if (!m_state.orders.locked || action.kind == EventActionKind::Focus)
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
                    HitRegion{0.0F, top, DIGEST_WIDTH - 1.0F, y - top, Action::FocusEvent, card.leadEvent});
    }

    if (y > SCREEN_HEIGHT)
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
    return Action::FocusEvent;
  }
}

void MainPage::DrawMap(ShapeRenderer& _shapes, FontRenderer& _text)
{
  const float paneX = DIGEST_WIDTH;
  const float paneWidth = SCREEN_WIDTH - DIGEST_WIDTH - ORDERS_WIDTH;
  const float paneHeight = SCREEN_HEIGHT - TOP_BAR_HEIGHT;
  m_mapView.SetViewport(paneX, TOP_BAR_HEIGHT, paneWidth, paneHeight);
  m_mapView.FrameContent(m_contentCenter, m_contentRadius, CAPITAL_STEM_HEIGHT + CAPITAL_RADIUS);
  const Neuron::OrbitCamera& camera = m_mapView.Camera();

  _shapes.FillVerticalGradient(paneX, TOP_BAR_HEIGHT, paneWidth, paneHeight, MAP_TOP, MAP_MIDDLE, 0.45F, MAP_BOTTOM);
  _text.SetClipRect(paneX, TOP_BAR_HEIGHT, paneWidth, paneHeight);

  // The sky goes through the same camera as everything else, because it is in the same world --
  // infinitely far away in it, which is a direction rather than a place (ADR-032). It is drawn
  // first and depth-tests against nothing, so the galaxy covers it.
  m_sky.Draw(_shapes, camera, STAR);

  const auto project = [&camera](const Neuron::OrbitCamera::WorldPoint& _world) { return camera.Project(_world); };
  const auto groundOf = [&](std::int32_t _system)
  {
    const SystemNode& node = m_state.graph.systems[static_cast<std::size_t>(_system)];
    return project(MapView::Ground(node.positionX, node.positionY));
  };

  // A segment between two projected points, drawn only when both ends are in front of the eye.
  // Clipping the one-end case properly would be the right answer for a camera that can be put
  // inside the galaxy; this one orbits outside it, so a lane with one end behind the eye is a
  // lane at the very edge of a steep view, and dropping it is not visible.
  const auto segment = [&_shapes](const Neuron::OrbitCamera::ScreenPoint& _a, const Neuron::OrbitCamera::ScreenPoint& _b,
                                  const Color& _color, float _thickness)
  {
    if (_a.visible && _b.visible)
    {
      _shapes.Line(_a.xPixels, _a.yPixels, _b.xPixels, _b.yPixels, _color, _thickness);
    }
  };

  // The ground grid, now genuinely on the ground: lines of constant x and constant z, projected.
  // It turns with the camera because it is part of the world, and that single change is most of
  // what makes the rotation read as a viewpoint moving rather than a picture being spun (ADR-017).
  for (std::uint32_t step = 0; step <= GRID_LINES_ACROSS; ++step)
  {
    const float at = GRID_MIN_DESIGN + static_cast<float>(step) * GRID_STEP;
    segment(project(MapView::Ground(at, GRID_MIN_DESIGN)), project(MapView::Ground(at, GRID_MAX_DESIGN)), GRID_LINE, 1.0F);
    segment(project(MapView::Ground(GRID_MIN_DESIGN, at)), project(MapView::Ground(GRID_MAX_DESIGN, at)), GRID_LINE, 1.0F);
  }

  // Lanes lie on the plane, under everything that stands on it.
  for (const Lane& lane : m_state.graph.lanes)
  {
    const Neuron::OrbitCamera::ScreenPoint a = groundOf(lane.a);
    const Neuron::OrbitCamera::ScreenPoint b = groundOf(lane.b);
    if (!a.visible || !b.visible)
    {
      continue;
    }

    switch (lane.kind)
    {
    case LaneKind::Trade:
      // A trade lane is public and meant to be read at a glance: it is the one consensual
      // mechanic in the game, and cancelling one is a tell (one-pager, decision 3).
      _shapes.Line(a.xPixels, a.yPixels, b.xPixels, b.yPixels, BLUE, 2.5F);
      break;
    case LaneKind::Proposed:
      _shapes.DashedLine(a.xPixels, a.yPixels, b.xPixels, b.yPixels, BLUE, 2.0F, 4.0F, 5.0F);
      break;
    case LaneKind::None:
    default:
      _shapes.Line(a.xPixels, a.yPixels, b.xPixels, b.yPixels, LANE_PLAIN, 1.2F);
      break;
    }

    DrawCentered(_text, (a.xPixels + b.xPixels) * 0.5F, static_cast<std::int32_t>(std::lround((a.yPixels + b.yPixels) * 0.5F)) - 4,
                 std::to_string(lane.cost), TEXT_DETAIL);
  }

  // The sealed region: everyone can see it and count down to it, which is what makes it a race
  // rather than a reward (one-pager, "Pacing devices").
  //
  // It is a CIRCLE on the ground, emitted as a projected polygon rather than as a screen-space
  // ellipse. An ellipse was right when the viewing angle could not change; now the shape a ground
  // circle makes depends on where the camera is, and projecting it is how it comes out right at
  // every angle for free.
  if (m_state.region.anchor != EventRefs::NONE)
  {
    const SystemNode& anchor = m_state.graph.systems[static_cast<std::size_t>(m_state.region.anchor)];
    DrawGroundCircle(_shapes, anchor.positionX, anchor.positionY, REGION_RADIUS, WithAlpha(PURPLE, 20), PURPLE, true);
    // A second ring, lifted. The reference drew one to suggest a volume rather than a puddle, and
    // with a real camera it does the job properly: it is a circle at altitude, so the gap between
    // the two rings opens and closes as the camera tilts.
    DrawGroundCircle(_shapes, anchor.positionX, anchor.positionY, REGION_RADIUS, {0, 0, 0, 0}, WithAlpha(PURPLE, 89), true,
                     REGION_VOLUME_HEIGHT);

    for (const auto& [offsetX, offsetY] : m_state.region.siteOffsets)
    {
      const Neuron::OrbitCamera::ScreenPoint foot = project(MapView::Ground(anchor.positionX + offsetX, anchor.positionY + offsetY));
      const Neuron::OrbitCamera::ScreenPoint head =
        project(MapView::Above(anchor.positionX + offsetX, anchor.positionY + offsetY, SITE_PIN_HEIGHT));
      segment(foot, head, WithAlpha(PURPLE, 153), 1.0F);
      if (head.visible)
      {
        _shapes.FillEllipse(head.xPixels, head.yPixels, 2.5F, 2.5F, PURPLE);
      }
    }

    // Below the nearest point of the rim, not beside the centre: at a low camera angle the two
    // are almost the same place and the label lands inside the region.
    const Neuron::OrbitCamera::ScreenPoint label = project(MapView::Ground(anchor.positionX, anchor.positionY + REGION_RADIUS));
    if (label.visible)
    {
      DrawCentered(_text, label.xPixels, static_cast<std::int32_t>(std::lround(label.yPixels)) + 14,
                   std::format("SEALED - OPENS T{}", m_state.region.opensAt), PURPLE);
    }
  }

  // ---- SYSTEMS AND FLEETS, BACK TO FRONT ------------------------------------------------------
  //
  // THE SORT IS THE CAMERA'S DOING. With a fixed viewpoint the authored order was correct for
  // every frame and nothing had to decide it. An orbiting camera changes what is in front of what
  // as it moves, so the order has to be computed -- far first, because this renderer has no depth
  // buffer for interface geometry and painter's order is the whole of its occlusion model
  // (ADR-014, ADR-017).
  struct Drawable
  {
    float depth;
    std::int32_t index;
    bool isFleet;
  };

  std::vector<Drawable> drawables;
  drawables.reserve(m_state.graph.systems.size() + m_state.fleets.size());

  for (std::size_t index = 0; index < m_state.graph.systems.size(); ++index)
  {
    const SystemNode& node = m_state.graph.systems[index];
    if (HasFlag(node.flags, SystemFlags::RegionAnchor))
    {
      continue;
    }
    const Neuron::OrbitCamera::ScreenPoint ground = groundOf(static_cast<std::int32_t>(index));
    if (ground.visible)
    {
      drawables.push_back(Drawable{ground.depth, static_cast<std::int32_t>(index), false});
    }
  }

  for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
  {
    const Fleet& fleet = m_state.fleets[index];
    if (fleet.order != FleetStance::Move || fleet.from == fleet.to)
    {
      continue;
    }
    const SystemNode& from = m_state.graph.systems[static_cast<std::size_t>(fleet.from)];
    const SystemNode& to = m_state.graph.systems[static_cast<std::size_t>(fleet.to)];
    const float designX = from.positionX + (to.positionX - from.positionX) * fleet.progress;
    const float designY = from.positionY + (to.positionY - from.positionY) * fleet.progress;
    const Neuron::OrbitCamera::ScreenPoint at = project(MapView::Ground(designX, designY));
    if (at.visible)
    {
      drawables.push_back(Drawable{at.depth, static_cast<std::int32_t>(index), true});
    }
  }

  std::sort(drawables.begin(), drawables.end(), [](const Drawable& _a, const Drawable& _b) { return _a.depth > _b.depth; });

  for (const Drawable& drawable : drawables)
  {
    if (drawable.isFleet)
    {
      DrawFleet(_shapes, _text, drawable.index);
    }
    else
    {
      DrawSystem(_shapes, _text, drawable.index);
    }
  }

  // `MAP - FOCUS: HALVORSEN` in the top-left corner (DESIGN-GUIDELINES "Map"). The map is no
  // longer captioned with a census -- that is on the top bar now -- and says instead what it is
  // currently pointed at, because the digest can point it somewhere.
  std::string focusLine = "MAP";
  if (m_focusedSystem != EventRefs::NONE && m_focusedSystem < static_cast<std::int32_t>(m_state.graph.systems.size()))
  {
    const SystemNode& focused = m_state.graph.systems[static_cast<std::size_t>(m_focusedSystem)];
    focusLine += focused.name.empty() ? " - FOCUS: THE FALLOW" : " - FOCUS: " + Uppercased(focused.name);
  }
  _text.DrawText(static_cast<std::int32_t>(paneX) + 12, static_cast<std::int32_t>(TOP_BAR_HEIGHT) + 12, focusLine, TEXT_DETAIL);

  // The legend earns its place: the owner colours are also the semantic colours, so a player who
  // learns this row can read every other coloured thing on the screen.
  struct LegendEntry
  {
    std::string label;
    Color color;
    bool isLane;
    bool dashed;
  };

  // Three fixed empires until 2026-09-11, and now the ones this player can actually see. A
  // twelve-swatch legend would fill the bar with colours for empires nobody has met, and the
  // entries that earn their place are the ones already on the map (ADR-027).
  const auto labelOf = [this](OwnerId _player)
  {
    return _player >= 0 && _player < static_cast<OwnerId>(m_state.players.size()) ? m_state.players[static_cast<std::size_t>(_player)].label
                                                                                  : std::string("RIVAL");
  };

  std::vector<LegendEntry> legend;
  legend.push_back({labelOf(m_state.viewer), OwnerColor(m_state.viewer, m_state.viewer), false, false});

  std::vector<OwnerId> rivals;
  for (const SystemNode& node : m_state.graph.systems)
  {
    if (node.owner != NOBODY && node.owner != m_state.viewer && std::find(rivals.begin(), rivals.end(), node.owner) == rivals.end())
    {
      rivals.push_back(node.owner);
    }
  }
  std::sort(rivals.begin(), rivals.end());

  // Four rivals is what fits beside the two lane entries at 8px. Past that the map is its own
  // legend: every system carries a name, and tapping one says who holds it.
  constexpr std::size_t MOST_RIVALS_SHOWN = 4;
  for (std::size_t index = 0; index < rivals.size() && index < MOST_RIVALS_SHOWN; ++index)
  {
    legend.push_back({labelOf(rivals[index]), OwnerColor(rivals[index], m_state.viewer), false, false});
  }

  legend.push_back({"PROPOSED LANE", BLUE, true, true});
  legend.push_back({"TRADE LANE", BLUE, true, false});

  float legendX = paneX + 12.0F;
  const float legendY = SCREEN_HEIGHT - 20.0F;
  for (const LegendEntry& entry : legend)
  {
    if (entry.isLane)
    {
      if (entry.dashed)
      {
        _shapes.DashedLine(legendX, legendY + 4.0F, legendX + 14.0F, legendY + 4.0F, entry.color, 2.0F, 4.0F, 3.0F);
      }
      else
      {
        _shapes.Line(legendX, legendY + 4.0F, legendX + 14.0F, legendY + 4.0F, entry.color, 2.0F);
      }
      legendX += 19.0F;
    }
    else
    {
      _shapes.FillEllipse(legendX + 4.0F, legendY + 4.0F, 4.0F, 4.0F, entry.color);
      legendX += 13.0F;
    }

    _text.DrawText(static_cast<std::int32_t>(legendX), static_cast<std::int32_t>(legendY), entry.label, TEXT_DETAIL);
    legendX += static_cast<float>(FontRenderer::MeasurePixels(entry.label)) + 14.0F;
  }

  _text.ClearClipRect();
}

void MainPage::DrawGroundCircle(ShapeRenderer& _shapes, float _designX, float _designY, float _radius, const Color& _fill,
                                const Color& _outline, bool _dashed, float _height)
{
  // A circle drawn ON the plane and projected, so the camera decides what shape it makes: a thin
  // sliver from a low angle, round from overhead, and nothing here has to know which.
  constexpr std::uint32_t SEGMENTS = 40;
  std::array<Neuron::OrbitCamera::ScreenPoint, SEGMENTS> rim = {};

  for (std::uint32_t segment = 0; segment < SEGMENTS; ++segment)
  {
    const float angle = TWO_PI * static_cast<float>(segment) / static_cast<float>(SEGMENTS);
    rim[segment] =
      m_mapView.Camera().Project(MapView::Above(_designX + _radius * std::cos(angle), _designY + _radius * std::sin(angle), _height));
    if (!rim[segment].visible)
    {
      return;
    }
  }

  if (_fill.alpha > 0)
  {
    // A fan from the centre. A circle projected from outside its own plane stays convex, so a fan
    // is enough and there is nothing to triangulate.
    const Neuron::OrbitCamera::ScreenPoint center = m_mapView.Camera().Project(MapView::Above(_designX, _designY, _height));
    if (center.visible)
    {
      for (std::uint32_t segment = 0; segment < SEGMENTS; ++segment)
      {
        const Neuron::OrbitCamera::ScreenPoint& a = rim[segment];
        const Neuron::OrbitCamera::ScreenPoint& b = rim[(segment + 1) % SEGMENTS];
        _shapes.FillTriangle(center.xPixels, center.yPixels, a.xPixels, a.yPixels, b.xPixels, b.yPixels, _fill);
      }
    }
  }

  if (_outline.alpha > 0)
  {
    for (std::uint32_t segment = 0; segment < SEGMENTS; ++segment)
    {
      if (_dashed && (segment % 2) == 1)
      {
        continue;
      }
      const Neuron::OrbitCamera::ScreenPoint& a = rim[segment];
      const Neuron::OrbitCamera::ScreenPoint& b = rim[(segment + 1) % SEGMENTS];
      _shapes.Line(a.xPixels, a.yPixels, b.xPixels, b.yPixels, _outline, 1.0F);
    }
  }
}

void MainPage::DrawSystem(ShapeRenderer& _shapes, FontRenderer& _text, std::int32_t _index)
{
  const Neuron::OrbitCamera& camera = m_mapView.Camera();
  const SystemNode& node = m_state.graph.systems[static_cast<std::size_t>(_index)];

  const bool capital = HasFlag(node.flags, SystemFlags::Capital);
  const float worldRadius = capital ? CAPITAL_RADIUS : NODE_RADIUS;
  const float stemHeight = capital ? CAPITAL_STEM_HEIGHT : STEM_HEIGHT;

  const Neuron::OrbitCamera::ScreenPoint ground = camera.Project(MapView::Ground(node.positionX, node.positionY));
  const Neuron::OrbitCamera::ScreenPoint top = camera.Project(MapView::Above(node.positionX, node.positionY, stemHeight));
  if (!ground.visible || !top.visible)
  {
    return;
  }

  const Color owner = OwnerColor(node.owner, m_state.viewer);
  // Sized at the depth the NODE is at, not the ground point below it: a stem leans away from the
  // camera, so the two stop being the same distance once the view is steep.
  const float radius = worldRadius * camera.PixelsPerWorldUnitAt(top.depth);

  // The shadow is a ground circle, so it deforms with the camera like everything else on the
  // plane -- round from overhead, a sliver from low down.
  DrawGroundCircle(_shapes, node.positionX, node.positionY, worldRadius * SHADOW_WIDE, WithAlpha(owner, 56), {0, 0, 0, 0}, false);

  _shapes.Line(ground.xPixels, ground.yPixels, top.xPixels, top.yPixels, WithAlpha(owner, 140));

  if (capital)
  {
    _shapes.FillEllipse(top.xPixels, top.yPixels, radius * HALO_SCALE, radius * HALO_SCALE, WithAlpha(owner, 46));
  }
  if (HasFlag(node.flags, SystemFlags::Contested))
  {
    _shapes.StrokeEllipse(top.xPixels, top.yPixels, radius * RING_SCALE, radius * RING_SCALE, owner);
  }
  if (node.custodianSince != 0)
  {
    _shapes.DashedEllipse(top.xPixels, top.yPixels, radius * 2.0F, radius * 2.0F, owner, 1.0F, 2.0F, 3.0F);
  }
  if (_index == m_focusedSystem)
  {
    _shapes.StrokeEllipse(top.xPixels, top.yPixels, radius * 3.0F, radius * 3.0F, TEXT_PRIMARY);
  }

  _shapes.FillEllipse(top.xPixels, top.yPixels, radius, radius, owner);

  // Labels are 8px at every distance. The reference draws every map label at one size and the
  // game has one font at one size (ADR-014), so a far system's name is exactly as legible as a
  // near one's -- which on a map you read rather than admire is the right trade.
  std::string label = node.name;
  if (capital)
  {
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char _c) { return static_cast<char>(std::toupper(_c)); });
  }
  DrawCentered(_text, top.xPixels, static_cast<std::int32_t>(std::lround(top.yPixels - radius)) - 13, label, TEXT_PRIMARY);

  if (node.custodianSince != 0)
  {
    DrawCentered(_text, ground.xPixels, static_cast<std::int32_t>(std::lround(ground.yPixels)) + 8,
                 std::format("CUSTODIAN T{}", node.custodianSince), TEXT_MUTED);
  }
  if (node.capturedAt != 0)
  {
    DrawCentered(_text, ground.xPixels, static_cast<std::int32_t>(std::lround(ground.yPixels)) + 8,
                 std::format("CAPTURED T{}", node.capturedAt), RED);
  }

  AddHit(top.xPixels - radius * 3.0F, top.yPixels - radius * 3.0F, radius * 6.0F, (ground.yPixels - top.yPixels) + radius * 6.0F,
         Action::OpenSystem, _index);
}

void MainPage::DrawFleet(ShapeRenderer& _shapes, FontRenderer& _text, std::int32_t _index)
{
  const Neuron::OrbitCamera& camera = m_mapView.Camera();
  const Fleet& fleet = m_state.fleets[static_cast<std::size_t>(_index)];

  const SystemNode& from = m_state.graph.systems[static_cast<std::size_t>(fleet.from)];
  const SystemNode& to = m_state.graph.systems[static_cast<std::size_t>(fleet.to)];
  const float designX = from.positionX + (to.positionX - from.positionX) * fleet.progress;
  const float designY = from.positionY + (to.positionY - from.positionY) * fleet.progress;

  const Neuron::OrbitCamera::ScreenPoint foot = camera.Project(MapView::Ground(designX, designY));
  const Neuron::OrbitCamera::ScreenPoint head = camera.Project(MapView::Above(designX, designY, FLEET_HOVER));
  if (!foot.visible || !head.visible)
  {
    return;
  }

  const Color owner = OwnerColor(fleet.owner, m_state.viewer);
  _shapes.Line(foot.xPixels, foot.yPixels, head.xPixels, head.yPixels, WithAlpha(owner, 153));

  // The arrowhead points along the lane IN WORLD SPACE and is then projected, so it turns with the
  // camera and keeps meaning "that way" rather than "that way on the screen when the map happened
  // to be seen from the front".
  const Neuron::OrbitCamera::ScreenPoint ahead = camera.Project(
    MapView::Above(designX + (to.positionX - from.positionX) * 0.02F, designY + (to.positionY - from.positionY) * 0.02F, FLEET_HOVER));
  float dirX = 1.0F;
  float dirY = 0.0F;
  if (ahead.visible)
  {
    const float runX = ahead.xPixels - head.xPixels;
    const float runY = ahead.yPixels - head.yPixels;
    const float run = std::sqrt(runX * runX + runY * runY);
    if (run > 0.001F)
    {
      dirX = runX / run;
      dirY = runY / run;
    }
  }

  constexpr float ARROW = 6.0F;
  _shapes.FillTriangle(head.xPixels + dirX * ARROW, head.yPixels + dirY * ARROW, head.xPixels - dirX * ARROW - dirY * ARROW * 0.8F,
                       head.yPixels - dirY * ARROW + dirX * ARROW * 0.8F, head.xPixels - dirX * ARROW + dirY * ARROW * 0.8F,
                       head.yPixels - dirY * ARROW - dirX * ARROW * 0.8F, owner);

  // Two fleets converging on one system put their labels in the same place -- which is what is
  // happening at Kepler-Reach in the reference, and is the normal case rather than an edge one.
  // Yours goes above the arrowhead, where you are already looking; a rival's goes beside and below
  // it, so the two can never overlap.
  const std::string label = std::format("{} - ETA T{}", fleet.name, fleet.eta);
  const auto labelWidth = static_cast<float>(FontRenderer::MeasurePixels(label));
  const auto labelY = static_cast<std::int32_t>(std::lround(head.yPixels - 18.0F));
  const float paneX = DIGEST_WIDTH;
  const float paneWidth = SCREEN_WIDTH - DIGEST_WIDTH - ORDERS_WIDTH;

  if (fleet.owner == m_state.viewer)
  {
    const float clamped = std::clamp(head.xPixels, paneX + labelWidth * 0.5F + 4.0F, paneX + paneWidth - labelWidth * 0.5F - 4.0F);
    DrawCentered(_text, clamped, labelY, label, owner);
    AddHit(head.xPixels - 14.0F, head.yPixels - 22.0F, 28.0F, 36.0F, Action::OpenFleet, _index);
  }
  else
  {
    const float placed = std::min(head.xPixels + 10.0F, paneX + paneWidth - labelWidth - 4.0F);
    _text.DrawText(static_cast<std::int32_t>(std::lround(placed)), labelY + LINE_HEIGHT, label, owner);
  }
}

void MainPage::DrawLocksRail(ShapeRenderer& _shapes, FontRenderer& _text)
{
  // **Nothing here is a control, and that is the point of the redesign** (SCREENS.md 01). This rail
  // used to own the buttons; now every order is given on the event that caused it, and this is a
  // read-only answer to one question: what goes in when the clock hits zero. A player who reads
  // only this column still knows what they have committed.
  const float railX = SCREEN_WIDTH - ORDERS_WIDTH;
  const float contentX = railX + RAIL_PADDING;
  const float contentRight = SCREEN_WIDTH - RAIL_PADDING;
  const std::size_t columns = FontRenderer::FitCharacters(static_cast<std::uint32_t>(contentRight - contentX));

  _shapes.FillRect(railX, TOP_BAR_HEIGHT, ORDERS_WIDTH, SCREEN_HEIGHT - TOP_BAR_HEIGHT, APP_BACKGROUND);
  _shapes.FillRect(railX, TOP_BAR_HEIGHT, 1.0F, SCREEN_HEIGHT - TOP_BAR_HEIGHT, CARD_BORDER);

  const std::int32_t headerY = static_cast<std::int32_t>(TOP_BAR_HEIGHT) + 12;
  _text.DrawText(static_cast<std::int32_t>(contentX), headerY,
                 m_state.match.finished ? std::string{"FINAL"} : std::format("LOCKS T{}", m_state.OrdersTick()), TEXT_MUTED);
  DrawRight(_text, contentRight, headerY, m_state.match.finished ? "MATCH ENDED" : (m_state.orders.locked ? "LOCKED" : "UNLOCKED"),
            m_state.match.finished ? RED : (m_state.orders.locked ? TEXT_MUTED : AMBER));

  float y = TOP_BAR_HEIGHT + 28.0F;

  // One line of help, and only one. It says where the controls went, because a player who used the
  // old rail will look for them here first.
  const std::string_view help = m_state.match.finished ? "The match is over. This is what you finished with."
                                                       : "What goes in when the clock hits zero. Change it from the digest.";
  for (const std::string& line : FontRenderer::Wrap(help, columns))
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(y), line, TEXT_DETAIL);
    y += static_cast<float>(LINE_HEIGHT);
  }
  y += 6.0F;

  const auto section = [&](std::string_view _label, std::string_view _count)
  {
    _shapes.FillRect(railX + 1.0F, y, ORDERS_WIDTH - 1.0F, 1.0F, DIVIDER);
    _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(y) + 8, _label, TEXT_MUTED);
    DrawRight(_text, contentRight, static_cast<std::int32_t>(y) + 8, _count, TEXT_MUTED);
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
                     TEXT_PRIMARY);
    }
    DrawRight(_text, contentRight, lineY, _status, _statusColor);
    y += static_cast<float>(std::max<std::size_t>(1, wrapped.size())) * static_cast<float>(LINE_HEIGHT) + 4.0F;
  };

  const auto nothing = [&](std::string_view _text2)
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(y), _text2, NEUTRAL_DIM);
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
      row(label, std::format("T{}", fleet.eta), TEXT_MUTED);
    }
    else if (fleet.status.find("incumbent") != std::string::npos)
    {
      row(label, "+DEF", BLUE);
    }
    else
    {
      row(label, "HOLD", TEXT_MUTED);
    }
  }

  // ---- BUILDS ------------------------------------------------------------------------------------
  section("BUILDS", std::format("{} AVAIL", m_state.orders.availableBuilds));

  if (m_state.orders.queuedBuilds.empty())
  {
    nothing("- nothing queued -");
  }
  for (const std::int32_t queued : m_state.orders.queuedBuilds)
  {
    if (queued >= 0 && queued < static_cast<std::int32_t>(m_state.orders.builds.size()))
    {
      row(Uppercased(m_state.orders.builds[static_cast<std::size_t>(queued)].title), "QUEUED", BLUE);
    }
  }
  for (const BuildRow& build : m_state.orders.builds)
  {
    if (build.isTradeLane)
    {
      row(Uppercased(build.title), "PROPOSE", AMBER);
    }
  }

  // ---- SIGNALS -----------------------------------------------------------------------------------
  //
  // Empty, and honestly so: a signal this player SENT is not in `MatchState` at all. The client
  // models offers arriving (`proposals`) and not offers going out, so there is nothing here to
  // report yet. The section is drawn rather than hidden because its absence is the finding.
  section("SIGNALS", "");
  nothing("- none sent -");

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
    row(std::format("{} {}", Uppercased(proposal.from), what), std::format("{} TICKS", proposal.ticksLeft), AMBER);
  }

  // ---- The footer --------------------------------------------------------------------------------
  //
  // Pinned to the bottom rather than following the sections, because it is the one line that is
  // true whatever else the rail says: all three columns go in together (one-pager, decision 3).
  const float footerY = SCREEN_HEIGHT - 30.0F;
  _shapes.FillRect(railX + 1.0F, footerY, ORDERS_WIDTH - 1.0F, 1.0F, DIVIDER);
  const std::int32_t footerText = static_cast<std::int32_t>(footerY) + 11;
  if (m_state.match.finished)
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), footerText, "NOTHING MORE LOCKS", TEXT_MUTED);
    DrawRight(_text, contentRight, footerText, std::format("T{} FINAL", m_state.match.tick), RED);
  }
  else
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), footerText, "ALL LOCK TOGETHER", TEXT_MUTED);
    DrawRight(_text, contentRight, footerText, FormatCountdown(m_state.match.secondsToLock), AMBER);
  }
}

void MainPage::DrawPanel(ShapeRenderer& _shapes, FontRenderer& _text)
{
  if (m_panel == Panel::None)
  {
    return;
  }

  // Panels sit over the map and nowhere else: the digest and the orders stay readable, because
  // the point of opening one is usually to decide something about what they say.
  const float paneX = DIGEST_WIDTH;
  const float paneWidth = SCREEN_WIDTH - DIGEST_WIDTH - ORDERS_WIDTH;
  const float width = 300.0F;
  const float x = paneX + (paneWidth - width) * 0.5F;
  const float y = TOP_BAR_HEIGHT + 90.0F;

  std::vector<std::string> rows;
  std::vector<std::int32_t> rowTargets;
  std::string title;

  // What tapping a row does. It differs per panel, and it used to not exist: every row went to
  // `ChooseDestination`, so the BUILD panel listed two things a player could not tap. Opening a
  // panel that offers nothing is worse than having no panel.
  Action rowAction = Action::ChooseDestination;

  switch (m_panel)
  {
  case Panel::BuildList:
  {
    const SystemNode& node = m_state.graph.systems[static_cast<std::size_t>(m_panelSubject)];
    title = std::format("BUILD - {}", node.name);
    rowAction = Action::ToggleBuild;

    for (std::size_t index = 0; index < m_state.orders.builds.size(); ++index)
    {
      const BuildRow& row = m_state.orders.builds[index];
      const bool queued =
        std::ranges::find(m_state.orders.queuedBuilds, static_cast<std::int32_t>(index)) != m_state.orders.queuedBuilds.end();

      // A queued row says so, because tapping it again is how you take it back and nothing else
      // on this panel would tell you that you had already chosen it.
      rows.push_back(queued ? std::format("{}  - QUEUED", row.title) : row.title);
      rowTargets.push_back(m_state.orders.locked ? EventRefs::NONE : static_cast<std::int32_t>(index));
    }
    break;
  }
  case Panel::Destination:
  {
    const Fleet& fleet = m_state.fleets[static_cast<std::size_t>(m_panelSubject)];
    title = std::format("MOVE {} - PICK LANE", fleet.name);
    const std::int32_t origin = fleet.order == FleetStance::Move ? fleet.to : fleet.from;
    // Lane-constrained: only the systems this fleet can actually reach along an edge, and the
    // tick it would arrive. A destination picker that offered anything else would be offering a
    // move the graph cannot express (one-pager, "Shape of a game").
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
      rows.push_back(
        std::format("{} - ETA T{}", m_state.graph.systems[static_cast<std::size_t>(other)].name, m_state.OrdersTick() + lane.cost - 1));
      rowTargets.push_back(other);
    }
    break;
  }
  case Panel::Replay:
  {
    title = std::format("REPLAY TICK {}", m_panelSubject);
    // A stub, and labelled as one. The six phases are the tick resolution order from the
    // one-pager; stepping through them needs the resolved state the server has not sent yet.
    rows = {"1. LOCK", "2. PRODUCTION", "3. MOVEMENT", "4. COMBAT", "5. CLAIMS", "6. DIGEST", "", "(not yet wired to a resolved tick)"};
    rowTargets.assign(rows.size(), EventRefs::NONE);
    break;
  }
  case Panel::None:
  default:
    return;
  }

  const float height = 34.0F + static_cast<float>(rows.size()) * 20.0F + 12.0F;

  _shapes.FillRect(x, y, width, height, APP_BACKGROUND);
  _shapes.StrokeRect(x, y, width, height, CARD_BORDER);
  _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), static_cast<std::int32_t>(y) + 12, title, TEXT_PRIMARY);
  DrawRight(_text, x + width - CARD_PADDING, static_cast<std::int32_t>(y) + 12, "X", TEXT_MUTED);
  AddHit(x + width - 24.0F, y, 24.0F, 30.0F, Action::ClosePanel, 0);

  float rowY = y + 30.0F;
  for (std::size_t index = 0; index < rows.size(); ++index)
  {
    _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), static_cast<std::int32_t>(rowY) + 6, rows[index], TEXT_DETAIL);
    if (rowTargets[index] != EventRefs::NONE)
    {
      AddHit(x, rowY, width, 20.0F, rowAction, rowTargets[index]);
    }
    rowY += 20.0F;
  }
}

} // namespace Frontier
