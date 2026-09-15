// MainPageRail.cpp -- the locks rail: ORDERS, then PLACES, then SIGNALS and PROPOSALS (ADR-112).
//
// What goes in when the clock hits zero, as one list of one row shape, and then the places it is
// about. Every row is a link to what it names (ADR-060) and the one cell that is not a link is the
// `X` that takes an order back. The sections scroll in pixels between a fixed header and footer
// (ADR-101), and a row not wholly inside the band is neither drawn nor a target.

#include "pch.h"
#include "MainPage.h"

#include "DesignTokens.h"
#include "MainPageParts.h"

#include <algorithm>

namespace Lockstep
{

namespace
{

using Neuron::Color;
using Neuron::Face;
using Neuron::FontRenderer;
using Neuron::ShapeRenderer;

/// What a tap does, which the rows composed in here have to name (ADR-112). The page's own enum,
/// aliased rather than qualified thirty times.
using Action = MainPage::Action;

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

/// One row of the locks rail's `ORDERS` list (ADR-112).
///
/// **One shape for every kind of order**, composed before anything is drawn: the column had a
/// section per order KIND, which is the shape of the code that fills it rather than of the question
/// a player is asking. A build and a move are the same row here -- what it is, where, the number,
/// and the `×` -- and the only thing that tells them apart is which array the two indices point
/// into (ADR-057).
struct OrderRow
{
  /// `SHIPYARD L1`, `FLT 1 → FAROE`.
  std::string title;
  /// `DOTHAN`, `10 SHIPS`. Dropped whole when the row is too narrow for it.
  std::string where;
  /// `−20`, `T1`. Empty on a row with nothing to count.
  std::string number;
  Color numberInk = Ink::BLUE;
  /// What the row's body does, and what it names.
  Action open = Action::None;
  std::int32_t openIndex = EventRefs::NONE;
  /// What the `×` does, and what it names. `Action::None` draws no `×` and the body fills the row.
  Action takeBack = Action::None;
  std::int32_t takeBackIndex = EventRefs::NONE;
  /// A fleet with no move: drawn dim behind a dashed square, and still a target, because the point
  /// of the row is that it is the one thing on this column a player can still do something about.
  bool unordered = false;
};

} // namespace

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

  _shapes.FillRect(railX, Frame::TOP_BAR_HEIGHT, Frame::ORDERS_WIDTH, Frame::SCREEN_HEIGHT - Frame::TOP_BAR_HEIGHT, Ink::APP_BACKGROUND);
  _shapes.FillRect(railX, Frame::TOP_BAR_HEIGHT, 1.0F, Frame::SCREEN_HEIGHT - Frame::TOP_BAR_HEIGHT, Ink::CARD_BORDER);

  const bool atLock = m_state.orders.locked && !m_state.match.finished;

  const std::int32_t headerY = static_cast<std::int32_t>(Frame::TOP_BAR_HEIGHT) + 12;
  _text.DrawText(static_cast<std::int32_t>(contentX), headerY,
                 m_state.match.finished ? std::string{"FINAL"} : std::format("ORDERS · T{}", m_state.OrdersTick()), Ink::TEXT_MUTED);

