// MainPageMove.cpp -- the move mode: the banner, the reachable systems, the confirm strip (ADR-114).
//
// A move is chosen ON the map. The sheet collapses to a strip, the systems one lane away light with a
// ring and an ETA chip, lighting one is a SELECTION and the strip's filled `SEND` is the order. The
// map's own drawing of the mode is `MapRender`'s; this unit decides what is reachable, what the
// strip says, and what the rest of the screen does while the mode is on.

#include "pch.h"
#include "MainPage.h"

#include "Controls.h"
#include "DesignTokens.h"
#include "MainPageParts.h"
#include "MapRender.h"

#include <algorithm>

namespace Lockstep
{

namespace
{

using Neuron::Color;
using Neuron::Face;
using Neuron::FontRenderer;
using Neuron::ShapeRenderer;

/// What a tap does, which the rows composed in here have to name (ADR-113). The page's own enum,
/// aliased rather than qualified thirty times.
using Action = MainPage::Action;

/// **What is standing on a system, which is what a lane into it is a fight or an expansion by**
/// (ADR-063). Every hostile fleet parked there is an incumbent by the time a fleet ordered this
/// tick lands -- the rule `TickResolver::Preview` applies -- so anything here fights with the
/// defender's bonus and the line says so in the words the verdict box uses. Never a verdict: the
/// wire carries one only for where a fleet is already flying.
[[nodiscard]] std::string StandingAt(const MatchState& _state, std::int32_t _system)
{
  if (_system < 0 || _system >= static_cast<std::int32_t>(_state.graph.systems.size()))
  {
    return {};
  }
  const SystemNode& node = _state.graph.systems[static_cast<std::size_t>(_system)];

  std::string held;
  if (node.owner == NOBODY)
  {
    held = "UNCLAIMED";
  }
  else if (node.owner == _state.viewer)
  {
    held = "YOURS";
  }
  else if (node.owner < static_cast<OwnerId>(_state.players.size()))
  {
    held = _state.players[static_cast<std::size_t>(node.owner)].label;
  }
  else
  {
    held = "RIVAL";
  }

  std::uint32_t garrison = 0;
  for (const Fleet& standing : _state.fleets)
  {
    const bool hostile = standing.owner != NOBODY && standing.owner != _state.viewer;
    if (hostile && standing.from == standing.to && standing.to == _system)
    {
      garrison += standing.ships;
    }
  }
  if (garrison > 0)
  {
    held += std::format(" · {} +DEF", garrison);
  }
  if (HasFlag(node.flags, SystemFlags::Capital))
  {
    held += " · CAPITAL";
  }
  if (HasFlag(node.flags, SystemFlags::Contested))
  {
    held += " · CONTESTED";
  }
  return held;
}

} // namespace

void MainPage::ReopenMove(std::int32_t _fleetId)
{
  // **A mode is a sheet for this purpose** (ADR-065): the tick landing under a player who is in the
  // middle of choosing is the reason they opened it, not a reason to take it away. Put back only
  // when the fleet is still on this board, still theirs and still able to take an order -- a fleet
  // the lock sent away is one ADR-077 says has nothing left to offer.
  if (_fleetId == EventRefs::NONE)
  {
    return;
  }
  for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
  {
    if (m_state.fleets[index].id == _fleetId)
    {
      EnterMove(static_cast<std::int32_t>(index));
      return;
    }
  }
}

void MainPage::EnterMove(std::int32_t _fleet)
{
  if (!OrdersEditable() || _fleet < 0 || _fleet >= static_cast<std::int32_t>(m_state.fleets.size()))
  {
    return;
  }
  const Fleet& fleet = m_state.fleets[static_cast<std::size_t>(_fleet)];

  // The guard behind every control that leads here, by the rule the lock would refuse the order by
  // (ADR-077). A fleet the server already has on a lane takes no second order.
  if (fleet.owner != m_state.viewer || fleet.underWay)
  {
    return;
  }

  // **The sheet collapses to the strip** (ADR-114): the question is now about the map, and a sheet
  // over the map while the answer is on it would be the thing this mode exists to stop.
  m_panel = Panel::None;
  m_armedConcede = EventRefs::NONE;
  m_focusedSystem = fleet.from;

  // A fleet that already has a move opens with that move lit, so re-entering the mode is a way to
  // change a destination rather than a way to start again (ADR-031).
  m_moveMode =
    MoveMode{.fleet = _fleet, .fleetId = fleet.id, .origin = fleet.from, .selected = fleet.OnALane() ? fleet.to : EventRefs::NONE};
}

void MainPage::ExitMove() noexcept
{
  m_moveMode.reset();
}

std::vector<MainPage::MoveTargetSystem> MainPage::ReachableFor(std::int32_t _fleet) const
{
  std::vector<MoveTargetSystem> reachable;
  if (_fleet < 0 || _fleet >= static_cast<std::int32_t>(m_state.fleets.size()))
  {
    return reachable;
  }
  const std::int32_t origin = m_state.fleets[static_cast<std::size_t>(_fleet)].from;

  // **One lane and no further.** `Match::Validate` refuses any destination that is not one lane from
  // where the fleet stands (`NoLaneToDestination`), so the handoff's "multi-hop within the fleet's
  // range if the rules allow" resolves to the adjacent systems: offering anything else would be
  // lighting a system the lock is certain to refuse, which is what ADR-053 took off this screen.
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
    reachable.push_back(MoveTargetSystem{.system = other, .ticks = lane.cost, .arrivesAt = m_state.OrdersTick() + lane.cost - 1});
  }

