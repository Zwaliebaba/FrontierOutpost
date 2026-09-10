// MainPage.cpp -- the ops console: digest rail, map, orders rail.
//
// Design/Screens/README.md is the spec and every number here comes from it. ADR-014 records the
// decisions the spec did not settle and the places the 8x8 font could not carry the reference's
// copy.

#include "pch.h"
#include "MainPage.h"

#include "MapProjection.h"

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
constexpr Color STAR = {214, 220, 228, 140};
constexpr Color LANE_PLAIN = {214, 220, 228, 71};

constexpr float SCREEN_WIDTH = 1280.0F;
constexpr float SCREEN_HEIGHT = 720.0F;

/// The map's own coordinates, before the letterbox. Everything on the plane is authored here.
constexpr float GRID_MIN_X = -200.0F;
constexpr float GRID_MAX_X = 1000.0F;
constexpr float GRID_STEP_X = 100.0F;
constexpr float GRID_STEP_Y = 70.0F;

/// Node geometry, as multiples of the depth scale (README "Systems").
constexpr float NODE_RADIUS = 4.5F * 1.15F;
constexpr float CAPITAL_RADIUS = 6.0F * 1.15F;
constexpr float STEM_HEIGHT = 20.0F;
constexpr float CAPITAL_STEM_HEIGHT = 30.0F;
constexpr float SHADOW_WIDE = 2.2F;
constexpr float SHADOW_TALL = 0.9F;
constexpr float HALO_SCALE = 2.4F;
constexpr float RING_SCALE = 2.2F;
constexpr float FLEET_HOVER = 14.0F;
constexpr float REGION_RADIUS = 62.0F;
constexpr float REGION_FLATTEN = 0.42F;

/// The background star field: thirty dots in the map's projected space, unprojected (README).
/// They are a texture, not geometry, which is why they are literals rather than graph data.
struct Star
{
  float x;
  float y;
  float radius;
};

constexpr std::array<Star, 30> STARS = {{
  {40, 60, 1.0F},   {120, 30, 0.8F},  {210, 90, 1.2F},  {330, 40, 0.7F},  {450, 70, 1.0F},  {560, 20, 0.9F},
  {690, 60, 1.1F},  {760, 130, 0.8F}, {70, 180, 0.9F},  {160, 140, 0.7F}, {240, 170, 1.0F}, {410, 150, 0.8F},
  {520, 110, 1.2F}, {600, 200, 0.8F}, {740, 220, 1.0F}, {30, 300, 0.9F},  {90, 400, 1.0F},  {200, 440, 0.8F},
  {290, 500, 1.1F}, {380, 360, 0.7F}, {470, 470, 1.0F}, {600, 520, 0.9F}, {720, 330, 0.8F}, {770, 480, 1.0F},
  {140, 530, 0.8F}, {50, 480, 1.1F},  {640, 440, 0.7F}, {350, 230, 0.9F}, {500, 380, 0.8F}, {250, 280, 0.7F},
}};

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
        const std::int32_t origin = fleet.order == FleetOrder::Move ? fleet.to : fleet.from;
        fleet.from = origin;
        fleet.to = region->index;
        fleet.order = FleetOrder::Move;
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

void MainPage::Draw(ShapeRenderer& _shapes, FontRenderer& _text)
{
  m_hits.clear();

  _shapes.FillRect(0.0F, 0.0F, SCREEN_WIDTH, SCREEN_HEIGHT, APP_BACKGROUND);

  DrawTopBar(_shapes, _text);
  DrawDigestRail(_shapes, _text);
  DrawMap(_shapes, _text);
  DrawOrdersRail(_shapes, _text);
  DrawPanel(_shapes, _text);
}

void MainPage::DrawTopBar(ShapeRenderer& _shapes, FontRenderer& _text)
{
  const std::int32_t centered = CenterTextY(0.0F, TOP_BAR_HEIGHT);
  _shapes.FillRect(0.0F, TOP_BAR_HEIGHT - 1.0F, SCREEN_WIDTH, 1.0F, CARD_BORDER);

  _text.DrawText(16, centered, "FRONTIER OUTPOST", TEXT_PRIMARY);

  // "DAY 12/21" rather than "DAY 12 / 21", and the countdown and replay labels use T-notation:
  // at 8px the reference's spelled-out bar is 63px wider than the frame (ADR-014).
  const std::string matchLine =
    std::format("MATCH {} - DAY {}/{} - ENDS {}", m_state.match.id, m_state.match.day, m_state.match.totalDays, m_state.match.endsAt);
  _text.DrawText(16 + static_cast<std::int32_t>(FontRenderer::MeasurePixels("FRONTIER OUTPOST")) + 10, centered, matchLine, TEXT_MUTED);

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

  const std::string leaderLine = std::format("LEAD {} {}", m_state.player.leader.name, FormatScore(m_state.player.leader.score));
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
  const std::string countdown = FormatCountdown(m_state.match.secondsToLock);
  const std::int32_t bigY = CenterTextY(0.0F, TOP_BAR_HEIGHT, FontRenderer::COUNTDOWN_SCALE);
  DrawRight(_text, cursor, bigY, countdown, AMBER, FontRenderer::COUNTDOWN_SCALE);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(countdown, FontRenderer::COUNTDOWN_SCALE)) + 8.0F;

  DrawRight(_text, cursor, centered, std::format("T{} LOCKS IN", m_state.OrdersTick()), TEXT_MUTED);
}