  if (atLock)
  {
    // A filled chip rather than a word (screen 06). LOCKED in muted grey was the same weight as
    // UNLOCKED in amber and read as a label; filled, it reads as a state the rail is IN.
    const auto chipWidth = static_cast<float>(FontRenderer::MeasurePixels("LOCKED")) + 12.0F;
    _shapes.FillRect(contentRight - chipWidth, BandTopForText(headerY, 16.0F), chipWidth, 16.0F, Ink::LOCKED_FILL);
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
  std::string help{"What goes in when the clock hits zero. Tap a row to open the place it is about."};
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

  // ---- The scrollable band ----------------------------------------------------------------------
  //
  // **The rail scrolls now** (ADR-101), because 44-pixel rows made a late-game empire's sections run
  // off the bottom of a column that had never had an answer for it (ADR-086's open question).
  //
  // Everything between the help line and the footer moves; the header and the footer do not, because
  // what they say -- which tick is locking, and that all three columns go in together -- is true
  // however far down the list you are.
  //
  // **A row that is not ENTIRELY inside the band is not drawn at all**, which is coarser than a clip
  // and is what the renderers can actually do: `FontRenderer` has a clip rectangle and
  // `ShapeRenderer` has none, so a half-scrolled row would paint its divider and its hover fill over
  // the help line above. The scroll step is one row, so in the ordinary case nothing is ever half
  // anything; the cull is what keeps that true when a wrapped row is taller than the step.
  const float bandTop = y;
  const float bandBottom = Frame::SCREEN_HEIGHT - 30.0F - (m_railPaged ? RAIL_PAGE_HEIGHT : 0.0F);
  m_railViewportPixels = bandBottom - bandTop;
  m_railScrollPixels = std::clamp(m_railScrollPixels, 0.0F, std::max(0.0F, m_railContentPixels - m_railViewportPixels));
  y -= m_railScrollPixels;

  _text.SetClipRect(railX, bandTop, Frame::ORDERS_WIDTH, m_railViewportPixels);

  /// Whether a box of `_height` starting at the current `y` is wholly inside the band.
  const auto visible = [&](float _height) { return y >= bandTop && y + _height <= bandBottom; };

  // What the page band will say. **Counted while drawing rather than predicted**, because the cull
  // is the only thing that knows what did not fit: a row's height depends on how its title wrapped.
  std::int32_t hiddenBelow = 0;
  std::string sectionBelow;

  /// Records one unit of `_height` at the current `y` as out of sight below, if that is where it is.
  const auto counted = [&](float _height, std::string_view _section)
  {
    if (y + _height > bandBottom)
    {
      ++hiddenBelow;
      if (!_section.empty() && sectionBelow.empty())
      {
        sectionBelow = std::string{_section};
      }
    }
  };

  // A section header is a row in this column, and `SIGNALS` is a control (ADR-039), so it is held to
  // the floor like every other row (ADR-100). The label is centred in it rather than sitting at its
  // top, because the band is now tall enough for that to be visible.
  const auto section = [&](std::string_view _label, std::string_view _count)
  {
    counted(RAIL_SECTION_HEIGHT, _label);
    if (visible(RAIL_SECTION_HEIGHT))
    {
      _shapes.FillRect(railX + 1.0F, y, Frame::ORDERS_WIDTH - 1.0F, 1.0F, Ink::DIVIDER);
      const std::int32_t labelY = CenterTextY(y, RAIL_SECTION_HEIGHT);
      _text.DrawText(static_cast<std::int32_t>(contentX), labelY, _label, Ink::TEXT_MUTED);
      DrawRight(_text, contentRight, labelY, _count, Ink::TEXT_MUTED);
    }
    y += RAIL_SECTION_HEIGHT;
  };

  /// A row: what it is on the left, where it stands on the right. The status carries the colour --
  /// it is the half a player scans down the column for.
  ///
  /// **A row is a link to what it is about** (ADR-060). It gives no order -- the digest is still
  /// the order surface -- it takes the eye to the thing the row names, which is the question a
  /// player reading this column keeps having to answer somewhere else. `Action::None` is a row with
  /// nothing to point at, and it is not a target and draws no hover.
  /// `_dimHead` is how many BYTES at the front of the label are drawn muted (ADR-086): a fleet row
  /// is `FLT 3 · 3` and the id is the half a player is not scanning for. It applies to the first
  /// wrapped line only, which is the only line a head can be on.
  const auto row = [&](std::string_view _label, std::string_view _status, const Color& _statusColor, Action _action, std::int32_t _index,
                       std::size_t _dimHead = 0)
  {
    const auto room = static_cast<std::uint32_t>(contentRight - contentX - static_cast<float>(FontRenderer::MeasurePixels(_status)) - 8.0F);

    const std::vector<std::string> wrapped = FontRenderer::WrapToWidth(_label, room);

    // **A row in a column grows its BOX, not just its hit** (ADR-100). A row drawn at 21 and
    // tappable at 44 has boundaries a finger cannot see, and two neighbours would overlap where
    // neither shows a join.
    const float height =
      std::max(TOUCH_FLOOR, static_cast<float>(std::max<std::size_t>(1, wrapped.size())) * static_cast<float>(LINE_HEIGHT) + 4.0F);
    // **A row nobody can see is a row nobody can tap.** Culling the drawing and leaving the hit
    // would put an invisible control over the help line, which is worse than either.
    counted(height, {});
    if (!visible(height))
    {
      y += height;
      return;
    }

    const bool target = _action != Action::None;

    if (target)
    {
      const bool hovered = m_pointerXPixels >= railX && m_pointerYPixels >= y && m_pointerYPixels < y + height;
      if (hovered)
      {
        _shapes.FillRect(railX + 1.0F, y, Frame::ORDERS_WIDTH - 1.0F, height, Ink::HOVER_FILL);
      }
      AddHit(railX, y, Frame::ORDERS_WIDTH, height, _action, _index);
      m_hoverRegions.push_back(HoverRegion{railX, y, Frame::ORDERS_WIDTH, height});
    }

    // Centred as a block: one line sits in the middle of the row, two sit either side of it, which is
    // the rule a sheet row already follows at this height.
    const std::int32_t lineY = static_cast<std::int32_t>(y + (height - static_cast<float>(wrapped.size() * LINE_HEIGHT)) * 0.5F);

    for (std::size_t index = 0; index < wrapped.size(); ++index)
    {
      const std::int32_t at = lineY + static_cast<std::int32_t>(index) * LINE_HEIGHT;
      if (index == 0 && _dimHead > 0 && _dimHead < wrapped[0].size())
      {
        const std::string_view head{wrapped[0].data(), _dimHead};
        const std::string_view tail{wrapped[0].data() + _dimHead, wrapped[0].size() - _dimHead};
        _text.DrawText(static_cast<std::int32_t>(contentX), at, head, Ink::TEXT_MUTED);
        _text.DrawText(static_cast<std::int32_t>(contentX) + static_cast<std::int32_t>(FontRenderer::MeasurePixels(head)), at, tail,
                       Ink::TEXT_PRIMARY);
        continue;
      }
      _text.DrawText(static_cast<std::int32_t>(contentX), at, wrapped[index], Ink::TEXT_PRIMARY);
    }
    DrawRight(_text, contentRight, CenterTextY(y, height), _status, _statusColor);
    y += height;
  };

  /// One `PLACES` row: a disc in the owner's colour, the name, what it yields and what is standing
  /// on it, and how much of this tick's queue is about it (ADR-112).
  const auto placeRow = [&](std::string_view _name, std::string_view _facts, std::string_view _orders, const Color& _ordersInk,
                            const Color& _disc, Action _action, std::int32_t _index)
  {
    counted(TOUCH_FLOOR, "PLACES");
    if (!visible(TOUCH_FLOOR))
    {
      y += TOUCH_FLOOR;
      return;
    }

    const bool hovered = m_pointerXPixels >= railX && m_pointerYPixels >= y && m_pointerYPixels < y + TOUCH_FLOOR;
    if (hovered)
    {
      _shapes.FillRect(railX + 1.0F, y, Frame::ORDERS_WIDTH - 1.0F, TOUCH_FLOOR, Ink::HOVER_FILL);
    }
    AddHit(railX, y, Frame::ORDERS_WIDTH, TOUCH_FLOOR, _action, _index);
    m_hoverRegions.push_back(HoverRegion{railX, y, Frame::ORDERS_WIDTH, TOUCH_FLOOR});
    _shapes.FillRect(railX + 1.0F, y, Frame::ORDERS_WIDTH - 1.0F, 1.0F, Ink::DIVIDER);

    // A DISC rather than the square an order row wears: on this screen a place is round and a fleet
    // is not, which the map has drawn since ADR-079.
    const std::int32_t textY = CenterTextY(y, TOUCH_FLOOR);
    _shapes.FillEllipse(contentX + PLACE_DISC_SIZE * 0.5F, y + TOUCH_FLOOR * 0.5F, PLACE_DISC_SIZE * 0.5F, PLACE_DISC_SIZE * 0.5F, _disc);

    float textX = contentX + PLACE_DISC_SIZE + 8.0F;
    _text.DrawText(static_cast<std::int32_t>(textX), textY, _name, Ink::TEXT_PRIMARY, Face::MonoMedium);
    textX += static_cast<float>(FontRenderer::MeasurePixels(_name, Face::MonoMedium)) + 8.0F;

    const auto ordersWidth = static_cast<float>(FontRenderer::MeasurePixels(_orders));
    if (!_facts.empty() && static_cast<float>(FontRenderer::MeasurePixels(_facts)) <= contentRight - ordersWidth - 8.0F - textX)
    {
      _text.DrawText(static_cast<std::int32_t>(textX), textY, _facts, Ink::TEXT_MUTED);
    }
    DrawRight(_text, contentRight, textY, _orders, _ordersInk);
    y += TOUCH_FLOOR;
  };

  const auto nothing = [&](std::string_view _text2)
  {
    if (visible(static_cast<float>(LINE_HEIGHT) + 4.0F))
    {
      _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(y), _text2, Ink::NEUTRAL_DIM);
    }
    y += static_cast<float>(LINE_HEIGHT) + 4.0F;
  };

