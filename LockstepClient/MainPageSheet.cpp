// MainPageSheet.cpp -- the sheets: the place sheet's tiles and fleets, the signal picker, the replay.
//
// **A sheet is about a PLACE and it is the only door an order goes through** (ADR-111). It holds what
// the system can build as a grid of tiles (ADR-107) and the fleets standing on it, its body scrolls
// by blocks, and it may take half the map pane and no more (ADR-052). The other two sheets are
// columns of rows in the same frame.

#include "pch.h"
#include "MainPage.h"

#include "Controls.h"
#include "DesignTokens.h"
#include "MainPageParts.h"

#include <algorithm>
#include <array>
#include <utility>

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

// ---- The build sheet's tiles (ADR-107) ---------------------------------------------------------

/// What a build tile IS, which is the whole of how it is drawn.
///
/// **One enumerator per row of ADR-107's state table**, so that the seven treatments are chosen in
/// one place and applied in another. The lock is not one of them: it dims whatever state a tile is
/// already in and takes its target away, exactly as it dims a sheet row (ADR-065), and folding it
/// in here would be seven more enumerators saying the same thing.
enum class TileState : std::uint8_t
{
  /// Orderable, and the purse covers it.
  Available,
  /// Queued this tick. A tap takes it back (ADR-053).
  Queued,
  /// Orderable in principle and not against this purse, once the queue has had its share (ADR-078).
  BeyondThePurse,
  /// Paid for at an earlier lock and on its way (ADR-070).
  Rising,
  /// Something else on this system is rising, so the lock would refuse this one (ADR-069).
  Blocked,
  /// The building is at its top level, so there is no next one to buy. Also what a tile that is at
  /// the top of its ladder and a tile that has simply gone inert have in common, which is why they
  /// share an ink.
  TopLevel,
  /// A trade lane: a building with two owners, so it is proposed rather than built.
  Propose,
};

/// Which of the four control states one of a tile's seven falls into (ADR-107, ADR-110).
///
/// **Seven states and four treatments, and that is the point of the table rather than a loss.** The
/// seven say WHY -- rising is not queued and blocked is not dear -- and the tile says which in its
/// bottom line; the four say what the player can do about it, which is the one thing the chrome has
/// to carry. A tile is never `Primary`: the filled control is one per screen and it is not a tile
/// (ADR-089).
[[nodiscard]] ControlState StateOfTile(TileState _state) noexcept
{
  switch (_state)
  {
  case TileState::Queued:
  case TileState::Rising:
    // The one treatment two states share, and they share it because they are the same statement a
    // lock apart: this is yours and it is paid for.
    return ControlState::Committed;
  case TileState::BeyondThePurse:
  case TileState::Blocked:
  case TileState::TopLevel:
    return ControlState::Inert;
  case TileState::Propose:
  case TileState::Available:
  default:
    return ControlState::Outlined;
  }
}

/// The inks one tile is drawn in: the control vocabulary's chrome and bottom line, and the three
/// above it that are the tile's own.
struct TileInk
{
  /// The border, the fill, and the two ends of the bottom line (`label` and `number`).
  ControlInk control;
  /// The icon, and the ladder square for the level this tile buys.
  Color accent;
  Color title;
  Color detail;
};

[[nodiscard]] TileInk InkFor(TileState _state, bool _moneyReason = false) noexcept
{
  ControlInk control = ControlInkFor(StateOfTile(_state), ControlKind::Tile, false, _moneyReason);
  switch (_state)
  {
  case TileState::Queued:
  case TileState::Rising:
    // The title and the detail stay at full strength on a committed tile: it is the bottom line
    // that says the state, and a blue title would say it twice.
    return TileInk{.control = control, .accent = Ink::BLUE, .title = Ink::TEXT_PRIMARY, .detail = Ink::TEXT_DETAIL};
  case TileState::BeyondThePurse:
  case TileState::Blocked:
  case TileState::TopLevel:
    return TileInk{.control = control, .accent = Ink::NEUTRAL_DIM, .title = Ink::NEUTRAL_DIM, .detail = Ink::NEUTRAL_DIM};
  case TileState::Propose:
    // Amber, which on this screen is what needs somebody else (DESIGN-GUIDELINES "Semantic"): a
    // lane is the one building the player cannot finish alone. It reaches the bottom line as well
    // as the icon, because the price on it is a price somebody else has to agree to.
    control.label = Ink::AMBER;
    control.number = Ink::AMBER;
    return TileInk{.control = control, .accent = Ink::AMBER, .title = Ink::TEXT_PRIMARY, .detail = Ink::TEXT_DETAIL};
  case TileState::Available:
  default:
    return TileInk{.control = control, .accent = Ink::TEXT_PRIMARY, .title = Ink::TEXT_PRIMARY, .detail = Ink::TEXT_DETAIL};
  }
}

/// One tile of the build grid, composed before anything is drawn.
///
/// **Measured and then drawn, like a digest card** (`CardLayout`): a sheet's height is its header,
/// its help line, its body and its `CANCEL` bar, and the body is only knowable once the tiles are.
struct BuildTile
{
  /// `Shipyard L1 → L2`, and the SYSTEM IS NOT IN IT -- the sheet's header already said it.
  std::string title;
  /// What the level pays and how long it takes, from the snapshot's tables (ADR-070).
  std::string detail;
  /// The bottom line's two ends: the price or the progress, and the note beside it.
  std::string state;
  std::string note;

  /// 0 shipyard, 1 mining station, 2 bastion (`BuildRow::kind`); a lane is not a kind of building.
  std::uint8_t kind = 0;
  bool lane = false;

  /// How many levels this system already holds of this building, and the one this tile buys. The
  /// ladder is drawn from the pair, so a tile never has to say `L2 → L3` twice.
  std::uint32_t held = 0;
  std::uint32_t buys = 0;
  /// Whose agreement a lane is waiting on, drawn where every other tile draws its ladder.
  std::string partner;

  /// How much of a rising build is in, from 0 to 1, or a negative for a tile that is not rising.
  /// **Zero is a real answer** -- a build ordered at the last lock -- so the absence cannot be it.
  float progress = -1.0F;

  TileInk ink;
  /// Whether the bottom line's left end is set in the Medium cut. It is on every tile whose left
  /// end is a number the player is deciding by, and not on one that is only reporting.
  bool stateIsMedium = true;
  /// Whether an inert tile's reason is money, which is the one an amber note is for (ADR-110).
  bool moneyReason = false;
  /// The build row this tile queues or unqueues, or `EventRefs::NONE` for one that is only read.
  std::int32_t target = EventRefs::NONE;
};