void MainPage::DrawDigestRail(ShapeRenderer& _shapes, FontRenderer& _text)
{
  _shapes.FillRect(DIGEST_WIDTH - 1.0F, TOP_BAR_HEIGHT, 1.0F, SCREEN_HEIGHT - TOP_BAR_HEIGHT, CARD_BORDER);

  const std::int32_t headerY = static_cast<std::int32_t>(TOP_BAR_HEIGHT) + 12;
  _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), headerY, std::format("DIGEST - TICK {}", m_state.match.tick), TEXT_MUTED);
  DrawRight(_text, DIGEST_WIDTH - RAIL_PADDING, headerY, std::format("{} EVENTS", m_state.digest.size()), TEXT_MUTED);

  // The rail is 300 wide; a row spends 14 on each margin, 8 on the dot and 10 on the gap, which
  // leaves 254 -- 31 characters of an 8px font. Most of the reference's copy is longer than that,
  // so it wraps rather than truncating: a digest that hides its second half is not a digest.
  constexpr float TEXT_LEFT = RAIL_PADDING + 8.0F + 10.0F;
  const float textWidth = DIGEST_WIDTH - TEXT_LEFT - RAIL_PADDING;
  const std::size_t columns = FontRenderer::FitCharacters(static_cast<std::uint32_t>(textWidth));

  float y = TOP_BAR_HEIGHT + 28.0F;
  for (std::size_t index = 0; index < m_state.digest.size(); ++index)
  {
    const DigestEvent& event = m_state.digest[index];
    const std::vector<std::string> title = FontRenderer::Wrap(event.title, columns);
    const std::vector<std::string> detail = FontRenderer::Wrap(event.detail, columns);

    const auto lines = static_cast<float>(title.size() + detail.size());
    const float height = 20.0F + lines * static_cast<float>(LINE_HEIGHT) + 2.0F;

    _shapes.FillRect(0.0F, y, DIGEST_WIDTH - 1.0F, 1.0F, DIVIDER);
    if (m_focusedSystem != EventRefs::NONE && event.refs.system == m_focusedSystem)
    {
      _shapes.FillRect(0.0F, y + 1.0F, DIGEST_WIDTH - 1.0F, height - 1.0F, HOVER_FILL);
    }

    const Color accent = EventColor(event.kind);
    // The top event carries a 2px bar in its colour: the digest is sorted by consequence and the
    // first row is what the tick was actually about.
    if (index == 0)
    {
      _shapes.FillRect(0.0F, y + 1.0F, 2.0F, height - 1.0F, accent);
    }

    std::int32_t lineY = static_cast<std::int32_t>(y) + 11;
    _shapes.FillEllipse(RAIL_PADDING + 4.0F, static_cast<float>(lineY) + 4.0F, 4.0F, 4.0F, accent);

    for (const std::string& line : title)
    {
      _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT), lineY, line, TEXT_PRIMARY);
      lineY += LINE_HEIGHT;
    }
    lineY += 2;
    for (const std::string& line : detail)
    {
      _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT), lineY, line, TEXT_DETAIL);
      lineY += LINE_HEIGHT;
    }

    AddHit(0.0F, y, DIGEST_WIDTH - 1.0F, height, Action::FocusEvent, static_cast<std::int32_t>(index));
    y += height;
  }
}