  // **Every row below is focus-only once the orders are locked or the match is over**, matching
  // every other control on the screen: a tap still moves the eye, and nothing opens a sheet that
  // could take an order for a tick that is already resolving (ADR-060, screen 06).
  const bool navigateOnly = m_state.orders.locked || m_state.match.finished;

  // ---- ORDERS ------------------------------------------------------------------------------------
  //
  // **One row shape for every kind of order** (ADR-112). This column had a section per ORDER KIND --
  // `FLEETS` and `BUILDS` -- which is the shape of the code that composes it rather than the shape
  // of the question a player is asking. The question is *what goes in when the clock hits zero*, and
  // the answer is a list: an owner square, what it is, where it is, the number it costs or the tick
  // it lands on, and the `×` that takes it back.
  //
  // Row order is what the lock will take, then what an earlier one already did, then what has not
  // been ordered at all. The last of those is the row this section exists to make visible: a fleet
  // with nothing to do is invisible on every other surface of this screen.
  std::vector<OrderRow> orders;

  // **Focus-only at the lock, exactly as every row on this rail has been** (ADR-060): a tap still
  // moves the eye, and nothing opens a surface that could take an order for a tick that is already
  // resolving.
  const Action linkAction = navigateOnly ? Action::FocusSystem : Action::OpenSystem;