/// One of the four glyphs, 22x22 at `(_xPixels, _yPixels)`, in the tile's state colour.
///
/// **Built from the map's own shapes and never from a bitmap** (R13, ADR-014): the fleet dart for a
/// shipyard, the diamond-section column for a mining station, a hexagon for a bastion, a dashed lane
/// between two owner squares for a trade lane. Tinted by STATE and never by owner -- a build sheet
/// is about one system and every building on it is the viewer's, so an owner colour here would be a
/// third channel saying nothing (ADR-027).
void DrawBuildIcon(ShapeRenderer& _shapes, std::uint8_t _kind, bool _lane, float _xPixels, float _yPixels, const Color& _color)
{
  /// The stroke every outlined glyph is drawn at. One and a half rather than one, because at 22
  /// pixels a hairline hexagon beside a filled dart reads as two weights of the same family.
  constexpr float STROKE = 1.5F;
  const auto at = [_xPixels, _yPixels](float _x, float _y) noexcept { return ShapeRenderer::ShapePoint{_xPixels + _x, _yPixels + _y}; };

  if (_lane)
  {
    _shapes.DashedLine(_xPixels + 5.0F, _yPixels + 11.0F, _xPixels + 17.0F, _yPixels + 11.0F, _color, STROKE, 3.0F, 2.0F);
    // The proposer's end is filled and the partner's is an outline: the lane is half agreed.
    _shapes.FillRect(_xPixels + 1.0F, _yPixels + 8.0F, 6.0F, 6.0F, _color);
    _shapes.StrokeRect(_xPixels + 15.0F, _yPixels + 8.0F, 6.0F, 6.0F, _color, STROKE);
    return;
  }

  switch (_kind)
  {
  case 1:
  {
    // The mining station: the map's diamond-section column seen end on, with its core.
    const std::array<ShapeRenderer::ShapePoint, 4> diamond = {at(11.0F, 2.0F), at(20.0F, 11.0F), at(11.0F, 20.0F), at(2.0F, 11.0F)};
    _shapes.StrokePolygon(diamond, _color, STROKE);
    _shapes.FillEllipse(_xPixels + 11.0F, _yPixels + 11.0F, 2.5F, 2.5F, _color);
    break;
  }
  case 2:
  {
    // The bastion: a hexagon, which is the one silhouette on this sheet that is neither a ship nor
    // a mine. Nothing emits a bastion row yet (blueprint §3).
    const std::array<ShapeRenderer::ShapePoint, 6> hexagon = {at(11.0F, 2.0F),  at(19.0F, 6.0F), at(19.0F, 12.0F),
                                                              at(11.0F, 20.0F), at(3.0F, 12.0F), at(3.0F, 6.0F)};
    _shapes.StrokePolygon(hexagon, _color, STROKE);
    _shapes.Line(_xPixels + 11.0F, _yPixels + 7.0F, _xPixels + 11.0F, _yPixels + 14.0F, _color, STROKE);
    break;
  }
  case 0:
  default:
    // The shipyard: the arrowhead the map draws a fleet as (ADR-090), so the thing that makes ships
    // and the thing it makes are the same shape.
    _shapes.FillTriangle(_xPixels + 3.0F, _yPixels + 4.0F, _xPixels + 19.0F, _yPixels + 11.0F, _xPixels + 7.0F, _yPixels + 11.0F, _color);
    _shapes.FillTriangle(_xPixels + 7.0F, _yPixels + 11.0F, _xPixels + 19.0F, _yPixels + 11.0F, _xPixels + 3.0F, _yPixels + 18.0F, _color);
    break;
  }
}

/// One unit of the place sheet's body, which is what that body scrolls by (ADR-111).
enum class BlockKind : std::uint8_t
{
  BuildBand,
  /// The one case the grid does not cover: a system with every building at its top level.
  NothingToBuild,
  TileRow,
  Divider,
  FleetBand,
  FleetRow
};

struct Block
{
  BlockKind kind;
  /// Which tile row, or which fleet row. Unused by the bands and the divider.
  std::size_t index;
  /// Including whatever gap sits ABOVE it, so a running sum is the body's height.
  float height;
};

/// Which slot of the 2x2 grid a tile belongs in: mining station, shipyard, bastion, trade lane.
///
/// **Fixed by ROLE and not by the order the snapshot composed the rows in** (ADR-107), so the tile a
/// thumb reaches for is in the same corner of every system's sheet. Economy first, because it is
/// what a player buys most of and the grid is read top-left first.
[[nodiscard]] std::size_t TileSlotOf(const BuildTile& _tile) noexcept
{
  if (_tile.lane)
  {
    return 3;
  }
  return _tile.kind == 1 ? 0 : (_tile.kind == 0 ? 1 : 2);
}

} // namespace