void MainPage::DrawMap(ShapeRenderer& _shapes, FontRenderer& _text)
{
  const float paneX = DIGEST_WIDTH;
  const float paneWidth = SCREEN_WIDTH - DIGEST_WIDTH - ORDERS_WIDTH;
  const float paneHeight = SCREEN_HEIGHT - TOP_BAR_HEIGHT;
  const MapProjection projection{paneX, TOP_BAR_HEIGHT, paneWidth, paneHeight};

  _shapes.FillVerticalGradient(paneX, TOP_BAR_HEIGHT, paneWidth, paneHeight, MAP_TOP, MAP_MIDDLE, 0.45F, MAP_BOTTOM);

  // The star field is unprojected: it is behind the plane, not on it.
  for (const Star& star : STARS)
  {
    const float x = paneX + star.x * projection.LetterboxScale();
    const float y = TOP_BAR_HEIGHT + (paneHeight - MapProjection::DESIGN_HEIGHT * projection.LetterboxScale()) * 0.5F +
                    star.y * projection.LetterboxScale();
    _shapes.FillEllipse(x, y, star.radius, star.radius, STAR);
  }

  // The ground grid, drawn as lines of constant y and constant x through the projection. It is
  // what makes the plane read as a plane rather than as a scatter of dots.
  // Counted in whole steps rather than by adding a float to itself: nine lines of constant depth
  // and thirteen of constant x, at exactly the spacing the reference uses.
  constexpr std::uint32_t DEPTH_LINES = static_cast<std::uint32_t>(MapProjection::DESIGN_HEIGHT / GRID_STEP_Y) + 1;
  constexpr std::uint32_t ACROSS_LINES = static_cast<std::uint32_t>((GRID_MAX_X - GRID_MIN_X) / GRID_STEP_X) + 1;

  for (std::uint32_t step = 0; step < DEPTH_LINES; ++step)
  {
    const float designY = static_cast<float>(step) * GRID_STEP_Y;
    const MapProjection::Point left = projection.Project(GRID_MIN_X, designY);
    const MapProjection::Point right = projection.Project(GRID_MAX_X, designY);
    _shapes.Line(left.x, left.y, right.x, right.y, GRID_LINE);
  }
  for (std::uint32_t step = 0; step < ACROSS_LINES; ++step)
  {
    const float designX = GRID_MIN_X + static_cast<float>(step) * GRID_STEP_X;
    // `atHorizon` and `atNearEdge`, not `far` and `near`: both of those are macros that
    // <windows.h> still defines to nothing, and a local called either one vanishes.
    const MapProjection::Point atHorizon = projection.Project(designX, 0.0F);
    const MapProjection::Point atNearEdge = projection.Project(designX, MapProjection::DESIGN_HEIGHT);
    _shapes.Line(atHorizon.x, atHorizon.y, atNearEdge.x, atNearEdge.y, GRID_LINE);
  }

  const MapProjection::Point horizon = projection.Project(MapProjection::CENTER_X, 0.0F);
  _shapes.FillRadialGradient(horizon.x, horizon.y, paneWidth * 0.6F, paneHeight * 0.35F, HORIZON_GLOW, HORIZON_GLOW_RIM);

  const Graph& graph = m_state.graph;
  const auto groundOf = [&projection, &graph](std::int32_t _system)
  {
    const SystemNode& node = graph.systems[static_cast<std::size_t>(_system)];
    return projection.Project(node.positionX, node.positionY);
  };

  // Lanes lie ON the plane, under everything that stands on it.
  for (const Lane& lane : graph.lanes)
  {
    const MapProjection::Point a = groundOf(lane.a);
    const MapProjection::Point b = groundOf(lane.b);

    switch (lane.kind)
    {
    case LaneKind::Trade:
      // A trade lane is public and is meant to be read at a glance: it is the one consensual
      // mechanic in the game, and cancelling one is a tell (one-pager, decision 3).
      _shapes.Line(a.x, a.y, b.x, b.y, BLUE, 2.5F);
      break;
    case LaneKind::Proposed:
      _shapes.DashedLine(a.x, a.y, b.x, b.y, BLUE, 2.0F, 4.0F, 5.0F);
      break;
    case LaneKind::None:
    default:
      _shapes.Line(a.x, a.y, b.x, b.y, LANE_PLAIN, 1.2F);
      break;
    }

    DrawCentered(_text, (a.x + b.x) * 0.5F, static_cast<std::int32_t>(std::lround((a.y + b.y) * 0.5F)) - 4, std::to_string(lane.cost),
                 TEXT_DETAIL);
  }

  // The sealed region: everyone can see it and count down to it, which is what makes it a race
  // rather than a reward (one-pager, "Pacing devices").
  if (m_state.region.anchor != EventRefs::NONE)
  {
    const SystemNode& anchor = graph.systems[static_cast<std::size_t>(m_state.region.anchor)];
    const MapProjection::Point center = groundOf(m_state.region.anchor);
    const float scale = MapProjection::ScaleAt(anchor.positionY);
    const float radiusX = projection.ToScreen(REGION_RADIUS * scale);
    const float radiusY = radiusX * REGION_FLATTEN;

    _shapes.FillEllipse(center.x, center.y, radiusX, radiusY, WithAlpha(PURPLE, 20));
    _shapes.DashedEllipse(center.x, center.y, radiusX, radiusY, PURPLE, 1.0F, 5.0F, 5.0F);
    // A second ellipse lifted by its own height, so the region reads as a volume rather than a
    // puddle.
    _shapes.DashedEllipse(center.x, center.y - radiusY, radiusX, radiusY, WithAlpha(PURPLE, 89), 1.0F, 5.0F, 5.0F);

    for (const auto& [offsetX, offsetY] : m_state.region.siteOffsets)
    {
      const float siteX = center.x + projection.ToScreen(offsetX * scale);
      const float siteY = center.y + projection.ToScreen(offsetY * scale);
      const float lift = projection.ToScreen(FLEET_HOVER * scale);
      _shapes.Line(siteX, siteY, siteX, siteY - lift, WithAlpha(PURPLE, 153));
      _shapes.FillEllipse(siteX, siteY - lift, 2.5F, 2.5F, PURPLE);
    }

    DrawCentered(_text, center.x, static_cast<std::int32_t>(std::lround(center.y + radiusY + 8.0F)),
                 std::format("SEALED - OPENS T{}", m_state.region.opensAt), PURPLE);
  }

  // Systems stand on the plane: a shadow where they touch it, a stem, then the node.
  for (std::size_t index = 0; index < graph.systems.size(); ++index)
  {
    const SystemNode& node = graph.systems[index];
    if (HasFlag(node.flags, SystemFlags::RegionAnchor))
    {
      continue;
    }

    const MapProjection::Point ground = groundOf(static_cast<std::int32_t>(index));
    const float scale = MapProjection::ScaleAt(node.positionY);
    const bool capital = HasFlag(node.flags, SystemFlags::Capital);
    const float radius = projection.ToScreen((capital ? CAPITAL_RADIUS : NODE_RADIUS) * scale);
    const float stem = projection.ToScreen((capital ? CAPITAL_STEM_HEIGHT : STEM_HEIGHT) * scale);
    const Color owner = OwnerColor(node.owner);
    const float topY = ground.y - stem;

    _shapes.FillEllipse(ground.x, ground.y, radius * SHADOW_WIDE, radius * SHADOW_TALL, WithAlpha(owner, 56));
    _shapes.Line(ground.x, ground.y, ground.x, topY, WithAlpha(owner, 140));

    if (capital)
    {
      _shapes.FillEllipse(ground.x, topY, radius * HALO_SCALE, radius * HALO_SCALE, WithAlpha(owner, 46));
    }
    if (HasFlag(node.flags, SystemFlags::Contested))
    {
      _shapes.StrokeEllipse(ground.x, topY, radius * RING_SCALE, radius * RING_SCALE, owner);
    }
    if (node.custodianSince != 0)
    {
      _shapes.DashedEllipse(ground.x, topY, radius * 2.0F, radius * 2.0F, owner, 1.0F, 2.0F, 3.0F);
    }
    if (static_cast<std::int32_t>(index) == m_focusedSystem)
    {
      _shapes.StrokeEllipse(ground.x, topY, radius * 3.0F, radius * 3.0F, TEXT_PRIMARY);
    }

    _shapes.FillEllipse(ground.x, topY, radius, radius, owner);

    // Labels are 8px wherever they are on the plane. The reference draws every one of them at one
    // size too; only the geometry takes the depth scale (ADR-014).
    //
    // A capital is uppercased rather than given a weight, because the font has one weight and
    // emphasis on this screen is colour and case (README "Frame"). It is also the one piece of
    // hierarchy the map needs: a capital is what a war is ultimately about.
    std::string label = node.name;
    if (capital)
    {
      std::transform(label.begin(), label.end(), label.begin(), [](unsigned char _c) { return static_cast<char>(std::toupper(_c)); });
    }
    DrawCentered(_text, ground.x, static_cast<std::int32_t>(std::lround(topY - radius - 13.0F)), label, TEXT_PRIMARY);

    if (node.custodianSince != 0)
    {
      DrawCentered(_text, ground.x, static_cast<std::int32_t>(std::lround(ground.y + 8.0F)),
                   std::format("CUSTODIAN T{}", node.custodianSince), TEXT_MUTED);
    }
    if (node.capturedAt != 0)
    {
      DrawCentered(_text, ground.x, static_cast<std::int32_t>(std::lround(ground.y + 8.0F)), std::format("CAPTURED T{}", node.capturedAt),
                   RED);
    }

    AddHit(ground.x - radius * 3.0F, topY - radius * 3.0F, radius * 6.0F, stem + radius * 6.0F, Action::OpenSystem,
           static_cast<std::int32_t>(index));
  }

  // Fleets hover above their lane at the progress fraction, with the tick they arrive. Once a
  // fleet has departed it is public, so a rival's is drawn exactly like yours in their colour.
  for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
  {
    const Fleet& fleet = m_state.fleets[index];
    if (fleet.order != FleetOrder::Move || fleet.from == fleet.to)
    {
      continue;
    }

    const MapProjection::Point from = groundOf(fleet.from);
    const MapProjection::Point to = groundOf(fleet.to);
    const float x = from.x + (to.x - from.x) * fleet.progress;
    const float y = from.y + (to.y - from.y) * fleet.progress;

    const float scale = projection.ToScreen(FLEET_HOVER);
    const float tipY = y - scale;
    const Color owner = OwnerColor(fleet.owner);

    _shapes.Line(x, y, x, tipY, WithAlpha(owner, 153));

    // The arrowhead points along the lane, so which way a fleet is going is readable without the
    // label -- which matters because the label is the first thing that collides when two fleets
    // converge on one system, exactly as they are doing here.
    const float runX = to.x - from.x;
    const float runY = to.y - from.y;
    const float run = std::sqrt(runX * runX + runY * runY);
    const float dirX = run > 0.0F ? runX / run : 1.0F;
    const float dirY = run > 0.0F ? runY / run : 0.0F;
    constexpr float ARROW = 6.0F;
    _shapes.FillTriangle(x + dirX * ARROW, tipY + dirY * ARROW, x - dirX * ARROW - dirY * ARROW * 0.8F,
                         tipY - dirY * ARROW + dirX * ARROW * 0.8F, x - dirX * ARROW + dirY * ARROW * 0.8F,
                         tipY - dirY * ARROW - dirX * ARROW * 0.8F, owner);

    // Two fleets converging on one system put their labels in the same place -- which is exactly
    // what is happening at Kepler-Reach in the reference, and is the normal case rather than an
    // edge one, because converging is what fleets do. YOURS goes above the arrowhead, where you
    // are already looking; a rival's goes beside it. The two can then never overlap, and which
    // one is yours is readable before you have read either.
    const std::string label = std::format("{} - ETA T{}", fleet.name, fleet.eta);
    const auto labelWidth = static_cast<float>(FontRenderer::MeasurePixels(label));
    const auto labelY = static_cast<std::int32_t>(std::lround(tipY - 18.0F));
    if (fleet.owner == Owner::You)
    {
      const float clamped = std::clamp(x, paneX + labelWidth * 0.5F + 4.0F, paneX + paneWidth - labelWidth * 0.5F - 4.0F);
      DrawCentered(_text, clamped, labelY, label, owner);
    }
    else
    {
      // Beside AND one line below. Sideways alone is not enough when the two arrowheads are a
      // dozen pixels apart, which is what "one tick out from yours" looks like on the map.
      const float placed = std::min(x + 10.0F, paneX + paneWidth - labelWidth - 4.0F);
      _text.DrawText(static_cast<std::int32_t>(std::lround(placed)), labelY + LINE_HEIGHT, label, owner);
    }

    if (fleet.owner == Owner::You)
    {
      AddHit(x - 14.0F, tipY - 22.0F, 28.0F, 36.0F, Action::OpenFleet, static_cast<std::int32_t>(index));
    }
  }

  DrawRight(
    _text, paneX + paneWidth - 12.0F, static_cast<std::int32_t>(TOP_BAR_HEIGHT) + 12,
    std::format("{} PLAYERS - {} SYSTEMS - {} UNCLAIMED", m_state.player.playerCount, m_state.totalSystems, m_state.unclaimedSystems),
    TEXT_DETAIL);

  // The legend earns its place: the owner colours are also the semantic colours, so a player who
  // learns this row can read every other coloured thing on the screen.
  struct LegendEntry
  {
    const char* label;
    Color color;
    bool isLane;
    bool dashed;
  };
  const std::array<LegendEntry, 5> legend = {{
    {"YOU", BLUE, false, false},
    {"HALVORSEN", AMBER, false, false},
    {"SORNE", RED, false, false},
    {"PROPOSED LANE", BLUE, true, true},
    {"TRADE LANE", BLUE, true, false},
  }};

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
}