  const auto placeOf = [&](std::int32_t _systemId) { return PositionOfSystem(m_state, _systemId); };

  for (const std::int32_t queued : m_state.orders.queuedBuilds)
  {
    if (queued < 0 || queued >= static_cast<std::int32_t>(m_state.orders.builds.size()))
    {
      continue;
    }
    const BuildRow& build = m_state.orders.builds[static_cast<std::size_t>(queued)];
    const std::int32_t at = placeOf(build.system);
    orders.push_back(OrderRow{.title = Uppercased(std::format("{} L{}", build.building, build.level)),
                              .where = NameOfSystem(m_state, at),
                              .number = std::format("−{}", build.cost),
                              .numberInk = Ink::BLUE,
                              .open = at == EventRefs::NONE ? Action::None : linkAction,
                              .openIndex = at,
                              .takeBack = navigateOnly ? Action::None : Action::ToggleBuild,
                              .takeBackIndex = queued});
  }

  for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
  {
    const Fleet& fleet = m_state.fleets[index];
    if (fleet.owner != m_state.viewer || fleet.underWay || !fleet.OnALane())
    {
      continue;
    }
    orders.push_back(OrderRow{.title = std::format("{} → {}", Uppercased(fleet.name), NameOfSystem(m_state, fleet.to)),
                              .where = fleet.ships == 1 ? std::string{"1 SHIP"} : std::format("{} SHIPS", fleet.ships),
                              .number = std::format("T{}", fleet.eta),
                              .numberInk = Ink::BLUE,
                              .open = linkAction,
                              .openIndex = fleet.from,
                              .takeBack = navigateOnly ? Action::None : Action::CancelFleetOrder,
                              .takeBackIndex = static_cast<std::int32_t>(index)});
  }