void MainPage::ReopenPanel(Panel _panel, std::int32_t _subjectId, std::int32_t _subject)
{
  switch (_panel)
  {
  case Panel::Place:
  {
    // Still yours. `Action::OpenSystem` applies the same rule (ADR-058) and this is the same
    // question asked a tick later.
    const std::int32_t at = PositionOfSystem(m_state, _subjectId);
    if (at == EventRefs::NONE || m_state.graph.systems[static_cast<std::size_t>(at)].owner != m_state.viewer)
    {
      return;
    }
    m_panel = Panel::Place;
    m_panelSubject = at;
    m_panelSubjectId = _subjectId;
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

void MainPage::OpenPlace(std::int32_t _system)
{
  // **A sheet and the move mode are alternatives, not layers** (ADR-113). The mode is played on the
  // map and a sheet covers it, so opening one is leaving the other -- the same trade `EnterMove`
  // makes in the other direction when it collapses the sheet it was entered from.
  ExitMove();

  const bool known = _system >= 0 && _system < static_cast<std::int32_t>(m_state.graph.systems.size());
  m_panel = known ? Panel::Place : Panel::None;
  m_panelSubject = known ? _system : EventRefs::NONE;
  m_panelSubjectId = known ? m_state.graph.systems[static_cast<std::size_t>(_system)].id : EventRefs::NONE;
  m_sheetScroll = 0;
  m_sheetDragPixels = 0.0F;
}

bool MainPage::ScrollSheet(std::int32_t _blocks)
{
  if (m_panel != Panel::Place || _blocks == 0)
  {
    return false;
  }
  // Clamped against the last frame's measurement, which is the only thing that knows how many
  // blocks the body came to and how many of them fit (ADR-111). A frame old, like every other hit
  // test on this screen.
  const auto top = static_cast<std::int32_t>(m_sheetScroll) + _blocks;
  const auto most = static_cast<std::int32_t>(m_sheetBlocks > m_sheetBlocksShown ? m_sheetBlocks - m_sheetBlocksShown : 0);
  const auto wanted = static_cast<std::size_t>(std::clamp(top, 0, most));
  if (wanted == m_sheetScroll)
  {
    return false;
  }
  m_sheetScroll = wanted;
  return true;
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
///
/// **There is one sheet about a PLACE now, and it is the only door an order goes through**
/// (ADR-111). It holds what the system can build and the fleets standing on it, where there used
/// to be a build sheet reached from the map and a fleet list reached from a badge. The other three
/// sheets -- the destination picker, the signal picker and the replay stub -- are unchanged columns
/// of rows, and the frame around all four is the same frame.
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
    /// What this row sorts by, where its sheet sorts at all. The destination picker's is the lane
    /// cost, so the nearest destination is first (ADR-092); every other sheet leaves it at zero and
    /// keeps the order it composed rows in.
    std::uint32_t sortBy = 0;
    /// The second line's ink. Muted unless the line is ABOUT somebody -- a destination's garrison is
    /// the rival who is standing there, and their colour is what says which rival (ADR-092).
    ///
    /// **Last in the aggregate, and that is load-bearing.** Most rows on most sheets are built with
    /// positional braces, so a field inserted in the middle of this silently rebinds every one of
    /// them -- which it did, and the compiler caught it only because a `std::int32_t` target will
    /// not narrow into a `Color`.
    Color detailInk = Ink::TEXT_MUTED;
  };

  /// One of the viewer's fleets on the place this sheet is about (ADR-111).
  ///
  /// **A boxed row rather than a divided one**, because it is the one row on this sheet that
  /// carries a control: the box is what says the button inside it belongs to this fleet and not to
  /// the fleet under it.
  struct PlaceFleet
  {
    std::string name;
    /// `10 SHIPS - HOLDING`, or `10 SHIPS > FAROE - T1` once a move is queued.
    std::string detail;
    Button button;
    Action action = Action::None;
    std::int32_t target = EventRefs::NONE;
  };

  constexpr Color NO_ACCENT = {0, 0, 0, 0};

  std::vector<SheetRow> rows;

  /// Rows drawn BELOW the capped list and outside the count, immediately above `CANCEL`. Only the
  /// concede uses it (ADR-093): it is the one control on this screen that must always be reachable,
  /// and every other row on every sheet is equal, so the first six win.
  std::vector<SheetRow> pinned;

  /// The place sheet's two bodies, which are a grid and a short list rather than a column of rows
  /// (ADR-107, ADR-111). Exactly one of these and `rows` is filled.
  std::vector<BuildTile> tiles;
  std::vector<PlaceFleet> fleets;

  std::string title;
  /// The place sheet's header: the disc in the owner's colour, and the muted clause beside the name
  /// that says what the system IS -- `YOURS - +6 A TICK - CAPITAL`.
  Color headerDisc = NO_ACCENT;
  std::string headerFacts;
  /// The two section bands' right-hand labels: what can be started here, and what is standing here.
  std::string buildCount;
  std::string fleetCount;

  /// What the bottom bar says. `DONE` on the sheet a player has been giving orders in, because
  /// closing it is finishing rather than backing out; `CANCEL` on the three that are pickers.
  const char* barLabel = "CANCEL";

  /// Whether something on this sheet is dim for want of credits, which is what decides whether the
  /// purse sentence above it is a warning or a note (ADR-078).
  bool shortOfCredits = false;

  /// What a build sheet's header and help line need about the system they are on: the tick a build
  /// already rising there lands on -- zero when none is -- and the sentence that says so
  /// (ADR-070, ADR-107). Composed with the tiles, because that is the pass that reads the rows.
  std::uint32_t sheetRisingLandsAt = 0;
  std::string risingHelp;

  // What tapping a row does. It differs per panel, and it used to not exist: every row went to one
  // action, so the build panel listed two things a player could not tap.
  Action rowAction = Action::None;

  switch (m_panel)
  {
  case Panel::Place:
  {
    if (m_panelSubject < 0 || m_panelSubject >= static_cast<std::int32_t>(m_state.graph.systems.size()))
    {
      return;
    }
    const SystemNode& node = m_state.graph.systems[static_cast<std::size_t>(m_panelSubject)];
    title = Uppercased(node.name);
    headerDisc = OwnerColor(node.owner, m_state.viewer);
    barLabel = "DONE";
    rowAction = Action::ToggleBuild;

    // **What the place IS, in the words the rest of the screen uses** (ADR-111). It is the one
    // thing a sheet titled with a name cannot say for itself, and it is what the digest's `MAP`
    // chip and the rail's `PLACES` row were the only places to read.
    headerFacts = "YOURS";
    if (node.production != 0)
    {
      headerFacts += std::format(" · +{} A TICK", node.production);
    }
    if (HasFlag(node.flags, SystemFlags::Capital))
    {
      headerFacts += " · CAPITAL";
    }
    if (HasFlag(node.flags, SystemFlags::Contested))
    {
      headerFacts += " · CONTESTED";
    }

    // **This system's buildings and nobody else's** (ADR-058). `Orders::builds` is the whole
    // empire's list -- the rail counts it, and the digest offers from it -- so drawing it whole
    // under a title naming ONE system offered `Shipyard - Pell` on the sheet for Dothan.
    //
    // `BuildRow::system` is a system id and `m_panelSubject` is a position in the view's own list,
    // which are different numbers for the same system (ADR-057). The node carries both.
    //
    // **Two passes, because a blocked tile has to know what is blocking it.** A system that is
    // building draws the tiles for what it could build next beside the one that is rising -- inert,
    // priced, and each saying the tick it becomes orderable on, which is the rising build's
    // (ADR-107). The player is planning, not choosing, and a sheet with one tile on it had nothing
    // to plan against.
    for (const BuildRow& row : m_state.orders.builds)
    {
      if (row.system != node.id || !row.rising)
      {
        continue;
      }
      sheetRisingLandsAt = row.completesAt;

      const std::uint32_t orderedAt = row.completesAt > row.ticks ? row.completesAt - row.ticks : 0;
      const std::uint32_t done = m_state.match.tick > orderedAt ? m_state.match.tick - orderedAt : 0;
      risingHelp = RisingSentence(node.name, done, row.ticks);
    }

    std::uint32_t startable = 0;
    for (std::size_t index = 0; index < m_state.orders.builds.size(); ++index)
    {
      const BuildRow& row = m_state.orders.builds[index];
      if (row.system != node.id)
      {
        continue;
      }
      startable += row.available && !row.rising ? 1U : 0U;

      const bool queued =
        std::ranges::find(m_state.orders.queuedBuilds, static_cast<std::int32_t>(index)) != m_state.orders.queuedBuilds.end();
      const bool affordable = queued || m_state.CanAffordBuild(static_cast<std::int32_t>(index));
      const bool atTopLevel = row.level > BUILDING_LEVELS;

      // The order of these tests is the order the reasons outrank each other. A queued tile stays
      // blue at the lock because it is a receipt of what this lock will take, exactly as its rail
      // row does; everything below that is a reason the tile cannot be tapped.
      TileState state = TileState::Available;
      if (row.rising)
      {
        state = TileState::Rising;
      }
      else if (queued)
      {
        state = TileState::Queued;
      }
      else if (atTopLevel)
      {
        state = TileState::TopLevel;
      }
      else if (!row.available)
      {
        state = TileState::Blocked;
      }
      else if (!affordable)
      {
        state = TileState::BeyondThePurse;
      }
      else if (row.isTradeLane)
      {
        state = TileState::Propose;
      }
      shortOfCredits = shortOfCredits || state == TileState::BeyondThePurse;

      BuildTile tile;
      tile.kind = row.kind;
      tile.lane = row.isTradeLane;
      tile.buys = row.level;
      tile.held = row.level > 0 ? row.level - 1 : 0;
      tile.partner = row.partner;

      // **The level ladder says `L2 -> L3`, so the title only has to name the step.** A first build
      // has no step to name and a rising one is not a step the player is choosing (ADR-107). A
      // lane's title is the row's own, because the far end of a lane is a different system from the
      // one this sheet is about and only the server knows which.
      tile.title = row.isTradeLane  ? row.title
                   : row.rising     ? std::format("{} L{} rising", row.building, row.level)
                   : row.level <= 1 ? std::format("{} L{}", row.building, row.level)
                                    : std::format("{} L{} → L{}", row.building, row.level - 1, row.level);

      // What the level BUYS and what it COSTS IN TICKS -- the two numbers a player weighs a
      // shipyard level against a mining level with, and the reason both tables are on the wire
      // (ADR-053, ADR-069). The server wrote the sentence; this only places it.
      tile.detail = row.detail;

      switch (state)
      {
      case TileState::Rising:
      {
        // The ticks that are in, out of the level's own count. `orderedAt` is `completesAt - ticks`
        // (ADR-069), so the fraction is derivable from what the snapshot already carries -- and is
        // not derivable at all for a remembered system, which carries no construction (ADR-022).
        const std::uint32_t orderedAt = row.completesAt > row.ticks ? row.completesAt - row.ticks : 0;
        const std::uint32_t done = m_state.match.tick > orderedAt ? m_state.match.tick - orderedAt : 0;
        const std::uint32_t in = done > row.ticks ? row.ticks : done;
        tile.state = row.ticks == 0 ? std::string{"RISING"} : std::format("{} OF {} TICKS", in, row.ticks);
        tile.note = std::format("DONE T{}", row.completesAt);
        tile.progress = row.ticks == 0 ? -1.0F : static_cast<float>(in) / static_cast<float>(row.ticks);
        break;
      }
      case TileState::Queued:
        // The note is an INSTRUCTION, and it is the only one on the grid: at the lock or offline
        // there is no tap to describe, so it is left off rather than dimmed into a lie (ADR-065).
        // Every other note is a fact and stays true whether or not anything can be ordered.
        tile.state = std::format("QUEUED −{}", row.cost);
        tile.note = OrdersEditable() ? "TAP TO TAKE BACK" : std::string{};
        break;
      case TileState::BeyondThePurse:
        tile.state = std::format("{} CR", row.cost);
        tile.note = std::format("NEED {} MORE", BuildShortfall(static_cast<std::int32_t>(index)));
        tile.stateIsMedium = false;
        tile.moneyReason = true;
        break;
      case TileState::Blocked:
        tile.state = std::format("{} CR", row.cost);
        tile.note = std::format("AFTER T{}", sheetRisingLandsAt);
        tile.stateIsMedium = false;
        break;
      case TileState::TopLevel:
        tile.state = std::format("L{} · MAX", BUILDING_LEVELS);
        tile.stateIsMedium = false;
        break;
      case TileState::Propose:
        tile.state = std::format("PROPOSE {} CR", row.cost);
        tile.note = std::format("OPEN {} TICKS", row.ticks);
        break;
      case TileState::Available:
      default:
      {
        tile.state = std::format("{} CR", row.cost);

        // **What the purse has left once this AND the queue are paid**, which is the number a
        // player picking between two tiles is actually short of (ADR-078). Omitted once it is big
        // enough not to be the question -- three digits of change on every tile is noise.
        const std::uint32_t committed = m_state.orders.QueuedBuildCost() + row.cost;
        const std::uint32_t left = m_state.player.credits > committed ? m_state.player.credits - committed : 0;
        tile.note = left >= 100 ? std::string{} : std::format("{} CR LEFT AFTER", left);
        break;
      }
      }

      // Chosen after the switch above, because whether an inert tile's reason is MONEY is what
      // decides the colour of its note and only the switch knows (ADR-110).
      tile.ink = InkFor(state, tile.moneyReason);

      // Nothing on a sheet is a target while the orders are locked or the link is down (ADR-065,
      // ADR-085), and a tile that reports rather than offers is never one.
      const bool offers = state == TileState::Available || state == TileState::Queued || state == TileState::Propose;
      tile.target = OrdersEditable() && offers ? static_cast<std::int32_t>(index) : EventRefs::NONE;

      // **Dimmed in place rather than restated**, which is what the sheet has always done at the
      // lock: the state keeps its border and its icon, and every string on it goes to the inert ink
      // (ADR-065).
      if (!OrdersEditable())
      {
        tile.ink.title = Ink::NEUTRAL_DIM;
        tile.ink.detail = Ink::NEUTRAL_DIM;
        tile.ink.control.label = Ink::NEUTRAL_DIM;
        tile.ink.control.number = Ink::NEUTRAL_DIM;
      }

      tiles.push_back(std::move(tile));
    }

    // **One tile per role, in the same corner on every sheet** (ADR-107). Stable, so two tiles of
    // one role -- which nothing composes today -- keep the order the snapshot put them in.
    std::ranges::stable_sort(tiles, [](const BuildTile& _a, const BuildTile& _b) { return TileSlotOf(_a) < TileSlotOf(_b); });
    buildCount = std::format("{} AVAIL · 1 AT A TIME", startable);

    // ---- The fleets standing here (ADR-111) -----------------------------------------------------
    //
    // **The move order's door, and the only one that is about this place.** A fleet used to be
    // reached from a row on the locks rail, which meant the two orders a player can give entered
    // through two different columns; here it sits under the builds of the system it is standing on,
    // because that is the one screen where both are about the same place.
    std::uint32_t shipsHere = 0;
    for (const std::int32_t index : FleetsAtPlace(m_panelSubject))
    {
      const Fleet& fleet = m_state.fleets[static_cast<std::size_t>(index)];
      shipsHere += fleet.ships;

      const bool ordered = fleet.OnALane();
      const std::string ships = fleet.ships == 1 ? std::string{"1 SHIP"} : std::format("{} SHIPS", fleet.ships);

      PlaceFleet row;
      row.name = Uppercased(fleet.name);
      row.detail = ordered ? std::format("{} → {} · T{}", ships, NameOfSystem(m_state, fleet.to), fleet.eta) : ships + " · HOLDING";

      // A queued move wears the same committed state a queued build does, and takes itself back
      // the same way (ADR-110). An unordered fleet is the outlined way on to the map.
      row.button.label = ordered ? "TAKE BACK" : "MOVE ›";
      row.button.state = !OrdersEditable() ? ControlState::Locked : (ordered ? ControlState::Committed : ControlState::Outlined);
      row.action = !OrdersEditable() ? Action::None : (ordered ? Action::CancelFleetOrder : Action::BeginMove);
      row.target = index;
      fleets.push_back(std::move(row));
    }
    fleetCount = fleets.empty() ? std::string{} : (shipsHere == 1 ? std::string{"1 SHIP"} : std::format("{} SHIPS", shipsHere));
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
                              !OrdersEditable() ? EventRefs::NONE : static_cast<std::int32_t>(index)});
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
      pinned.push_back(SheetRow{.title = "CONCEDE", .accent = NO_ACCENT, .target = EventRefs::NONE, .band = true});

      // Red from the first tap, and the armed row says what the NEXT tap does rather than what this
      // row is -- the only warning a concede gets and the only one it needs.
      pinned.push_back(SheetRow{.title = signal.title,
                                .right = queued ? "SENDING" : (armed ? "TAP AGAIN TO CONFIRM" : std::string{}),
                                .accent = armed || queued ? Ink::RED : NO_ACCENT,
                                .target = !OrdersEditable() ? EventRefs::NONE : concede,
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
    // The `--dev` flag IS the disclosure now (ADR-091): the only way to this sheet is a button that
    // ships hidden, so the person looking at it already knows what it is.
    title = std::format("REPLAY TICK {}", m_panelSubject);
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
  // **THE CONCEDE IS PINNED BELOW THE SIX, NOT INSIDE THEM** (ADR-064, amended by ADR-093). It must
  // always be reachable -- it is the only order on this screen that cannot be taken back and the
  // only way out of a match -- and keeping it inside the cap made it cost a real signal every time
  // the board got busy enough to want both. It sits above `CANCEL`, under its own band, and neither
  // it nor the band counts against the six.
  const std::size_t shown = std::min(rows.size(), SHEET_MAXIMUM_ROWS);
  const std::size_t notShown = rows.size() - shown;
  const bool clipped = notShown > 0;

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
  for (const SheetRow& row : pinned)
  {
    listHeight += row.band ? SHEET_BAND_HEIGHT : SHEET_ROW_HEIGHT;
  }

  // ---- The place sheet's body, as blocks (ADR-111) ---------------------------------------------
  //
  // **A block is the unit this body scrolls by**, and the list is composed before anything is drawn
  // for the reason a digest card's height is: the scroll position is an index into it, and a pass
  // that measured and a pass that drew could disagree about where a block starts.
  const std::size_t tileRows = (std::min(tiles.size(), SHEET_TILE_SLOTS) + SHEET_TILE_COLUMNS - 1) / SHEET_TILE_COLUMNS;
  const float tileWidth = (width - 2.0F * CARD_PADDING - SHEET_TILE_GAP) / static_cast<float>(SHEET_TILE_COLUMNS);

  std::vector<Block> blocks;
  float bodyTail = 0.0F;
  if (m_panel == Panel::Place)
  {
    blocks.push_back(Block{BlockKind::BuildBand, 0, SHEET_TILE_TOP + SHEET_BAND_HEIGHT});
    if (tiles.empty())
    {
      // A system with everything at its top level says so rather than drawing an empty grid. The
      // same bargain the signal picker makes with an empire that has nobody to talk to.
      blocks.push_back(Block{BlockKind::NothingToBuild, 0, SHEET_TILE_TOP + static_cast<float>(LINE_HEIGHT)});
    }
    for (std::size_t row = 0; row < tileRows; ++row)
    {
      blocks.push_back(Block{BlockKind::TileRow, row, (row == 0 ? 0.0F : SHEET_TILE_GAP) + SHEET_TILE_HEIGHT});
    }
    bodyTail = SHEET_TILE_BOTTOM;

    if (!fleets.empty())
    {
      blocks.push_back(Block{BlockKind::Divider, 0, SHEET_TILE_BOTTOM + 1.0F});
      blocks.push_back(Block{BlockKind::FleetBand, 0, SHEET_BAND_HEIGHT});
      for (std::size_t index = 0; index < fleets.size(); ++index)
      {
        blocks.push_back(Block{BlockKind::FleetRow, index, SHEET_ROW_HEIGHT});
      }
      bodyTail = CARD_PADDING;
    }
  }

  // **One slot under the header for the thing the body cannot say about itself**, and THREE
  // sentences compete for it (ADR-107 adds the third).
  //
  // At the lock the sheet stays and goes inert (ADR-065). Its controls are already not targets --
  // every panel above passes `EventRefs::NONE` while the orders are locked -- so what is left is to
  // say why, in the rail's own words and in the rail's amber. That outranks the other two: nothing
  // here can be acted on for a reason that has nothing to do with this system or this purse.
  //
  // Then a system that is building says so (ADR-070): every tile but one on it is inert and the
  // reason is the same for all of them, which is a statement about the sheet rather than about a
  // tile. It outranks the purse because a purse that covers a build the lock would refuse anyway is
  // not why the tile is dim.
  //
  // Otherwise a place sheet says what the queue has already taken (ADR-078). A tile is refused
  // against the purse MINUS what is queued, and every number that reaches the eye beside it -- the
  // top bar's, the rail header's -- is the purse before it, so the sheet arrived at `NEED 4 MORE`
  // under a bar reading `46 CR` and the arithmetic was nowhere. Amber only when it is the reason
  // something here is not a target; a queue the purse still covers is a note, not a warning.
  const bool atLock = m_state.orders.locked && !m_state.match.finished;
  const std::string help = m_offline ? std::string{"The link is down. Nothing you tap here is sent; the board is yours to read."}
                           : atLock  ? LockSentence()
                           : !risingHelp.empty() ? risingHelp
                                                 : (m_panel == Panel::Place ? PurseSentence() : std::string{});
  const std::vector<std::string> sheetHelp =
    help.empty() ? std::vector<std::string>{} : FontRenderer::WrapToWidth(help, static_cast<std::uint32_t>(width - 2.0F * CARD_PADDING));
  const Color helpInk = atLock || m_offline || shortOfCredits ? Ink::AMBER : Ink::TEXT_DETAIL;
  // 8 above and 8 below the line, which is what a wrapped sentence needs to sit clear of the rule
  // over it and the first tile under it (ADR-107).
  const float helpHeight = sheetHelp.empty() ? 0.0F : static_cast<float>(sheetHelp.size()) * static_cast<float>(LINE_HEIGHT) + 16.0F;

  // ---- What the body gets, and what scrolls (ADR-111) ------------------------------------------
  //
  // **A sheet may take half the map pane and no more** (ADR-052), and the place sheet is the first
  // body in this client that can want more than that: a full grid, a divider, a band and two fleets
  // is 419 pixels before the header and the bar. So the body is capped, the FLEETS section is
  // pinned above the bottom bar when the rest of it scrolls, and the blocks that do not fit are
  // reached with a wheel notch or a drag banked to a block.
  //
  // The cap is what is LEFT of the share once the header, the help sentence and the bar have taken
  // theirs, floored at a band and one row of tiles: a sheet whose grid is scrolled out of sight is
  // a sheet about a place with nothing on it.
  float bodyHeight = bodyTail;
  for (const Block& block : blocks)
  {
    bodyHeight += block.height;
  }

  const float bodyCap = std::max(SHEET_BODY_MINIMUM, SHEET_MAP_SHARE - SHEET_HEADER_HEIGHT - helpHeight - SHEET_ACTION_HEIGHT);
  const bool scrolls = bodyHeight > bodyCap;

  // **The whole FLEETS section is pinned, or none of it is** (ADR-093's shape, ADR-111's subject).
  // Pinning the band alone would put the label above the bottom bar and leave `MOVE` behind the
  // scroll, which is the opposite of what pinning it is for; pinning a couple of rows and hiding
  // the rest would be a sheet that quietly forgets a fleet. So it is pinned when the whole section
  // fits in half the body, and otherwise it scrolls with everything else.
  std::size_t pinnedFrom = blocks.size();
  float pinnedHeight = 0.0F;
  if (scrolls && !fleets.empty())
  {
    const float section = SHEET_BAND_HEIGHT + static_cast<float>(fleets.size()) * SHEET_ROW_HEIGHT + bodyTail;
    if (section <= bodyCap * 0.5F)
    {
      pinnedFrom = blocks.size() - fleets.size() - 1;
      pinnedHeight = section;
    }
  }

  const float scrollRoom = std::max(0.0F, bodyCap - pinnedHeight);

  // Clamped against what the last frame measured, the way every other scrolling column on this
  // screen is: the top may go no further than the position that still fills the room (ADR-080).
  std::size_t lastTop = 0;
  if (scrolls)
  {
    float fromEnd = pinnedFrom == blocks.size() ? bodyTail : 0.0F;
    for (std::size_t index = pinnedFrom; index-- > 0;)
    {
      if (fromEnd + blocks[index].height > scrollRoom && lastTop == 0)
      {
        lastTop = index + 1;
        break;
      }
      fromEnd += blocks[index].height;
    }
  }
  m_sheetScroll = std::min(m_sheetScroll, lastTop);

  float drawnHeight = 0.0F;
  std::size_t lastBlock = m_sheetScroll;
  {
    float used = 0.0F;
    const float room = scrolls ? scrollRoom : bodyHeight - pinnedHeight;
    while (lastBlock < pinnedFrom && (lastBlock == m_sheetScroll || used + blocks[lastBlock].height <= room))
    {
      used += blocks[lastBlock].height;
      ++lastBlock;
    }
    drawnHeight = scrolls ? scrollRoom : used + (pinnedFrom == blocks.size() ? bodyTail : 0.0F);
  }
  m_sheetBlocks = pinnedFrom;
  m_sheetBlocksShown = lastBlock - m_sheetScroll;

  const float bodyDrawn = m_panel == Panel::Place ? drawnHeight + pinnedHeight : listHeight;
  const float height = SHEET_HEADER_HEIGHT + helpHeight + bodyDrawn + SHEET_ACTION_HEIGHT;
  const float y = Frame::SCREEN_HEIGHT - SHEET_MARGIN - height;

  _shapes.FillRect(x, y, width, height, Ink::APP_BACKGROUND);
  _shapes.StrokeRect(x, y, width, height, Ink::CARD_BORDER);

  // **The sheet swallows every tap it is over** (ADR-111). Recorded first, so every control drawn
  // on top of it wins the ones it is under; what is left is the space between them, which used to
  // fall through to the map and open whatever was behind the sheet.
  AddHit(x, y, width, height, Action::None, 0);

  // ---- Header ----------------------------------------------------------------------------------
  //
  // The display cut, centred by the cut's own metrics rather than the body's -- `CenterTextY` takes
  // the face for exactly this reason, and a 44px header around a 22px box is still a 44px header
  // (ADR-084).
  const std::int32_t statusY = CenterTextY(y, SHEET_HEADER_HEIGHT);
  float headerX = x + CARD_PADDING;
  if (headerDisc.alpha != 0)
  {
    // The same 10px disc the rail's `PLACES` row wears, and a disc rather than a square because a
    // place is round on this screen and a fleet is not (ADR-079, ADR-112).
    _shapes.FillEllipse(headerX + PLACE_DISC_SIZE * 0.5F, y + SHEET_HEADER_HEIGHT * 0.5F, PLACE_DISC_SIZE * 0.5F, PLACE_DISC_SIZE * 0.5F,
                        headerDisc);
    headerX += PLACE_DISC_SIZE + CARD_PADDING;
  }
  _text.DrawText(static_cast<std::int32_t>(headerX), CenterTextY(y, SHEET_HEADER_HEIGHT, Face::MonoDisplay), title, Ink::TEXT_PRIMARY,
                 Face::MonoDisplay);
  headerX += static_cast<float>(FontRenderer::MeasurePixels(title, Face::MonoDisplay)) + CARD_PADDING;
  DrawRight(_text, x + width - CARD_PADDING, statusY, "X", Ink::TEXT_MUTED);

  // ---- The header's status slot ----------------------------------------------------------------
  //
  // One position, inboard of the `X`'s 44-pixel corner -- which is a target and must never have a
  // chip drawn into it -- and three things that can occupy it, in this order.
  float statusLeft = x + width - SHEET_HEADER_HEIGHT;
  if (atLock || m_offline)
  {
    // The same filled grey chip the locks rail wears. `OFFLINE` where `LOCKED` goes, because the
    // two are the same shape of statement -- this sheet is showing you something it cannot take an
    // order about -- and differ only in why (ADR-085).
    const std::string chip = m_offline ? "OFFLINE" : "LOCKED";
    const auto chipWidth = static_cast<float>(FontRenderer::MeasurePixels(chip)) + 12.0F;
    const float chipX = statusLeft - chipWidth;
    _shapes.FillRect(chipX, y + 10.0F, chipWidth, 16.0F, Ink::LOCKED_FILL);
    _text.DrawText(static_cast<std::int32_t>(chipX) + 6, statusY, chip, Ink::APP_BACKGROUND);
    statusLeft = chipX;
  }
  else if (sheetRisingLandsAt != 0)
  {
    // **A system that is building says so where its purse would go** (ADR-070, ADR-107). Outlined
    // rather than filled, because it reports the board rather than taking the sheet away: the grey
    // chip above means nothing here can be ordered at all, and this means one thing already was.
    const std::string chip = std::format("RISING · DONE T{}", sheetRisingLandsAt);
    const auto chipWidth = static_cast<float>(FontRenderer::MeasurePixels(chip)) + 14.0F;
    const float chipX = statusLeft - chipWidth;
    constexpr float CHIP_HEIGHT = 22.0F;
    _shapes.StrokeRect(chipX, BandTopForText(statusY, CHIP_HEIGHT), chipWidth, CHIP_HEIGHT, Ink::BLUE);
    _text.DrawText(static_cast<std::int32_t>(chipX) + 7, statusY, chip, Ink::BLUE);
    statusLeft = chipX;
  }
  else if (m_panel == Panel::Place)
  {
    // **The purse, on the sheet that spends it** (ADR-087, ADR-107). The same pair of numbers the
    // top bar carries and in the same order -- the purse, then in blue what this tick's queue has
    // already taken of it -- because a tile priced `NEED 19 MORE` is priced against the difference
    // and the difference was 400 pixels away. Drawn right to left, so the qualifier is outermost.
    float cursor = statusLeft - 6.0F;
    const std::uint32_t committed = m_state.orders.QueuedBuildCost();
    if (committed > 0)
    {
      const std::string spent = std::format("−{}", committed);
      DrawRight(_text, cursor, statusY, spent, Ink::BLUE);
      cursor -= static_cast<float>(FontRenderer::MeasurePixels(spent)) + 6.0F;
    }
    const std::string purse = std::format("{} CR", m_state.player.credits);
    DrawRight(_text, cursor, statusY, purse, Ink::TEXT_MUTED);
    statusLeft = cursor - static_cast<float>(FontRenderer::MeasurePixels(purse));
  }

  // **What the place IS, in the room the status slot leaves** (ADR-111). Dropped whole rather than
  // clipped or wrapped: it is a clause about a system whose name is already on the sheet, and half
  // of it read against a purse would be worse than none of it. The top bar's census drops its
  // clauses the same way (SCREENS.md 01).
  if (!headerFacts.empty() && headerX + static_cast<float>(FontRenderer::MeasurePixels(headerFacts)) <= statusLeft - CARD_PADDING)
  {
    _text.DrawText(static_cast<std::int32_t>(headerX), statusY, headerFacts, Ink::TEXT_MUTED);
  }

  // A close target the height of the header, not the width of one glyph.
  AddHit(x + width - SHEET_HEADER_HEIGHT, y, SHEET_HEADER_HEIGHT, SHEET_HEADER_HEIGHT, Action::ClosePanel, 0);
  _shapes.FillRect(x, y + SHEET_HEADER_HEIGHT, width, 1.0F, Ink::DIVIDER);

  // ---- What the body cannot say about itself ---------------------------------------------------
  if (!sheetHelp.empty())
  {
    std::int32_t helpY = static_cast<std::int32_t>(y + SHEET_HEADER_HEIGHT) + 8;
    for (const std::string& line : sheetHelp)
    {
      _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), helpY, line, helpInk, Face::SansMedium);
      helpY += LINE_HEIGHT;
    }
    _shapes.FillRect(x, y + SHEET_HEADER_HEIGHT + helpHeight, width, 1.0F, Ink::DIVIDER);
  }

  float rowY = y + SHEET_HEADER_HEIGHT + helpHeight;

  // ---- The place sheet's blocks ----------------------------------------------------------------
  //
  // **The whole tile is the target** (ADR-107): 284x96 against a 44-pixel floor, so there is nothing
  // to grow and nothing a thumb can land between. The bottom line is where the eye goes -- a price
  // on the left and what it leaves on the right -- and the top line is what it is.
  const auto drawTile = [&](const BuildTile& _tile, float _tileX, float _tileY)
  {
    DrawControlBox(_shapes, _tileX, _tileY, tileWidth, SHEET_TILE_HEIGHT, _tile.ink.control);

    // **The ticks that are in, along the inside of the bottom edge** (ADR-107). Three pixels, the
    // whole width faint and the done fraction solid, so a rising tile reports progress without
    // spending a line on it. Drawn before the text, because the text is what has to stay on top.
    if (_tile.progress >= 0.0F)
    {
      const float barY = _tileY + SHEET_TILE_HEIGHT - TILE_PROGRESS_HEIGHT;
      _shapes.FillRect(_tileX, barY, tileWidth, TILE_PROGRESS_HEIGHT, WithAlpha(Ink::BLUE, 51));
      _shapes.FillRect(_tileX, barY, tileWidth * _tile.progress, TILE_PROGRESS_HEIGHT, Ink::BLUE);
    }

    // Three lines spread down the tile's 76 pixels of content: a 22px icon row, then the detail,
    // then the bottom line, ten pixels apart. 10 + 22 + 10 + 17 + 10 + 17 + 10 is exactly 96.
    const float contentX = _tileX + TILE_PADDING_X;
    const float iconRowY = _tileY + TILE_PADDING_Y;
    const std::int32_t iconRowTextY = CenterTextY(iconRowY, TILE_ICON_SIZE, Face::MonoMedium);
    DrawBuildIcon(_shapes, _tile.kind, _tile.lane, contentX, iconRowY, _tile.ink.accent);

    const float titleX = contentX + TILE_ICON_SIZE + TILE_ICON_GAP;
    _text.DrawText(static_cast<std::int32_t>(titleX), iconRowTextY, _tile.title, _tile.ink.title, Face::MonoMedium);

    // **The level ladder, right-aligned on the icon row**: filled for a level already held,
    // outlined in the tile's accent for the one this tile buys, a hairline for the rest. It says
    // `L2 -> L3` as a picture, which is why the title only has to name the step once. A lane has no
    // levels and shows the partner it is waiting on in the same slot instead.
    const float ladderRight = _tileX + tileWidth - TILE_PADDING_X;
    if (_tile.lane)
    {
      DrawRight(_text, ladderRight, iconRowTextY, _tile.partner, Ink::TEXT_MUTED);
    }
    else
    {
      const float pipY = iconRowY + (TILE_ICON_SIZE - TILE_PIP_SIZE) * 0.5F;
      const float ladderWidth =
        static_cast<float>(BUILDING_LEVELS) * TILE_PIP_SIZE + static_cast<float>(BUILDING_LEVELS - 1) * TILE_PIP_GAP;
      for (std::uint32_t level = 0; level < BUILDING_LEVELS; ++level)
      {
        const float pipX = ladderRight - ladderWidth + static_cast<float>(level) * (TILE_PIP_SIZE + TILE_PIP_GAP);
        if (level < _tile.held)
        {
          _shapes.FillRect(pipX, pipY, TILE_PIP_SIZE, TILE_PIP_SIZE, _tile.ink.title);
        }
        else if (_tile.buys > 0 && level == _tile.buys - 1)
        {
          _shapes.StrokeRect(pipX, pipY, TILE_PIP_SIZE, TILE_PIP_SIZE, _tile.ink.accent);
        }
        else
        {
          _shapes.StrokeRect(pipX, pipY, TILE_PIP_SIZE, TILE_PIP_SIZE, Ink::OUTLINE);
        }
      }
    }

    _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(iconRowY + TILE_ICON_SIZE + TILE_PADDING_Y), _tile.detail,
                   _tile.ink.detail, Face::SansRegular);

    const auto bottomY = static_cast<std::int32_t>(_tileY + SHEET_TILE_HEIGHT - TILE_PADDING_Y) - LINE_HEIGHT;
    _text.DrawText(static_cast<std::int32_t>(contentX), bottomY, _tile.state, _tile.ink.control.label,
                   _tile.stateIsMedium ? Face::MonoMedium : Face::MonoRegular);
    if (!_tile.note.empty())
    {
      DrawRight(_text, ladderRight, bottomY, _tile.note, _tile.ink.control.number);
    }

    if (_tile.target != EventRefs::NONE)
    {
      AddHit(_tileX, _tileY, tileWidth, SHEET_TILE_HEIGHT, Action::ToggleBuild, _tile.target);
    }
  };

  /// A 22px band: a muted label on the left and a count on the right, over what follows it. Never a
  /// target, which is the one band on this screen that does not grow to the floor (ADR-100).
  const auto drawBand = [&](std::string_view _label, std::string_view _count, float _bandY)
  {
    const std::int32_t labelY = CenterTextY(_bandY, SHEET_BAND_HEIGHT);
    _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), labelY, _label, Ink::TEXT_MUTED);
    DrawRight(_text, x + width - CARD_PADDING, labelY, _count, Ink::TEXT_MUTED);
  };

  /// One fleet's row: a box, the name, what it is doing, and the one control on it.
  const auto drawFleetRow = [&](const PlaceFleet& _fleet, float _fleetY)
  {
    const float rowX = x + CARD_PADDING;
    const float rowWidth = width - 2.0F * CARD_PADDING;
    _shapes.StrokeRect(rowX, _fleetY, rowWidth, SHEET_ROW_HEIGHT, Ink::CARD_BORDER);

    const std::int32_t textY = CenterTextY(_fleetY, SHEET_ROW_HEIGHT);
    float textX = rowX + CARD_PADDING;
    _shapes.FillRect(textX, _fleetY + (SHEET_ROW_HEIGHT - 8.0F) * 0.5F, 8.0F, 8.0F, OwnerColor(m_state.viewer, m_state.viewer));
    textX += 8.0F + CARD_PADDING;

    _text.DrawText(static_cast<std::int32_t>(textX), textY, _fleet.name, Ink::TEXT_PRIMARY, Face::MonoMedium);
    textX += static_cast<float>(FontRenderer::MeasurePixels(_fleet.name, Face::MonoMedium)) + CARD_PADDING;
    _text.DrawText(static_cast<std::int32_t>(textX), textY, _fleet.detail, Ink::TEXT_MUTED);

    const float buttonWidth = ButtonWidth(_fleet.button);
    const float buttonX = rowX + rowWidth - BUTTON_GAP - buttonWidth;
    const float buttonY = _fleetY + (SHEET_ROW_HEIGHT - BUTTON_HEIGHT) * 0.5F;
    const bool hovered = _fleet.action != Action::None && m_pointerXPixels >= buttonX && m_pointerXPixels < buttonX + buttonWidth &&
                         m_pointerYPixels >= buttonY && m_pointerYPixels < buttonY + BUTTON_HEIGHT;
    DrawButton(_shapes, _text, buttonX, buttonY, buttonWidth, _fleet.button,
               ControlInkFor(_fleet.button.state, ControlKind::Button, hovered));

    if (_fleet.action != Action::None)
    {
      // The button is 28 tall inside a 44 row, so the row's own height is the target: growing the
      // rectangle around the button gives exactly the row it sits in (ADR-100, ADR-110).
      AddHit(buttonX, _fleetY, std::max(buttonWidth, TOUCH_FLOOR), SHEET_ROW_HEIGHT, _fleet.action, _fleet.target);
      m_hoverRegions.push_back(HoverRegion{buttonX, buttonY, buttonWidth, BUTTON_HEIGHT});
    }
  };

  const auto drawBlock = [&](const Block& _block, float _blockY)
  {
    switch (_block.kind)
    {
    case BlockKind::BuildBand:
      drawBand("BUILD", buildCount, _blockY + SHEET_TILE_TOP);
      return;
    case BlockKind::NothingToBuild:
      _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), static_cast<std::int32_t>(_blockY + SHEET_TILE_TOP),
                     "Both buildings are at their top level.", Ink::NEUTRAL_DIM, Face::SansRegular);
      return;
    case BlockKind::TileRow:
    {
      const float tileY = _blockY + (_block.index == 0 ? 0.0F : SHEET_TILE_GAP);
      for (std::size_t column = 0; column < SHEET_TILE_COLUMNS; ++column)
      {
        const std::size_t at = _block.index * SHEET_TILE_COLUMNS + column;
        if (at >= std::min(tiles.size(), SHEET_TILE_SLOTS))
        {
          break;
        }
        drawTile(tiles[at], x + CARD_PADDING + static_cast<float>(column) * (tileWidth + SHEET_TILE_GAP), tileY);
      }
      return;
    }
    case BlockKind::Divider:
      _shapes.FillRect(x, _blockY + SHEET_TILE_BOTTOM, width, 1.0F, Ink::DIVIDER);
      return;
    case BlockKind::FleetBand:
      drawBand("FLEETS HERE", fleetCount, _blockY);
      return;
    case BlockKind::FleetRow:
    default:
      drawFleetRow(fleets[_block.index], _blockY);
      return;
    }
  };

  if (m_panel == Panel::Place)
  {
    for (std::size_t index = m_sheetScroll; index < lastBlock; ++index)
    {
      drawBlock(blocks[index], rowY);
      rowY += blocks[index].height;
    }
    rowY = y + SHEET_HEADER_HEIGHT + helpHeight + drawnHeight;
    for (std::size_t index = pinnedFrom; index < blocks.size(); ++index)
    {
      drawBlock(blocks[index], rowY);
      rowY += blocks[index].height;
    }
    rowY = y + height - SHEET_ACTION_HEIGHT;
  }

  // ---- Rows ------------------------------------------------------------------------------------
  //
  // The capped list, then whatever is pinned below it (ADR-093). One lambda, because a pinned row is
  // an ordinary row that is simply not counted -- a second copy of this would be a second place for
  // a band's rule or a row's hit rectangle to drift.
  bool previousWasBand = true;

  const auto drawRow = [&](const SheetRow& _row)
  {
    const bool tappable = _row.target != EventRefs::NONE;

    // A band is a label over what follows it, drawn like the rails' section headers: a rule, then
    // the label, and nothing to tap.
    if (_row.band)
    {
      _shapes.FillRect(x + CARD_PADDING, rowY, width - 2.0F * CARD_PADDING, 1.0F, Ink::DIVIDER);
      _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), CenterTextY(rowY, SHEET_BAND_HEIGHT), _row.title, Ink::TEXT_MUTED);
      rowY += SHEET_BAND_HEIGHT;
      previousWasBand = true;
      return;
    }

    // No second rule directly under a band's: one line is a section header and two is a box.
    if (!previousWasBand)
    {
      _shapes.FillRect(x + CARD_PADDING, rowY, width - 2.0F * CARD_PADDING, 1.0F, Ink::DIVIDER);
    }
    previousWasBand = false;

    float textX = x + CARD_PADDING;
    if (_row.accent.alpha != 0)
    {
      _shapes.FillRect(textX, rowY + 18.0F, 8.0F, 8.0F, _row.accent);
      textX += 16.0F;
    }

    // One line centres in the row; two sit either side of its middle. THE ROW HEIGHT DOES NOT
    // CHANGE with the content -- a column of rows of one height is what a finger aims at.
    const std::int32_t titleY = _row.detail.empty() ? CenterTextY(rowY, SHEET_ROW_HEIGHT) : static_cast<std::int32_t>(rowY) + 12;
    const Color titleColor = !tappable ? Ink::NEUTRAL_DIM : (_row.alarm ? Ink::RED : Ink::TEXT_PRIMARY);
    _text.DrawText(static_cast<std::int32_t>(textX), titleY, _row.title, titleColor);

    if (!_row.detail.empty())
    {
      _text.DrawText(static_cast<std::int32_t>(textX), static_cast<std::int32_t>(rowY) + 26, _row.detail,
                     tappable ? _row.detailInk : Ink::NEUTRAL_DIM, Face::SansRegular);
    }
    if (!_row.right.empty())
    {
      DrawRight(_text, x + width - CARD_PADDING, CenterTextY(rowY, SHEET_ROW_HEIGHT), _row.right,
                !tappable ? Ink::NEUTRAL_DIM : (_row.alarm ? Ink::RED : Ink::TEXT_DETAIL));
    }

    if (tappable)
    {
      AddHit(x, rowY, width, SHEET_ROW_HEIGHT, rowAction, _row.target);
    }
    rowY += SHEET_ROW_HEIGHT;
  };

  for (std::size_t index = 0; index < shown; ++index)
  {
    drawRow(rows[index]);
  }

  if (clipped)
  {
    _shapes.FillRect(x + CARD_PADDING, rowY, width - 2.0F * CARD_PADDING, 1.0F, Ink::DIVIDER);
    _text.DrawText(static_cast<std::int32_t>(x + CARD_PADDING), CenterTextY(rowY, SHEET_CLIPPED_HEIGHT),
                   std::format("+{} MORE THAN THIS SHEET CAN SHOW", notShown), Ink::NEUTRAL_DIM);
    rowY += SHEET_CLIPPED_HEIGHT;
    previousWasBand = false;
  }

  // Below the count and above `CANCEL`: the concede, and nothing else today (ADR-093).
  for (const SheetRow& row : pinned)
  {
    drawRow(row);
  }

  // ---- The bottom bar --------------------------------------------------------------------------
  //
  // A bar as well as the header's X. The X is where a mouse expects it and the bar is where a thumb
  // already is, and closing a sheet opened by mistake is the commonest thing done to one. The place
  // sheet says `DONE` rather than `CANCEL`, because there is nothing there to back out of: the
  // orders it took are already in, and closing it is finishing (ADR-111).
  _shapes.FillRect(x, rowY, width, 1.0F, Ink::DIVIDER);
  DrawCentered(_text, x + width * 0.5F, CenterTextY(rowY, SHEET_ACTION_HEIGHT), barLabel, Ink::TEXT_MUTED);
  AddHit(x, rowY, width, SHEET_ACTION_HEIGHT, Action::ClosePanel, 0);
}

} // namespace Lockstep