void MainPage::DrawOrdersRail(ShapeRenderer& _shapes, FontRenderer& _text)
{
  const float railX = SCREEN_WIDTH - ORDERS_WIDTH;
  const float contentX = railX + RAIL_PADDING;
  const float contentRight = SCREEN_WIDTH - RAIL_PADDING;
  const float cardWidth = contentRight - contentX;
  const std::size_t columns = FontRenderer::FitCharacters(static_cast<std::uint32_t>(cardWidth - 2.0F * CARD_PADDING));

  _shapes.FillRect(railX, TOP_BAR_HEIGHT, 1.0F, SCREEN_HEIGHT - TOP_BAR_HEIGHT, CARD_BORDER);

  const std::int32_t headerY = static_cast<std::int32_t>(TOP_BAR_HEIGHT) + 12;
  _text.DrawText(static_cast<std::int32_t>(contentX), headerY, std::format("ORDERS - TICK {}", m_state.OrdersTick()), TEXT_MUTED);
  // The one word that says whether anything on this rail can still be changed.
  DrawRight(_text, contentRight, headerY, m_state.orders.locked ? "LOCKED" : "UNLOCKED", m_state.orders.locked ? TEXT_MUTED : AMBER);

  float y = TOP_BAR_HEIGHT + 28.0F;

  const auto sectionHeader = [&](std::string_view _label, std::string_view _count)
  {
    _shapes.FillRect(railX + 1.0F, y, ORDERS_WIDTH - 1.0F, 1.0F, DIVIDER);
    _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(y) + 8, _label, TEXT_MUTED);
    DrawRight(_text, contentRight, static_cast<std::int32_t>(y) + 8, _count, TEXT_MUTED);
    y += 22.0F;
  };

  // ---- FLEETS -------------------------------------------------------------------------------
  std::uint32_t yours = 0;
  for (const Fleet& fleet : m_state.fleets)
  {
    yours += fleet.owner == Owner::You ? 1U : 0U;
  }
  sectionHeader("FLEETS", std::to_string(yours));

  for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
  {
    const Fleet& fleet = m_state.fleets[index];
    if (fleet.owner != Owner::You)
    {
      continue;
    }

    const std::vector<std::string> status = FontRenderer::Wrap(fleet.status, columns);
    const std::vector<std::string> preview = FontRenderer::Wrap(fleet.preview, columns);
    const auto lines = static_cast<float>(1 + status.size() + preview.size());
    const float height = 2.0F * CARD_PADDING - 4.0F + lines * static_cast<float>(LINE_HEIGHT);

    _shapes.FillRect(contentX, y, cardWidth, height, CARD_FILL);
    _shapes.StrokeRect(contentX, y, cardWidth, height, CARD_BORDER);

    std::int32_t lineY = static_cast<std::int32_t>(y) + 8;
    _text.DrawText(static_cast<std::int32_t>(contentX + CARD_PADDING), lineY, std::format("{} - {} ships", fleet.name, fleet.ships),
                   TEXT_PRIMARY);

    const std::string destination = fleet.order == FleetOrder::Move
                                      ? std::format("> {}", m_state.graph.systems[static_cast<std::size_t>(fleet.to)].name)
                                      : std::format("HOLD {}", m_state.graph.systems[static_cast<std::size_t>(fleet.from)].name);
    DrawRight(_text, contentRight - CARD_PADDING, lineY, destination, fleet.order == FleetOrder::Move ? BLUE : TEXT_DETAIL);
    lineY += LINE_HEIGHT;

    // "change >" sits on the STATUS line, whose right half is the only part of a fleet card that
    // is reliably free: line one carries the destination and the last line carries the combat
    // preview, which is the longest string on the card.
    bool changeDrawn = m_state.orders.locked;
    for (const std::string& line : status)
    {
      _text.DrawText(static_cast<std::int32_t>(contentX + CARD_PADDING), lineY, line, TEXT_DETAIL);
      if (!changeDrawn)
      {
        DrawRight(_text, contentRight - CARD_PADDING, lineY, "change >", BLUE);
        changeDrawn = true;
      }
      lineY += LINE_HEIGHT;
    }
    for (const std::string& line : preview)
    {
      // The engagement preview. Combat is deterministic, so this is not a guess -- it is what
      // will happen if both fleets are still there at the lock (one-pager, "Shape of a game").
      _text.DrawText(static_cast<std::int32_t>(contentX + CARD_PADDING), lineY, line, TEXT_DETAIL);
      lineY += LINE_HEIGHT;
    }

    AddHit(contentX, y, cardWidth, height, Action::OpenFleet, static_cast<std::int32_t>(index));
    y += height + 6.0F;
  }

  // ---- BUILDS -------------------------------------------------------------------------------
  sectionHeader("BUILDS", "38 AVAILABLE");

  for (std::size_t index = 0; index < m_state.orders.builds.size(); ++index)
  {
    const BuildRow& row = m_state.orders.builds[index];
    const bool queued = std::find(m_state.orders.queuedBuilds.begin(), m_state.orders.queuedBuilds.end(),
                                  static_cast<std::int32_t>(index)) != m_state.orders.queuedBuilds.end();

    const std::string label = row.isTradeLane ? "PROPOSE" : (queued ? "QUEUED" : "BUILD");
    const float buttonWidth = static_cast<float>(FontRenderer::MeasurePixels(label)) + 20.0F;
    const std::size_t rowColumns =
      FontRenderer::FitCharacters(static_cast<std::uint32_t>(cardWidth - 2.0F * CARD_PADDING - buttonWidth - 8.0F));

    const std::vector<std::string> detail = FontRenderer::Wrap(row.detail, rowColumns);
    const auto lines = static_cast<float>(1 + detail.size());
    const float height = 2.0F * CARD_PADDING - 4.0F + lines * static_cast<float>(LINE_HEIGHT);

    // The trade lane is a building with two owners, so it sits here with the shipyard rather than
    // in a diplomacy tab that does not exist -- dashed and amber until the neighbour accepts.
    if (row.isTradeLane)
    {
      _shapes.DashedLine(contentX, y, contentX + cardWidth, y, WithAlpha(AMBER, 128), 1.0F, 4.0F, 3.0F);
      _shapes.DashedLine(contentX, y + height, contentX + cardWidth, y + height, WithAlpha(AMBER, 128), 1.0F, 4.0F, 3.0F);
      _shapes.DashedLine(contentX, y, contentX, y + height, WithAlpha(AMBER, 128), 1.0F, 4.0F, 3.0F);
      _shapes.DashedLine(contentX + cardWidth, y, contentX + cardWidth, y + height, WithAlpha(AMBER, 128), 1.0F, 4.0F, 3.0F);
    }
    else
    {
      _shapes.StrokeRect(contentX, y, cardWidth, height, CARD_BORDER);
    }

    std::int32_t lineY = static_cast<std::int32_t>(y) + 8;
    _text.DrawText(static_cast<std::int32_t>(contentX + CARD_PADDING), lineY, row.title, TEXT_PRIMARY);
    lineY += LINE_HEIGHT;
    for (const std::string& line : detail)
    {
      _text.DrawText(static_cast<std::int32_t>(contentX + CARD_PADDING), lineY, line, TEXT_DETAIL);
      lineY += LINE_HEIGHT;
    }

    const float buttonX = contentRight - CARD_PADDING - buttonWidth;
    const float buttonY = y + (height - 18.0F) * 0.5F;
    const bool inert = m_state.orders.locked;
    if (row.isTradeLane)
    {
      _shapes.StrokeRect(buttonX, buttonY, buttonWidth, 18.0F, inert ? OUTLINE : AMBER);
      _text.DrawText(static_cast<std::int32_t>(buttonX + 10.0F), CenterTextY(buttonY, 18.0F), label, inert ? TEXT_MUTED : AMBER);
    }
    else if (queued)
    {
      _shapes.StrokeRect(buttonX, buttonY, buttonWidth, 18.0F, BLUE);
      _text.DrawText(static_cast<std::int32_t>(buttonX + 10.0F), CenterTextY(buttonY, 18.0F), label, BLUE);
    }
    else
    {
      _shapes.FillRect(buttonX, buttonY, buttonWidth, 18.0F, inert ? OUTLINE : BLUE);
      _text.DrawText(static_cast<std::int32_t>(buttonX + 10.0F), CenterTextY(buttonY, 18.0F), label, APP_BACKGROUND);
    }

    AddHit(buttonX, buttonY, buttonWidth, 18.0F, Action::ToggleBuild, static_cast<std::int32_t>(index));
    y += height + 6.0F;
  }

  // ---- PROPOSALS ----------------------------------------------------------------------------
  sectionHeader("PROPOSALS", std::format("{} OPEN", m_state.proposals.size()));

  for (std::size_t index = 0; index < m_state.proposals.size(); ++index)
  {
    const Proposal& proposal = m_state.proposals[index];
    const std::vector<std::string> terms = FontRenderer::Wrap(proposal.terms, columns);
    const float height = 2.0F * CARD_PADDING + static_cast<float>(1 + terms.size()) * static_cast<float>(LINE_HEIGHT) + 24.0F;

    _shapes.FillRect(contentX, y, cardWidth, height, WithAlpha(BLUE, 15));
    _shapes.StrokeRect(contentX, y, cardWidth, height, WithAlpha(BLUE, 102));

    std::int32_t lineY = static_cast<std::int32_t>(y) + 8;
    _text.DrawText(static_cast<std::int32_t>(contentX + CARD_PADDING), lineY, std::format("{} - open lane", proposal.from), TEXT_PRIMARY);
    // The countdown on a proposal is the same form as a fleet's ETA, because it is the same kind
    // of thing: something in flight with a tick attached (one-pager, "Diplomacy UI").
    DrawRight(_text, contentRight - CARD_PADDING, lineY, std::format("{} TICKS", proposal.ticksLeft), AMBER);
    lineY += LINE_HEIGHT;

    for (const std::string& line : terms)
    {
      _text.DrawText(static_cast<std::int32_t>(contentX + CARD_PADDING), lineY, line, TEXT_DETAIL);
      lineY += LINE_HEIGHT;
    }

    const bool answered = m_state.orders.answeredProposal == static_cast<std::int32_t>(index);
    const bool accepted = answered && m_state.orders.acceptedProposal;
    const float buttonY = y + height - CARD_PADDING - 18.0F;
    const float buttonWidth = (cardWidth - 2.0F * CARD_PADDING - 6.0F) * 0.5F;

    _shapes.FillRect(contentX + CARD_PADDING, buttonY, buttonWidth, 18.0F, accepted || !answered ? BLUE : OUTLINE);
    DrawCentered(_text, contentX + CARD_PADDING + buttonWidth * 0.5F, CenterTextY(buttonY, 18.0F), accepted ? "ACCEPTED" : "ACCEPT",
                 APP_BACKGROUND);

    const float declineX = contentX + CARD_PADDING + buttonWidth + 6.0F;
    const bool declined = answered && !m_state.orders.acceptedProposal;
    _shapes.StrokeRect(declineX, buttonY, buttonWidth, 18.0F, declined ? RED : OUTLINE);
    DrawCentered(_text, declineX + buttonWidth * 0.5F, CenterTextY(buttonY, 18.0F), declined ? "DECLINED" : "DECLINE",
                 declined ? RED : TEXT_PRIMARY);

    AddHit(contentX + CARD_PADDING, buttonY, buttonWidth, 18.0F, Action::AcceptProposal, static_cast<std::int32_t>(index));
    AddHit(declineX, buttonY, buttonWidth, 18.0F, Action::DeclineProposal, static_cast<std::int32_t>(index));
    y += height + 6.0F;
  }

  // ---- FOOTER -------------------------------------------------------------------------------
  // The sentence that explains the whole rail: the three columns are one commitment.
  constexpr float FOOTER_HEIGHT = 36.0F;
  const float footerY = SCREEN_HEIGHT - FOOTER_HEIGHT;
  _shapes.FillRect(railX + 1.0F, footerY, ORDERS_WIDTH - 1.0F, 1.0F, CARD_BORDER);
  _text.DrawText(static_cast<std::int32_t>(contentX), CenterTextY(footerY, FOOTER_HEIGHT), "ALL 3 LOCK TOGETHER", TEXT_DETAIL);
  DrawRight(_text, contentRight, CenterTextY(footerY, FOOTER_HEIGHT, FontRenderer::COUNTDOWN_SCALE),
            FormatCountdown(m_state.match.secondsToLock), AMBER, FontRenderer::COUNTDOWN_SCALE);
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

  switch (m_panel)
  {
  case Panel::BuildList:
  {
    const SystemNode& node = m_state.graph.systems[static_cast<std::size_t>(m_panelSubject)];
    title = std::format("BUILD - {}", node.name);
    for (const BuildRow& row : m_state.orders.builds)
    {
      rows.push_back(row.title);
      rowTargets.push_back(EventRefs::NONE);
    }
    break;
  }
  case Panel::Destination:
  {
    const Fleet& fleet = m_state.fleets[static_cast<std::size_t>(m_panelSubject)];
    title = std::format("MOVE {} - PICK LANE", fleet.name);
    const std::int32_t origin = fleet.order == FleetOrder::Move ? fleet.to : fleet.from;
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
      AddHit(x, rowY, width, 20.0F, Action::ChooseDestination, rowTargets[index]);
    }
    rowY += 20.0F;
  }
}

} // namespace Frontier