  // **What an earlier lock already took, and there is no `×` on it** (ADR-069, ADR-070). The rail is
  // the receipt of everything this player has committed to, and a build in flight is exactly that --
  // it is simply not a thing this lock will take, and not a thing any tap can take back.
  for (const BuildRow& build : m_state.orders.builds)
  {
    if (!build.rising)
    {
      continue;
    }
    const std::int32_t at = placeOf(build.system);
    orders.push_back(OrderRow{.title = Uppercased(std::format("{} L{}", build.building, build.level)),
                              .where = NameOfSystem(m_state, at),
                              .number = std::format("T{}", build.completesAt),
                              .numberInk = Ink::TEXT_MUTED,
                              .open = at == EventRefs::NONE ? Action::None : linkAction,
                              .openIndex = at});
  }

  for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
  {
    const Fleet& fleet = m_state.fleets[index];
    if (fleet.owner != m_state.viewer || !fleet.underWay)
    {
      continue;
    }
    orders.push_back(OrderRow{.title = std::format("{} → {}", Uppercased(fleet.name), NameOfSystem(m_state, fleet.to)),
                              .where = fleet.ships == 1 ? std::string{"1 SHIP"} : std::format("{} SHIPS", fleet.ships),
                              .number = std::format("T{}", fleet.eta),
                              .numberInk = Ink::TEXT_MUTED,
                              .open = Action::FocusSystem,
                              .openIndex = fleet.to});
  }

  // **A fleet with no move is a row, dim and unordered** (ADR-112). It is the one thing this column
  // never said: a player reading a list of what goes in at the lock had no way to see what does not.
  for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
  {
    const Fleet& fleet = m_state.fleets[index];
    if (fleet.owner != m_state.viewer || fleet.underWay || fleet.OnALane())
    {
      continue;
    }
    orders.push_back(OrderRow{.title = Uppercased(fleet.name),
                              .where = std::format("NO MOVE · {}", NameOfSystem(m_state, fleet.from)),
                              .open = navigateOnly ? Action::FocusSystem : Action::BeginMove,
                              .openIndex = navigateOnly ? fleet.from : static_cast<std::int32_t>(index),
                              .unordered = true});
  }