  // **Nearest first, then by name** (ADR-092). The rows on the strip are read in that order and the
  // chips on the map are not read in any order at all, so the sort is about the list -- and doing it
  // once here is what keeps the two lists from being two lists.
  std::ranges::stable_sort(reachable,
                           [this](const MoveTargetSystem& _a, const MoveTargetSystem& _b)
                           {
                             if (_a.ticks != _b.ticks)
                             {
                               return _a.ticks < _b.ticks;
                             }
                             return m_state.graph.systems[static_cast<std::size_t>(_a.system)].name <
                                    m_state.graph.systems[static_cast<std::size_t>(_b.system)].name;
                           });
  return reachable;
}

Color MainPage::Faded(const Color& _color) const noexcept
{
  // **The digest fades while the map is taking a move** (ADR-114) and stops being a target: the one
  // filled control on the screen has to be the strip's `SEND` (ADR-089), and a column of live
  // controls beside it would be two answers to one question.
  constexpr float SHARE = 0.55F;
  return m_moveMode.has_value() ? WithAlpha(_color, static_cast<std::uint8_t>(std::lround(static_cast<float>(_color.alpha) * SHARE)))
                                : _color;
}

void MainPage::DrawMoveMode(ShapeRenderer& _shapes, FontRenderer& _text)
{
  if (!m_moveMode.has_value())
  {
    return;
  }
  const MoveMode& move = *m_moveMode;
  if (move.fleet < 0 || move.fleet >= static_cast<std::int32_t>(m_state.fleets.size()))
  {
    return;
  }
  const Fleet& fleet = m_state.fleets[static_cast<std::size_t>(move.fleet)];
  const std::vector<MoveTargetSystem> reachable = ReachableFor(move.fleet);

  const float paneX = Frame::DIGEST_WIDTH;
  const float paneWidth = Frame::SCREEN_WIDTH - Frame::DIGEST_WIDTH - Frame::ORDERS_WIDTH;
  const std::string ships = fleet.ships == 1 ? std::string{"1 SHIP"} : std::format("{} SHIPS", fleet.ships);

  // ---- The banner --------------------------------------------------------------------------------
  //
  // **It replaces the map's own corner caption rather than sitting beside it** (ADR-114). The mode
  // is a statement about the whole pane -- every lane on it has changed meaning -- so it is said
  // across the whole pane, in the blue everything the mode lit is drawn in.
  const float bannerY = Frame::TOP_BAR_HEIGHT;
  _shapes.FillRect(paneX, bannerY, paneWidth, TOUCH_FLOOR, Ink::MOVE_MODE_WASH);
  _shapes.FillRect(paneX, bannerY + TOUCH_FLOOR - 1.0F, paneWidth, 1.0F, WithAlpha(Ink::BLUE, 90));

  const std::int32_t bannerText = CenterTextY(bannerY, TOUCH_FLOOR);
  float bannerX = paneX + RAIL_PADDING;
  _shapes.FillRect(bannerX, bannerY + (TOUCH_FLOOR - 8.0F) * 0.5F, 8.0F, 8.0F, OwnerColor(m_state.viewer, m_state.viewer));
  bannerX += 8.0F + CARD_PADDING;

  const std::string moving = std::format("MOVE {}", Uppercased(fleet.name));
  _text.DrawText(static_cast<std::int32_t>(bannerX), bannerText, moving, Ink::TEXT_PRIMARY, Face::MonoMedium);
  bannerX += static_cast<float>(FontRenderer::MeasurePixels(moving, Face::MonoMedium)) + CARD_PADDING;

  const std::string from = std::format("{} FROM {}", ships, NameOfSystem(m_state, move.origin));
  _text.DrawText(static_cast<std::int32_t>(bannerX), bannerText, from, Ink::TEXT_MUTED);
  bannerX += static_cast<float>(FontRenderer::MeasurePixels(from)) + CARD_PADDING + 6.0F;

  // A sentence, so it is set in the sentence face (ADR-074). It says what to do, which is the one
  // thing a mode has to say that a label cannot.
  _text.DrawText(static_cast<std::int32_t>(bannerX), bannerText, reachable.empty() ? "Nothing is one lane from here." : "Tap a lit system.",
                 Ink::TEXT_PRIMARY, Face::SansMedium);

  // **The cancel is the whole right-hand end of the banner**, not the width of its label: it is the
  // way out of a mode, and a way out that is eight glyphs wide is a way out a thumb misses.
  constexpr float BANNER_CANCEL_WIDTH = 120.0F;
  const float cancelX = paneX + paneWidth - BANNER_CANCEL_WIDTH;
  DrawRight(_text, paneX + paneWidth - RAIL_PADDING, bannerText, "ESC · CANCEL", Ink::TEXT_MUTED);
  AddHit(cancelX, bannerY, BANNER_CANCEL_WIDTH, TOUCH_FLOOR, Action::CancelMove, 0);

  // ---- The confirm strip -------------------------------------------------------------------------
  //
  // A sheet in every dimension it shares with one (ADR-052) and a GRID in its body: the rows are a
  // fallback for the map above them rather than the primary way to choose, so two columns keep the
  // strip short and the map open.
  const float width = paneWidth - 2.0F * SHEET_MARGIN;
  const float x = paneX + SHEET_MARGIN;

  const std::size_t shown = std::min(reachable.size(), SHEET_MAXIMUM_ROWS);
  const std::size_t notShown = reachable.size() - shown;
  const std::size_t gridRows = (shown + STRIP_COLUMNS - 1) / STRIP_COLUMNS;

  float bodyHeight = 0.0F;
  if (gridRows > 0)
  {
    bodyHeight = 2.0F * CARD_PADDING + static_cast<float>(gridRows) * SHEET_ROW_HEIGHT + static_cast<float>(gridRows - 1) * BUTTON_GAP;
  }
  if (notShown > 0)
  {
    bodyHeight += SHEET_CLIPPED_HEIGHT;
  }

  const float height = SHEET_HEADER_HEIGHT + bodyHeight + SHEET_ACTION_HEIGHT;
  const float y = Frame::SCREEN_HEIGHT - SHEET_MARGIN - height;

  _shapes.FillRect(x, y, width, height, Ink::APP_BACKGROUND);
  _shapes.StrokeRect(x, y, width, height, Ink::CARD_BORDER);
  // The strip is a modal like every other sheet: it swallows the taps it is over (ADR-112).
  AddHit(x, y, width, height, Action::None, 0);

  // ---- Its header ---------------------------------------------------------------------------------
  //
  // `FLT 1 → FAROE`, and before a pick `FLT 1 →` with nothing after it -- an arrow pointing at a
  // question rather than a placeholder that reads like an answer.
  const std::int32_t headerText = CenterTextY(y, SHEET_HEADER_HEIGHT);
  const bool picked = move.selected != EventRefs::NONE;
  const std::string title = picked ? std::format("{} → {}", Uppercased(fleet.name), NameOfSystem(m_state, move.selected))
                                   : std::format("{} →", Uppercased(fleet.name));
  _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), CenterTextY(y, SHEET_HEADER_HEIGHT, Face::MonoDisplay), title,
                 Ink::TEXT_PRIMARY, Face::MonoDisplay);

  float headerX = x + CARD_PADDING + static_cast<float>(FontRenderer::MeasurePixels(title, Face::MonoDisplay)) + CARD_PADDING;
  const std::string listHint = "OR PICK FROM THE LIST";
  const float hintLeft = x + width - CARD_PADDING - static_cast<float>(FontRenderer::MeasurePixels(listHint));
  DrawRight(_text, x + width - CARD_PADDING, headerText, listHint, Ink::TEXT_MUTED);

  // **What stands there, and never a verdict** (ADR-063): the wire carries a preview only for where
  // a fleet is already flying, so the header says what the destination IS and leaves the arithmetic
  // to the server.
  if (picked)
  {
    const auto target =
      std::ranges::find_if(reachable, [&move](const MoveTargetSystem& _target) { return _target.system == move.selected; });
    const std::string standing = StandingAt(m_state, move.selected);
    const std::string facts = target != reachable.end() ? std::format("ARRIVES T{} · {}", target->arrivesAt, standing) : standing;
    if (headerX + static_cast<float>(FontRenderer::MeasurePixels(facts)) <= hintLeft - CARD_PADDING)
    {
      _text.DrawText(static_cast<std::int32_t>(headerX), headerText, facts, Ink::TEXT_MUTED);
    }
  }
  _shapes.FillRect(x, y + SHEET_HEADER_HEIGHT, width, 1.0F, Ink::DIVIDER);

  // ---- Its rows -----------------------------------------------------------------------------------
  const float cellWidth = (width - 2.0F * CARD_PADDING - BUTTON_GAP) / static_cast<float>(STRIP_COLUMNS);
  float rowY = y + SHEET_HEADER_HEIGHT + CARD_PADDING;

  for (std::size_t index = 0; index < shown; ++index)
  {
    const MoveTargetSystem& target = reachable[index];
    const SystemNode& node = m_state.graph.systems[static_cast<std::size_t>(target.system)];
    const bool chosen = target.system == move.selected;

    // The column and the row as INTEGERS first: a division inside the cast reads as a fraction lost
    // to a reader and to clang-tidy alike (bugprone-integer-division), and it is neither.
    const std::size_t column = index % STRIP_COLUMNS;
    const std::size_t row = index / STRIP_COLUMNS;
    const float cellX = x + CARD_PADDING + static_cast<float>(column) * (cellWidth + BUTTON_GAP);
    const float cellY = rowY + static_cast<float>(row) * (SHEET_ROW_HEIGHT + BUTTON_GAP);

    const bool hovered = m_pointerXPixels >= cellX && m_pointerXPixels < cellX + cellWidth && m_pointerYPixels >= cellY &&
                         m_pointerYPixels < cellY + SHEET_ROW_HEIGHT;
    DrawControlBox(_shapes, cellX, cellY, cellWidth, SHEET_ROW_HEIGHT,
                   ControlInkFor(chosen ? ControlState::Committed : ControlState::Outlined, ControlKind::SheetRow, hovered));

    const std::int32_t cellText = CenterTextY(cellY, SHEET_ROW_HEIGHT);
    float cellTextX = cellX + CARD_PADDING;

    // An unclaimed system has no owner colour, so its square is an outline: the same distinction the
    // destination rows have made since ADR-092, in the one shape a grid has room for.
    const float squareY = cellY + (SHEET_ROW_HEIGHT - 8.0F) * 0.5F;
    if (node.owner == NOBODY)
    {
      _shapes.StrokeRect(cellTextX, squareY, 8.0F, 8.0F, WithAlpha(Ink::TEXT_PRIMARY, 128));
    }
    else
    {
      _shapes.FillRect(cellTextX, squareY, 8.0F, 8.0F, OwnerColor(node.owner, m_state.viewer));
    }
    cellTextX += 8.0F + CARD_PADDING;

    const std::string name = Uppercased(node.name);
    _text.DrawText(static_cast<std::int32_t>(cellTextX), cellText, name, Ink::TEXT_PRIMARY, Face::MonoMedium);
    cellTextX += static_cast<float>(FontRenderer::MeasurePixels(name, Face::MonoMedium)) + CARD_PADDING;

    const std::string eta =
      std::format("{} · T{}", target.ticks == 1 ? std::string{"1 TICK"} : std::format("{} TICKS", target.ticks), target.arrivesAt);
    const float etaLeft = cellX + cellWidth - CARD_PADDING - static_cast<float>(FontRenderer::MeasurePixels(eta));
    DrawRight(_text, cellX + cellWidth - CARD_PADDING, cellText, eta, chosen ? Ink::BLUE : Ink::TEXT_MUTED);

    const std::string standing = StandingAt(m_state, target.system);
    if (cellTextX + static_cast<float>(FontRenderer::MeasurePixels(standing)) <= etaLeft - 8.0F)
    {
      _text.DrawText(static_cast<std::int32_t>(cellTextX), cellText, standing, Ink::TEXT_MUTED);
    }

    AddHit(cellX, cellY, cellWidth, SHEET_ROW_HEIGHT, Action::ChooseDestination, target.system);
    m_hoverRegions.push_back(HoverRegion{cellX, cellY, cellWidth, SHEET_ROW_HEIGHT});
  }

  if (notShown > 0)
  {
    const float clippedY = rowY + static_cast<float>(gridRows) * (SHEET_ROW_HEIGHT + BUTTON_GAP) - BUTTON_GAP;
    _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), CenterTextY(clippedY, SHEET_CLIPPED_HEIGHT),
                   std::format("+{} MORE THAN THIS STRIP CAN SHOW", notShown), Ink::NEUTRAL_DIM);
  }

  // ---- Its bar ------------------------------------------------------------------------------------
  //
  // Split in half: the way out on the left, and **the one filled control on the screen** on the
  // right (ADR-089). Before a pick it is inert and says what is missing, because a filled button
  // that refuses a tap is worse than one that is plainly not ready.
  const float barY = y + height - SHEET_ACTION_HEIGHT;
  const float half = width * 0.5F;
  _shapes.FillRect(x, barY, width, 1.0F, Ink::DIVIDER);
  _shapes.FillRect(x + half, barY, 1.0F, SHEET_ACTION_HEIGHT, Ink::DIVIDER);

  DrawCentered(_text, x + half * 0.5F, CenterTextY(barY, SHEET_ACTION_HEIGHT), "CANCEL", Ink::TEXT_MUTED);
  AddHit(x, barY, half, SHEET_ACTION_HEIGHT, Action::CancelMove, 0);

  const ControlState sendState = picked ? ControlState::Primary : ControlState::Inert;
  const bool sendHovered = picked && m_pointerXPixels >= x + half && m_pointerXPixels < x + width && m_pointerYPixels >= barY &&
                           m_pointerYPixels < barY + SHEET_ACTION_HEIGHT;
  const ControlInk sendInk = ControlInkFor(sendState, ControlKind::Button, sendHovered);
  DrawControlBox(_shapes, x + half, barY, half, SHEET_ACTION_HEIGHT, sendInk);
  DrawCentered(_text, x + half * 1.5F, CenterTextY(barY, SHEET_ACTION_HEIGHT, Face::MonoMedium),
               picked ? std::format("SEND {} TO {}", ships, NameOfSystem(m_state, move.selected)) : std::string{"PICK A DESTINATION"},
               sendInk.label, Face::MonoMedium);
  if (picked)
  {
    AddHit(x + half, barY, half, SHEET_ACTION_HEIGHT, Action::SendMove, 0);
    m_hoverRegions.push_back(HoverRegion{x + half, barY, half, SHEET_ACTION_HEIGHT});
  }
}

} // namespace Lockstep