  /// One ORDERS row. The `×` is its own 44-pixel cell at the right end and the rest of the row is
  /// the link; a row with nothing to take back has no cell and the link fills it (ADR-112).
  const auto orderRow = [&](const OrderRow& _order)
  {
    counted(TOUCH_FLOOR, "ORDERS");
    if (!visible(TOUCH_FLOOR))
    {
      y += TOUCH_FLOOR;
      return;
    }

    const float takeBackLeft = railX + Frame::ORDERS_WIDTH - TOUCH_FLOOR;
    const bool takeable = _order.takeBack != Action::None;
    const float bodyWidth = takeable ? takeBackLeft - railX : Frame::ORDERS_WIDTH;

    if (_order.open != Action::None)
    {
      const bool hovered =
        m_pointerXPixels >= railX && m_pointerXPixels < railX + bodyWidth && m_pointerYPixels >= y && m_pointerYPixels < y + TOUCH_FLOOR;
      if (hovered)
      {
        _shapes.FillRect(railX + 1.0F, y, bodyWidth - 1.0F, TOUCH_FLOOR, Ink::HOVER_FILL);
      }
      AddHit(railX, y, bodyWidth, TOUCH_FLOOR, _order.open, _order.openIndex);
      m_hoverRegions.push_back(HoverRegion{railX, y, bodyWidth, TOUCH_FLOOR});
    }
    _shapes.FillRect(railX + 1.0F, y, Frame::ORDERS_WIDTH - 1.0F, 1.0F, Ink::DIVIDER);

    // An 8px square in the viewer's colour, DASHED when there is no order on the row: the one
    // marker on this screen that says *nothing has been given* (ADR-110's dashed rule, applied to a
    // marker rather than to a control's border -- the row is still a target).
    const std::int32_t textY = CenterTextY(y, TOUCH_FLOOR);
    const float squareY = y + (TOUCH_FLOOR - 8.0F) * 0.5F;
    if (_order.unordered)
    {
      _shapes.DashedLine(contentX, squareY + 0.5F, contentX + 8.0F, squareY + 0.5F, Ink::NEUTRAL_DIM, 1.0F, INERT_DASH, INERT_GAP);
      _shapes.DashedLine(contentX, squareY + 7.5F, contentX + 8.0F, squareY + 7.5F, Ink::NEUTRAL_DIM, 1.0F, INERT_DASH, INERT_GAP);
      _shapes.DashedLine(contentX + 0.5F, squareY, contentX + 0.5F, squareY + 8.0F, Ink::NEUTRAL_DIM, 1.0F, INERT_DASH, INERT_GAP);
      _shapes.DashedLine(contentX + 7.5F, squareY, contentX + 7.5F, squareY + 8.0F, Ink::NEUTRAL_DIM, 1.0F, INERT_DASH, INERT_GAP);
    }
    else
    {
      _shapes.FillRect(contentX, squareY, 8.0F, 8.0F, OwnerColor(m_state.viewer, m_state.viewer));
    }

    const float right = takeBackLeft - 4.0F;
    const auto numberWidth = static_cast<float>(FontRenderer::MeasurePixels(_order.number));
    if (!_order.number.empty())
    {
      DrawRight(_text, right, textY, _order.number, _order.unordered ? Ink::NEUTRAL_DIM : _order.numberInk);
    }

    float textX = contentX + 16.0F;
    _text.DrawText(static_cast<std::int32_t>(textX), textY, _order.title, _order.unordered ? Ink::NEUTRAL_DIM : Ink::TEXT_PRIMARY,
                   Face::MonoMedium);
    textX += static_cast<float>(FontRenderer::MeasurePixels(_order.title, Face::MonoMedium)) + 8.0F;

    // **The place is dropped whole when it does not fit**, the way the top bar drops a clause: the
    // title is what the row IS and half a system name beside a price is worse than none of it.
    const float room = right - (numberWidth > 0.0F ? numberWidth + 8.0F : 0.0F) - textX;
    if (!_order.where.empty() && static_cast<float>(FontRenderer::MeasurePixels(_order.where)) <= room)
    {
      _text.DrawText(static_cast<std::int32_t>(textX), textY, _order.where, _order.unordered ? Ink::NEUTRAL_DIM : Ink::TEXT_MUTED);
    }

    if (takeable)
    {
      // **`X` and not `×`.** The multiplication sign the handoff draws is not in the baked alphabet
      // (ADR-014's list, ADR-073's bake), and `FontRenderer::GlyphOf` falls back to a BLANK for a
      // codepoint it does not know -- so the cell would have drawn nothing at all. Adding a glyph
      // is a re-bake and a font decision; the sheet's close corner already says `X`.
      DrawCentered(_text, takeBackLeft + TOUCH_FLOOR * 0.5F, textY, "X", Ink::OUTLINE_HOVER);
      AddHit(takeBackLeft, y, TOUCH_FLOOR, TOUCH_FLOOR, _order.takeBack, _order.takeBackIndex);
    }
    y += TOUCH_FLOOR;
  };

  for (const OrderRow& order : orders)
  {
    orderRow(order);
  }
  if (orders.empty())
  {
    nothing("- nothing queued -");
  }

  // What the queue leaves, on the line under it (ADR-053).
  if (!m_state.orders.queuedBuilds.empty())
  {
    const std::uint32_t spent = m_state.orders.QueuedBuildCost();
    nothing(std::format("{} CR LEFT AT THE LOCK", spent <= m_state.player.credits ? m_state.player.credits - spent : 0));
  }

  // ---- PLACES -------------------------------------------------------------------------------------
  //
  // **One row per system you hold, and it replaces both of the sections above** (ADR-112). A place
  // is the subject of every order on this screen now (ADR-111), so the rail's second list is the
  // list of places rather than a second list of orders: what each one yields, what is standing on
  // it, and how much of the tick's queue is about it.
  std::vector<std::int32_t> held;
  for (std::size_t index = 0; index < m_state.graph.systems.size(); ++index)
  {
    const SystemNode& node = m_state.graph.systems[index];
    if (node.owner == m_state.viewer && !HasFlag(node.flags, SystemFlags::RegionAnchor))
    {
      held.push_back(static_cast<std::int32_t>(index));
    }
  }
  section("PLACES", std::format("{} HELD", held.size()));

  if (held.empty())
  {
    nothing("- none -");
  }

  for (const std::int32_t at : held)
  {
    const SystemNode& node = m_state.graph.systems[static_cast<std::size_t>(at)];

    std::uint32_t ships = 0;
    for (const Fleet& fleet : m_state.fleets)
    {
      ships += fleet.owner == m_state.viewer && !fleet.underWay && fleet.from == at ? fleet.ships : 0U;
    }

    std::uint32_t ordered = 0;
    for (const std::int32_t queued : m_state.orders.queuedBuilds)
    {
      const bool known = queued >= 0 && queued < static_cast<std::int32_t>(m_state.orders.builds.size());
      ordered += known && m_state.orders.builds[static_cast<std::size_t>(queued)].system == node.id ? 1U : 0U;
    }
    for (const Fleet& fleet : m_state.fleets)
    {
      ordered += fleet.owner == m_state.viewer && !fleet.underWay && fleet.OnALane() && fleet.from == at ? 1U : 0U;
    }

    std::string facts;
    if (node.production != 0)
    {
      facts = std::format("+{}", node.production);
    }
    if (ships != 0)
    {
      facts += facts.empty() ? std::string{} : " · ";
      facts += ships == 1 ? std::string{"1 SHIP"} : std::format("{} SHIPS", ships);
    }

    // **The en dash, which ADR-014 baked and nothing had a site for** until a place with no order on
    // it needed a mark rather than a blank. The em dash the handoff draws is not in the alphabet.
    placeRow(Uppercased(node.name), facts, ordered == 0 ? std::string{"–"} : std::format("{} ORDER{}", ordered, ordered == 1 ? "" : "S"),
             ordered == 0 ? Ink::NEUTRAL_DIM : Ink::BLUE, OwnerColor(node.owner, m_state.viewer),
             navigateOnly ? Action::FocusSystem : Action::OpenSystem, at);
  }

  // ---- SIGNALS -----------------------------------------------------------------------------------
  //
  // What is going OUT this tick. This rail said "- none sent -" for as long as it existed, because
  // the client had no model of an offer leaving; ADR-039 gave it one, and the count on the right is
  // the way in -- it is the only section header on this rail that is a control.
  // **Asked BEFORE the section advances `y`, and it is the reason this is not inside `section`:**
  // the hit belongs to a header that is a control, and the header's own lambda knows nothing about
  // actions. Culled with the row it is about, or a scrolled-away header leaves an invisible control
  // sitting over the help line (ADR-101).
  const std::int32_t signalsY = static_cast<std::int32_t>(y);
  const bool signalsVisible = visible(RAIL_SECTION_HEIGHT);
  section("SIGNALS",
          !OrdersEditable() ? std::string{m_offline ? "OFFLINE" : "LOCKED"} : std::format("{} TO SEND ›", m_state.orders.availableSignals));
  if (OrdersEditable() && signalsVisible)
  {
    AddHit(railX, static_cast<float>(signalsY), Frame::ORDERS_WIDTH, RAIL_SECTION_HEIGHT, Action::OpenSignals, 0);
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
  // ---- What the band came to ---------------------------------------------------------------------
  //
  // Measured rather than predicted, and read by the next frame's clamp: the content depends on how
  // many fleets, builds, signals and offers this tick happens to carry, and only the draw knows.
  m_railContentPixels = y + m_railScrollPixels - bandTop;
  _text.ClearClipRect();

  m_railPaged = m_railContentPixels > m_railViewportPixels + 1.0F;

  // ---- The rail's page band ------------------------------------------------------------------------
  //
  // **The same band the digest column has, in the same place, saying the same kind of thing**
  // (ADR-080, ADR-101). A wheel notch scrolls this column, but a wheel is not a finger and this
  // game is for touch (ADR-098): a column whose only scroll affordance is a mouse gesture is a
  // column half this game's players cannot reach the bottom of. So the band is two 44-pixel targets
  // and the wheel is the shortcut, rather than the other way round.
  //
  // It says WHAT is below and not only that something is (ADR-080's whole point): `3 MORE ·
  // SIGNALS ›` tells a player whether the thing they are looking for is down there. `‹ UP` appears
  // only once there is something above, so the band never offers a direction that does nothing.
  if (m_railPaged)
  {
    const float pageY = Frame::SCREEN_HEIGHT - 30.0F - RAIL_PAGE_HEIGHT;
    const std::int32_t pageText = CenterTextY(pageY, RAIL_PAGE_HEIGHT);
    _shapes.FillRect(railX + 1.0F, pageY, Frame::ORDERS_WIDTH - 1.0F, 1.0F, Ink::DIVIDER);

    if (m_railScrollPixels > 0.0F)
    {
      _text.DrawText(static_cast<std::int32_t>(contentX), pageText, "‹ UP", Ink::TEXT_MUTED);
      AddHit(railX, pageY, Frame::ORDERS_WIDTH * 0.5F, RAIL_PAGE_HEIGHT, Action::PageRail, -1);
    }

    const std::string below = hiddenBelow == 0       ? std::string{"END"}
                              : sectionBelow.empty() ? std::format("{} MORE ›", hiddenBelow)
                                                     : std::format("{} MORE · {} ›", hiddenBelow, sectionBelow);
    DrawRight(_text, contentRight, pageText, below, hiddenBelow == 0 ? Ink::NEUTRAL_DIM : Ink::TEXT_MUTED);
    if (hiddenBelow != 0)
    {
      AddHit(railX + Frame::ORDERS_WIDTH * 0.5F, pageY, Frame::ORDERS_WIDTH * 0.5F, RAIL_PAGE_HEIGHT, Action::PageRail, 1);
    }
  }

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

bool MainPage::ScrollRail(float _pixels)
{
  if (_pixels == 0.0F)
  {
    return false;
  }

  // **Clamped against what the last frame measured**, which is a frame old exactly as the hit list
  // is, and for the same reason: layout and what reads it are the same code run once.
  const float furthest = std::max(0.0F, m_railContentPixels - m_railViewportPixels);
  const float was = m_railScrollPixels;
  m_railScrollPixels = std::clamp(m_railScrollPixels + _pixels, 0.0F, furthest);
  return m_railScrollPixels != was;
}

} // namespace Lockstep
