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
#include <array>
#include <cctype>
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

// ---- One control vocabulary (ADR-110) ----------------------------------------------------------

/// What a control IS, which is the whole of how it is drawn.
///
/// **Four states and a lock, and the same five are applied to a button, a tile, a sheet row and a
/// rail row** (ADR-110). The hue never changes between them -- only the fill, the border's style
/// and the alpha -- so a player learns one vocabulary rather than one per surface, and a queued
/// order looks the same wherever they meet it.
enum class ControlState : std::uint8_t
{
  /// The one recommended thing to do. **One per screen** (ADR-089).
  Primary,
  /// Any other order, and every link.
  Outlined,
  /// Yours, queued, and the next tap takes it back (ADR-053).
  Committed,
  /// Cannot be ordered, and the number segment says why. **Never a target**, which is what the
  /// dashed border means and the only thing it means.
  Inert,
  /// At the lock, or with the link down (ADR-065, ADR-085). Not a target, and the one state that is
  /// about the screen rather than about the control.
  Locked
};

/// Which surface a control is. It decides the geometry -- and, in exactly one case, an ink.
enum class ControlKind : std::uint8_t
{
  Button,
  Tile,
  SheetRow,
  RailRow
};

/// The chrome and the inks one state is drawn in, chosen once.
struct ControlInk
{
  /// Fully transparent for a state with no border.
  Color border;
  /// Dashed, which only `Inert` is (ADR-110).
  bool dashed = false;
  /// Fully transparent for a state with no fill.
  Color fill;
  Color label;
  /// The number segment: its ink, the hairline that separates it from the label, and the ground it
  /// sits on. A filled control shades the ground and draws no line; every other state draws the
  /// line and leaves the ground alone, because a shade over a transparent box is a grey box.
  Color number;
  Color segmentRule;
  Color segmentFill;
};

/// The one state table, read by every control on this page.
///
/// `_moneyReason` is what makes an inert control's number amber: money is the reason a player can
/// do something about before the lock, and the board is not.
[[nodiscard]] ControlInk ControlInkFor(ControlState _state, ControlKind _kind, bool _hovered = false, bool _moneyReason = false) noexcept
{
  constexpr Color NO_INK = {0, 0, 0, 0};
  switch (_state)
  {
  case ControlState::Primary:
    // **The ring goes in the border slot, because a filled control has no room for a hover FILL**:
    // its rest state is already the brightest thing on the screen, so the press is said by lifting
    // the fill toward white and ringing it in the colour it came from.
    return ControlInk{.border = _hovered ? Ink::BLUE : NO_INK,
                      .dashed = false,
                      .fill = _hovered ? Ink::BUTTON_PRIMARY_HOVER : Ink::BLUE,
                      .label = Ink::APP_BACKGROUND,
                      .number = Ink::APP_BACKGROUND,
                      .segmentRule = NO_INK,
                      .segmentFill = Ink::BUTTON_SEGMENT_SHADE};

  case ControlState::Committed:
    return ControlInk{.border = Ink::BLUE,
                      .dashed = false,
                      .fill = _hovered ? Ink::COMMITTED_HOVER_FILL : Ink::TILE_COMMITTED_FILL,
                      .label = Ink::BLUE,
                      .number = Ink::BLUE,
                      .segmentRule = WithAlpha(Ink::BLUE, 90),
                      .segmentFill = NO_INK};

  case ControlState::Inert:
    return ControlInk{.border = Ink::INERT_BORDER,
                      .dashed = true,
                      .fill = NO_INK,
                      .label = Ink::NEUTRAL_DIM,
                      .number = _moneyReason ? Ink::AMBER : Ink::NEUTRAL_DIM,
                      .segmentRule = Ink::INERT_BORDER,
                      .segmentFill = NO_INK};

  case ControlState::Locked:
    // **A BUTTON at the lock is the filled grey the rail's chip wears; a tile, a sheet row and a
    // rail row dim in place** (ADR-065, amended by ADR-110). The grey says "this is a receipt now"
    // on a control the size of a chip, and it is the same statement the `LOCKED` chip above it
    // makes. At the size of a 284x96 tile or a 260-pixel row it is not that statement at all: a
    // locked sheet would become four light-grey boxes, which is the screen inverted rather than
    // gone quiet, and ADR-065 settled that a sheet at the lock stays where it is and fades.
    if (_kind == ControlKind::Button)
    {
      return ControlInk{.border = NO_INK,
                        .dashed = false,
                        .fill = Ink::LOCKED_FILL,
                        .label = Ink::APP_BACKGROUND,
                        .number = Ink::APP_BACKGROUND,
                        .segmentRule = NO_INK,
                        .segmentFill = Ink::BUTTON_SEGMENT_SHADE};
    }
    return ControlInk{.border = Ink::DIVIDER,
                      .dashed = false,
                      .fill = NO_INK,
                      .label = Ink::NEUTRAL_DIM,
                      .number = Ink::NEUTRAL_DIM,
                      .segmentRule = Ink::DIVIDER,
                      .segmentFill = NO_INK};

  case ControlState::Outlined:
  default:
    return ControlInk{.border = _hovered ? Ink::OUTLINE_HOVER : Ink::OUTLINE,
                      .dashed = false,
                      .fill = _hovered ? Ink::HOVER_FILL : NO_INK,
                      .label = Ink::TEXT_PRIMARY,
                      .number = Ink::TEXT_MUTED,
                      .segmentRule = Ink::DIVIDER,
                      .segmentFill = NO_INK};
  }
}

/// One control's box: its fill, then its border.
///
/// **Four dashed edges rather than a dashed rectangle**, because `ShapeRenderer` dashes a line and
/// has no dashed box, and a fifth primitive for one border style is a primitive to keep in step
/// with `StrokeRect` forever. Each edge is drawn half a pixel inside the bounds for the reason
/// `StrokeRect` insets its own: a 1px border lies INSIDE the box it was given, so it never bleeds
/// into the pixel the thing beside it owns.
void DrawControlBox(ShapeRenderer& _shapes, float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, const ControlInk& _ink)
{
  if (_ink.fill.alpha != 0)
  {
    _shapes.FillRect(_xPixels, _yPixels, _widthPixels, _heightPixels, _ink.fill);
  }
  if (_ink.border.alpha == 0)
  {
    return;
  }
  if (!_ink.dashed)
  {
    _shapes.StrokeRect(_xPixels, _yPixels, _widthPixels, _heightPixels, _ink.border);
    return;
  }

  const float right = _xPixels + _widthPixels;
  const float bottom = _yPixels + _heightPixels;
  const float dash = MainPage::INERT_DASH;
  const float gap = MainPage::INERT_GAP;
  _shapes.DashedLine(_xPixels, _yPixels + 0.5F, right, _yPixels + 0.5F, _ink.border, 1.0F, dash, gap);
  _shapes.DashedLine(_xPixels, bottom - 0.5F, right, bottom - 0.5F, _ink.border, 1.0F, dash, gap);
  _shapes.DashedLine(_xPixels + 0.5F, _yPixels, _xPixels + 0.5F, bottom, _ink.border, 1.0F, dash, gap);
  _shapes.DashedLine(right - 0.5F, _yPixels, right - 0.5F, bottom, _ink.border, 1.0F, dash, gap);
}

/// One button, composed before it is measured and drawn.
///
/// **A number never lives inside the label** (ADR-110). `BUILD 20 CR` was one string, so a queued
/// one became `BUILD 20 CR - QUEUED` and a dear one `BUILD 45 CR - NEED 19 MORE`: a label that
/// grows a suffix every time the state changes, in a column that drops a button that does not fit.
/// The number is its own cell, and the state is said by the chrome rather than spelled out.
struct Button
{
  /// Shouted, mono Medium.
  std::string label;
  /// The second cell: `20 CR`, `-20`, `10 SHIPS`, `NEED 19 MORE`, `AFTER T14`. Empty draws one cell.
  std::string number;
  ControlState state = ControlState::Outlined;
  /// Whether an inert button's reason is money, which is the one an amber number is for.
  bool moneyReason = false;
};

/// How wide a button comes out, in the face it is drawn in.
///
/// Measured rather than assumed, and measured with the segment INCLUDED, because the digest drops a
/// button that does not fit its column and a measurement of the label alone would drop the wrong
/// ones (ADR-053).
[[nodiscard]] float ButtonWidth(const Button& _button)
{
  float width = 2.0F * MainPage::BUTTON_LABEL_PADDING + static_cast<float>(FontRenderer::MeasurePixels(_button.label, Face::MonoMedium));
  if (_button.state == ControlState::Locked)
  {
    width += MainPage::LOCK_GLYPH_SIZE + 6.0F;
  }
  if (!_button.number.empty())
  {
    width +=
      1.0F + 2.0F * MainPage::BUTTON_NUMBER_PADDING + static_cast<float>(FontRenderer::MeasurePixels(_button.number, Face::MonoMedium));
  }
  return width;
}

/// A button's two cells, drawn at a width the caller has already decided it can afford.
void DrawButton(ShapeRenderer& _shapes, FontRenderer& _text, float _xPixels, float _yPixels, float _widthPixels, const Button& _button,
                const ControlInk& _ink)
{
  DrawControlBox(_shapes, _xPixels, _yPixels, _widthPixels, MainPage::BUTTON_HEIGHT, _ink);

  const std::int32_t labelY = CenterTextY(_yPixels, MainPage::BUTTON_HEIGHT, Face::MonoMedium);
  float labelX = _xPixels + MainPage::BUTTON_LABEL_PADDING;
  if (_button.state == ControlState::Locked)
  {
    // A square rather than a padlock: at six pixels a padlock is four grey dots, and the ladder on
    // a build tile already teaches this screen's reader that a small square is a state.
    _shapes.FillRect(labelX, _yPixels + (MainPage::BUTTON_HEIGHT - MainPage::LOCK_GLYPH_SIZE) * 0.5F, MainPage::LOCK_GLYPH_SIZE,
                     MainPage::LOCK_GLYPH_SIZE, _ink.label);
    labelX += MainPage::LOCK_GLYPH_SIZE + 6.0F;
  }
  _text.DrawText(static_cast<std::int32_t>(labelX), labelY, _button.label, _ink.label, Face::MonoMedium);

  if (_button.number.empty())
  {
    return;
  }

  const float segmentWidth =
    2.0F * MainPage::BUTTON_NUMBER_PADDING + static_cast<float>(FontRenderer::MeasurePixels(_button.number, Face::MonoMedium));
  const float segmentX = _xPixels + _widthPixels - segmentWidth;

  if (_ink.segmentFill.alpha != 0)
  {
    _shapes.FillRect(segmentX, _yPixels, segmentWidth, MainPage::BUTTON_HEIGHT, _ink.segmentFill);
  }
  else if (_ink.dashed)
  {
    _shapes.DashedLine(segmentX - 0.5F, _yPixels, segmentX - 0.5F, _yPixels + MainPage::BUTTON_HEIGHT, _ink.segmentRule, 1.0F,
                       MainPage::INERT_DASH, MainPage::INERT_GAP);
  }
  else if (_ink.segmentRule.alpha != 0)
  {
    _shapes.FillRect(segmentX - 1.0F, _yPixels, 1.0F, MainPage::BUTTON_HEIGHT, _ink.segmentRule);
  }

  _text.DrawText(static_cast<std::int32_t>(segmentX + MainPage::BUTTON_NUMBER_PADDING), labelY, _button.number, _ink.number,
                 Face::MonoMedium);
}

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

/// The disc in a place sheet's header and on a `PLACES` rail row (ADR-111, ADR-112). A disc rather
/// than the 8px square a fleet wears, because on this screen a place is round and a fleet is not
/// -- the map has drawn them that way since ADR-079.
constexpr float PLACE_DISC_SIZE = 10.0F;

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

/// A system's name in the screen's voice, or `THE DARK` for a position the graph does not have.
[[nodiscard]] std::string NameOfSystem(const MatchState& _state, std::int32_t _at)
{
  return _at >= 0 && _at < static_cast<std::int32_t>(_state.graph.systems.size())
           ? Uppercased(_state.graph.systems[static_cast<std::size_t>(_at)].name)
           : std::string{"THE DARK"};
}

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

std::string MainPage::FormatPlacement(std::uint32_t _placement)
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

void MainPage::Create(MatchState _state)
{
  // What the player had open, so that a state arriving does not shut it (ADR-065). A sheet is where
  // somebody is in the middle of deciding something, and the tick landing under them is not a
  // reason to take it away -- it is the reason they opened it.
  const Panel wasOpen = m_panel;
  const std::int32_t wasSubject = m_panelSubject;
  const std::int32_t wasSubjectId = m_panelSubjectId;

  // Where this player stood on the digest being replaced, so the chip can say a place was lost
  // (ADR-091). Zero on the first state, which is no placement and so never a slip.
  m_placementDrawn = m_state.player.placement;

  m_state = std::move(_state);
  m_panel = Panel::None;
  m_panelSubject = EventRefs::NONE;
  m_panelSubjectId = EventRefs::NONE;
  m_sheetScroll = 0;
  m_sheetDragPixels = 0.0F;
  m_focusedSystem = EventRefs::NONE;

  // The arming is an index into the signal list, and the list is recomposed with the state. Kept,
  // it would be a row armed that nobody armed.
  m_armedConcede = EventRefs::NONE;

  // A digest is replaced wholesale every tick, so nothing about how the last one was being READ
  // survives it: page three is nowhere in the new one, and the rival whose card was open may have
  // no card at all (ADR-061).
  m_digestTop = 0;
  m_cardsOnScreen = 1;
  m_digestDragPixels = 0.0F;
  m_expandedActor = NOBODY;

  MeasureContent();
  ReopenPanel(wasOpen, wasSubjectId, wasSubject);
}

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

  case Panel::Destination:
  {
    for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
    {
      // Still yours, and still standing. A picker left open across the lock that sent its fleet
      // away is a picker over a fleet the lock would now refuse an order on, which is the sheet
      // ADR-065 keeps open and ADR-077 says has nothing left to offer.
      if (m_state.fleets[index].id != _subjectId || m_state.fleets[index].owner != m_state.viewer || m_state.fleets[index].underWay)
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

void MainPage::OpenPlace(std::int32_t _system)
{
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

std::string MainPage::PurseSentence() const
{
  const std::uint32_t spent = m_state.orders.QueuedBuildCost();
  if (spent == 0)
  {
    return {};
  }
  const std::uint32_t left = spent <= m_state.player.credits ? m_state.player.credits - spent : 0;
  return std::format("Priced against the {} credits left after the {} already queued, not the {} in hand.", left, spent,
                     m_state.player.credits);
}

std::string MainPage::RisingSentence(std::string_view _systemName, std::uint32_t _ticksIn, std::uint32_t _ticks)
{
  if (_ticks == 0)
  {
    return {};
  }

  // Words below ten and digits from ten up, which is the ordinary rule for prose and the reason
  // this is not `std::to_string` (DESIGN-GUIDELINES "Copy": a sentence is a sentence). A level takes
  // one to four ticks today (ADR-069), so the table is the whole of the range and then some.
  const auto spelled = [](std::uint32_t _count)
  {
    constexpr std::array<const char*, 10> WORDS = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
    return _count < WORDS.size() ? std::string{WORDS[_count]} : std::to_string(_count);
  };

  const std::uint32_t in = _ticksIn > _ticks ? _ticks : _ticksIn;
  std::string counted = spelled(in);
  counted[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(counted[0])));
  return std::format("{} cannot take another order until this lands. {} of {} ticks {} in.", _systemName, counted, spelled(_ticks),
                     in == 1 ? "is" : "are");
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

  // **A drag that began on the digest scrolls it**, which is the finger's half of ADR-080: a touch
  // device has no wheel, and the page band alone made reading a long digest a sequence of taps.
  //
  // The column moves in whole cards and a finger moves in pixels, so the remainder is banked rather
  // than thrown away -- without that, a slow drag scrolls nothing at all. `DIGEST_SCROLL_PIXELS` is
  // the frame's row unit (ADR-052): the distance a finger already associates with one row of
  // anything on this screen.
  const bool startedOnDigest = _drag.originXPixels >= 0.0F && _drag.originXPixels < mapLeft && _drag.originYPixels >= Frame::TOP_BAR_HEIGHT;
  if (startedOnDigest)
  {
    constexpr float DIGEST_SCROLL_PIXELS = SHEET_ROW_HEIGHT;
    m_digestDragPixels += _drag.deltaYPixels;

    std::int32_t cards = 0;
    while (m_digestDragPixels <= -DIGEST_SCROLL_PIXELS)
    {
      m_digestDragPixels += DIGEST_SCROLL_PIXELS;
      ++cards;
    }
    while (m_digestDragPixels >= DIGEST_SCROLL_PIXELS)
    {
      m_digestDragPixels -= DIGEST_SCROLL_PIXELS;
      --cards;
    }
    (void)ScrollDigest(cards);
    return true;
  }

  const bool startedOnMap =
    _drag.originXPixels >= mapLeft && _drag.originXPixels < mapRight && _drag.originYPixels >= Frame::TOP_BAR_HEIGHT;
  if (!startedOnMap)
  {
    return false;
  }

  // **A drag that began on an open place sheet scrolls it rather than orbiting the map** (ADR-111).
  // The body moves in whole blocks and a finger moves in pixels, so the remainder is banked exactly
  // as the digest's is -- and this is the half of the gesture that matters, because a wheel is not
  // a finger and this game is for touch (ADR-098, ADR-101).
  if (m_panel == Panel::Place && m_sheetBlocks > m_sheetBlocksShown)
  {
    m_sheetDragPixels += _drag.deltaYPixels;

    std::int32_t blocks = 0;
    while (m_sheetDragPixels <= -SHEET_ROW_HEIGHT)
    {
      m_sheetDragPixels += SHEET_ROW_HEIGHT;
      ++blocks;
    }
    while (m_sheetDragPixels >= SHEET_ROW_HEIGHT)
    {
      m_sheetDragPixels -= SHEET_ROW_HEIGHT;
      --blocks;
    }
    (void)ScrollSheet(blocks);
    return true;
  }

  // Both axes now. Horizontal orbits the camera around the galaxy, vertical raises and lowers it
  // -- which is the difference the turntable could not express and the reason it did not feel
  // like a camera (ADR-017).
  m_mapView.Drag(_drag.deltaXPixels, _drag.deltaYPixels);
  return true;
}

bool MainPage::ScrollDigest(std::int32_t _cards)
{
  if (_cards == 0)
  {
    return false;
  }
  const std::size_t was = m_digestTop;
  const std::int64_t wanted = static_cast<std::int64_t>(m_digestTop) + _cards;

  // Clamped at both ends here rather than only in the draw, so a wheel spun hard against the end of
  // the stack does not bank a hundred notches that have to be spun back.
  m_digestTop = wanted <= 0 ? 0 : static_cast<std::size_t>(wanted);
  return m_digestTop != was;
}

bool MainPage::HandleZoom(std::int32_t _steps, float _xPixels, float _yPixels)
{
  if (_steps == 0)
  {
    return false;
  }

  // The pane under the pointer decides what a notch means: a list over the digest (ADR-080), a
  // camera over the map (ADR-090). One banked count, two meanings, and the pointer is what picks.
  const bool aboveTheBar = _yPixels >= Frame::TOP_BAR_HEIGHT;
  if (aboveTheBar && _xPixels >= 0.0F && _xPixels < Frame::DIGEST_WIDTH)
  {
    // A notch away from the player scrolls DOWN the column. `TakeZoomSteps` counts a notch away as
    // negative -- it was named for a camera, where away is out -- so the sign is flipped here, at
    // the one place that knows the gesture means a list rather than a distance.
    m_digestDragPixels = 0.0F;
    return ScrollDigest(-_steps);
  }

  // Over the map it means what it was banked for, sign and all: away from the player is out --
  // unless a place sheet is under the pointer, which is a list and takes the list's meaning
  // (ADR-111). The sheet is drawn over the pane, so the pane's own gesture cannot also be the
  // sheet's: a notch that zoomed the map out from under an open sheet is a notch spent on
  // something the player cannot see.
  if (aboveTheBar && _xPixels >= Frame::DIGEST_WIDTH && _xPixels < Frame::SCREEN_WIDTH - Frame::ORDERS_WIDTH)
  {
    if (m_panel == Panel::Place)
    {
      m_sheetDragPixels = 0.0F;
      return ScrollSheet(-_steps);
    }
    return m_mapView.Zoom(_steps);
  }

  // The locks rail, which had to learn this the moment its rows became 44 pixels tall (ADR-101).
  // A notch moves it by a row, so the gesture means the same thing it means on the column opposite.
  if (aboveTheBar && _xPixels >= Frame::SCREEN_WIDTH - Frame::ORDERS_WIDTH)
  {
    return ScrollRail(static_cast<float>(-_steps) * TOUCH_FLOOR);
  }

  return false;
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

bool MainPage::HandleKey(Neuron::KeyboardInput::Key _key)
{
  // A screenful, measured from what the last frame actually drew rather than from a number chosen
  // here: cards are different heights and a page is however many of them fit (ADR-080).
  const auto page = static_cast<std::int32_t>(std::max<std::size_t>(m_cardsOnScreen, 1));
  if (_key == Neuron::KeyboardInput::Key::PageDown)
  {
    return ScrollDigest(page);
  }
  if (_key == Neuron::KeyboardInput::Key::PageUp)
  {
    return ScrollDigest(-page);
  }
  return false;
}

void MainPage::AddHit(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, Action _action, std::int32_t _index)
{
  m_hits.push_back(HitRegion{_xPixels, _yPixels, _widthPixels, _heightPixels, _action, _index});
}

std::int32_t MainPage::RegionUnderPointer() const noexcept
{
  for (std::size_t index = 0; index < m_hoverRegions.size(); ++index)
  {
    const HoverRegion& region = m_hoverRegions[index];
    const bool inside = m_pointerXPixels >= region.x && m_pointerXPixels < region.x + region.width && m_pointerYPixels >= region.y &&
                        m_pointerYPixels < region.y + region.height;
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

  // Tested against the PREVIOUS frame's rectangles, exactly as a tap is: layout and hit testing are
  // the same code, so there is only one list and it is a frame old.
  const std::int32_t under = RegionUnderPointer();
  if (under == m_hoveredRegion)
  {
    return false;
  }
  m_hoveredRegion = under;
  return true;
}

bool MainPage::Animating() const noexcept
{
  return std::ranges::any_of(m_state.fleets,
                             [](const Fleet& _fleet) { return _fleet.order == FleetStance::Move && _fleet.from != _fleet.to; });
}

std::vector<std::int32_t> MainPage::FleetsAtPlace(std::int32_t _system) const
{
  std::vector<std::int32_t> here;
  for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
  {
    const Fleet& fleet = m_state.fleets[index];
    // Theirs, from here, and orderable. `underWay` is the one the badge does not have to ask -- a
    // fleet the server already has on a lane is not at either end of it -- and this does, because a
    // tap lands a frame after the badge was drawn and the lock can fall between them (ADR-077).
    if (fleet.owner == m_state.viewer && !fleet.underWay && fleet.from == _system)
    {
      here.push_back(static_cast<std::int32_t>(index));
    }
  }
  return here;
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

    const bool editable = OrdersEditable();
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
    case Action::OpenFleetsAt:
    {
      // **A place sheet opens on a system you HOLD, and on nothing else** (ADR-058, ADR-111). You
      // cannot build on somebody else's ground -- `Match::Validate` refuses it as `NotYourSystem` --
      // and you have nothing standing on it, so a sheet over a rival's capital is a list of orders
      // that system cannot take.
      //
      // A rival's system still focuses, because the tap has to do something visible: a control
      // that silently ignores you is the defect this screen has already been bitten by twice.
      //
      // **The disc and the badge beside it land here together** (ADR-079). They are two targets
      // because they name two things -- the system, and the ships standing on it -- and since the
      // sheet holds both they open the same sheet.
      m_focusedSystem = region->index;
      m_armedConcede = EventRefs::NONE;

      const bool yours = region->index >= 0 && region->index < static_cast<std::int32_t>(m_state.graph.systems.size()) &&
                         m_state.graph.systems[static_cast<std::size_t>(region->index)].owner == m_state.viewer;
      OpenPlace(yours ? region->index : EventRefs::NONE);
      return true;
    }

    case Action::OpenFleet:
    {
      if (region->index < 0 || region->index >= static_cast<std::int32_t>(m_state.fleets.size()))
      {
        return true;
      }
      const Fleet& fleet = m_state.fleets[static_cast<std::size_t>(region->index)];

      // The guard behind the controls, by the rule the lock would refuse the order by (ADR-053,
      // ADR-077). Every control that leads here already declines to be one for a fleet in transit,
      // and this is what makes that the rule rather than three places that agree.
      if (fleet.underWay)
      {
        return true;
      }

      // The map rings where the picker is rooted, so the sheet and the map are about one place:
      // the sheet lists the lanes out of where this fleet stands and the ring says which system
      // that is (ADR-060).
      m_focusedSystem = fleet.from;
      m_panel = Panel::Destination;
      m_panelSubject = region->index;
      m_panelSubjectId = fleet.id;
      return true;
    }

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
        // Refused here, by the rules the lock would refuse it by (ADR-053, ADR-069). The tiles and
        // buttons that lead here already say so and are not targets, so this is the guard behind
        // them rather than the message -- and it is the only place `queuedBuilds` grows, which is
        // what makes it the guard rather than one of several.
        //
        // `available` is the whole of "the lock would start this": the rising row and every row
        // beside it on a system that is building are all rows a queue would be refused (ADR-107).
        const bool startable = region->index >= 0 && region->index < static_cast<std::int32_t>(m_state.orders.builds.size()) &&
                               m_state.orders.builds[static_cast<std::size_t>(region->index)].available;
        if (!startable || !m_state.CanAffordBuild(region->index))
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

        // **`from` is where the fleet stands, whether or not a move has been ordered from here
        // already.** A picker only opens on a fleet that is not under way (ADR-077), and ordering
        // one sets `from` to where it is leaving -- so re-opening it lists the same lanes and
        // picking again replaces the order rather than adding a second hop to it, which is what
        // makes a move editable until the lock like every other order (ADR-031).
        const std::int32_t origin = fleet.from;
        fleet.to = region->index;
        fleet.order = FleetStance::Move;
        fleet.progress = 0.0F;
        fleet.eta = m_state.OrdersTick() + TicksTo(origin, region->index) - 1;
        fleet.status = std::format("ordered - ETA T{}", fleet.eta);
      }
      m_panel = Panel::None;
      return true;

    case Action::CancelFleetOrder:
    {
      // **The other half of taking an order back, and it is a different array from a build's**
      // (ADR-057, ADR-111). Putting `to` back to `from` is the whole of it: a fleet whose two ends
      // agree is standing, which is what `OnALane` reads and what every draw site tests.
      if (!editable || region->index < 0 || region->index >= static_cast<std::int32_t>(m_state.fleets.size()))
      {
        return true;
      }
      Fleet& fleet = m_state.fleets[static_cast<std::size_t>(region->index)];
      if (fleet.underWay)
      {
        return true;
      }
      fleet.to = fleet.from;
      fleet.order = FleetStance::Hold;
      fleet.progress = 0.0F;
      fleet.eta = 0;
      fleet.status.clear();
      return true;
    }

    case Action::ToggleActorCard:
      // One open at a time, so tapping a second card's title closes the first. The column has room
      // for one card's worth of lines and paging two open cards apart is not reading them.
      m_expandedActor = m_expandedActor == region->index ? NOBODY : region->index;
      return true;

    case Action::ShowDigestPage:
      m_digestTop = static_cast<std::size_t>(std::max(0, region->index));
      m_digestDragPixels = 0.0F;
      return true;

    case Action::OpenReplay:
      m_panel = Panel::Replay;
      m_panelSubject = static_cast<std::int32_t>(m_state.match.tick);
      m_panelSubjectId = EventRefs::NONE;
      return true;

    case Action::PageRail:
      // A bandful less one row, so the row a player was reading is still on the screen after the
      // tap. The digest pages the same way for the same reason (ADR-080).
      return ScrollRail(static_cast<float>(region->index) * std::max(TOUCH_FLOOR, m_railViewportPixels - TOUCH_FLOOR));

    case Action::ResetCamera:
      m_mapView.ResetView();
      return true;

    case Action::ClosePanel:
      m_panel = Panel::None;
      return true;

    case Action::None:
      // The sheet's own background. It does nothing and it is handled, which is the whole of what
      // makes the sheet a modal (ADR-111).
      return true;

    default:
      break;
    }
  }

  return false;
}

void MainPage::DrawWorld(ShapeRenderer& _shapes, FontRenderer& _text, Neuron::MeshRenderer& _meshes)
{
  m_hits.clear();
  // Cleared with the hit list rather than inside the rail, because the digest's buttons fill under
  // the pointer too and the rail is drawn after them (ADR-110).
  m_hoverRegions.clear();

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
                       .animationSeconds = m_animationSeconds,
                       .sheetOpen = m_panel != Panel::None};

  const std::vector<MapHit> mapHits = Lockstep::DrawMap(_shapes, _text, _meshes, frame);

  // **`RESET` is drawn only when the camera is somewhere other than where the map opened**
  // (ADR-090). There has been no way back to the authored framing since the map got a camera
  // (ADR-017) -- `ResetView` existed and nothing called it -- and hunting for it by eye is not a
  // thing to ask. A control that would do nothing is left off the screen rather than drawn dim,
  // because the map pane has no chrome and one chip appearing is itself the signal.
  if (!m_mapView.AtAuthoredFraming())
  {
    const float chipX =
      Frame::DIGEST_WIDTH + 12.0F + static_cast<float>(FontRenderer::MeasurePixels(FocusLine(m_state, m_focusedSystem))) + 10.0F;
    const float chipY = Frame::TOP_BAR_HEIGHT + 8.0F;
    const auto chipWidth = static_cast<float>(FontRenderer::MeasurePixels("RESET")) + 12.0F;
    // Drawn at the chip height the map's other chrome uses, hit at the floor around it (ADR-100):
    // it stands alone in the corner of a pane with nothing to overlap.
    constexpr float CHIP_HEIGHT = 22.0F;
    _shapes.StrokeRect(chipX, chipY, chipWidth, CHIP_HEIGHT, Ink::OUTLINE);
    _text.DrawText(static_cast<std::int32_t>(chipX) + 6, CenterTextY(chipY, CHIP_HEIGHT), "RESET", Ink::TEXT_MUTED);
    AddHit(chipX - (TOUCH_FLOOR - chipWidth) * 0.5F, chipY - (TOUCH_FLOOR - CHIP_HEIGHT) * 0.5F, std::max(chipWidth, TOUCH_FLOOR),
           TOUCH_FLOOR, Action::ResetCamera, 0);
  }

  for (const MapHit& hit : mapHits)
  {
    // **A garrison badge opens the same sheet the disc under it opens** (ADR-079, ADR-111), which
    // answers ADR-079's open question: the badge followed the rail's rule and went focus-only at
    // the lock while the disc beside it opened a sheet, and they were two rules because they
    // opened two different things. They open one sheet now, so they behave alike -- at the lock it
    // stays open and goes inert like any other (ADR-065). The two targets remain two, because they
    // still name two things: the system, and the ships standing on it.
    if (hit.fleetsAt != EventRefs::NONE)
    {
      AddHit(hit.x, hit.y, hit.width, hit.height, Action::OpenFleetsAt, hit.fleetsAt);
      continue;
    }

    if (hit.system != EventRefs::NONE)
    {
      AddHit(hit.x, hit.y, hit.width, hit.height, Action::OpenSystem, hit.system);
      continue;
    }

    // A marker on a lane is either a move ordered this tick, drawn at progress zero until the lock
    // (ADR-055), or a fleet the server already has in transit. Only the first takes an order
    // (ADR-077); the second focuses where it is going, which is what a marker is asked about once
    // there is nothing to decide about it.
    const bool underWay = hit.fleet >= 0 && hit.fleet < static_cast<std::int32_t>(m_state.fleets.size()) &&
                          m_state.fleets[static_cast<std::size_t>(hit.fleet)].underWay;
    const std::int32_t destination = underWay ? m_state.fleets[static_cast<std::size_t>(hit.fleet)].to : hit.fleet;
    AddHit(hit.x, hit.y, hit.width, hit.height, underWay ? Action::FocusSystem : Action::OpenFleet, destination);
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

  // **`REPLAY` is behind `--dev` until screen 07 is wired** (ADR-091). Its own sheet is titled
  // `REPLAY TICK 7 - NOT YET WIRED`, which is a control teaching a player that the buttons on this
  // screen may do nothing -- the exact lesson ADR-053 and ADR-077 were spent unteaching. It stays on
  // the bar for whoever is building it.
  if (m_developerControls)
  {
    const std::string replayLabel = std::format("REPLAY T{}", m_state.match.tick);
    const float replayWidth = 10.0F + 7.0F + 6.0F + static_cast<float>(FontRenderer::MeasurePixels(replayLabel)) + 10.0F;
    const float replayX = cursor - replayWidth;
    _shapes.StrokeRect(replayX, 13.0F, replayWidth, 22.0F, Ink::OUTLINE);
    // The one glyph the font does not have and does not need: the replay triangle is geometry, as
    // it is in the reference (README "Assets").
    _shapes.FillTriangle(replayX + 10.0F, 19.0F, replayX + 17.0F, 24.0F, replayX + 10.0F, 29.0F, Ink::TEXT_PRIMARY);
    _text.DrawText(static_cast<std::int32_t>(replayX + 23.0F), centered, replayLabel, Ink::TEXT_PRIMARY);
    // An isolated chip with the bar's own margin around it, so the HIT is the bar and the chip stays
    // 22 (ADR-100). The bar is exactly the floor tall, which is where the number came from.
    AddHit(replayX, 0.0F, replayWidth, Frame::TOP_BAR_HEIGHT, Action::OpenReplay, 0);
    cursor = replayX - 14.0F;

    _shapes.FillRect(cursor, 13.0F, 1.0F, 22.0F, Ink::CARD_BORDER);
    cursor -= 15.0F;
  }

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

  // **The chip says whether the place MOVED, not only what it is** (ADR-091). Placement is the
  // anti-snowball's whole instrument -- the one-pager makes the score public so a player can tell
  // they are falling behind -- and a number that is the same ink at 1st and at 6th says only where
  // you are, never that you are sliding.
  //
  // Blue when you lead, amber when you have dropped since the last digest this client drew, and the
  // ordinary outline otherwise. `m_placementDrawn` is session memory: a client that joins mid-match
  // has no previous place, and the chip is simply not amber until it has drawn one.
  const bool leading = m_state.player.placement == 1;
  const bool slipped = m_placementDrawn != 0 && m_state.player.placement > m_placementDrawn;
  const Color chipInk = leading ? Ink::BLUE : (slipped ? Ink::AMBER : Ink::OUTLINE);
  const Color chipText = leading ? Ink::BLUE : (slipped ? Ink::AMBER : Ink::TEXT_PRIMARY);

  const std::string placement = std::format("{} / {}", FormatPlacement(m_state.player.placement), m_state.player.playerCount);
  const float chipWidth = static_cast<float>(FontRenderer::MeasurePixels(placement)) + 14.0F;
  _shapes.StrokeRect(cursor - chipWidth, 14.0F, chipWidth, 20.0F, chipInk);
  _text.DrawText(static_cast<std::int32_t>(cursor - chipWidth + 7.0F), centered, placement, chipText);
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
  //
  // **And what this tick has already committed, right after it** (ADR-087). `46 CR` beside a sheet
  // refusing a 30 CR build is the contradiction a player actually hits: both numbers are right, and
  // the 20 between them was only ever visible in the locks rail on the other side of the screen.
  // `46 CR −20` carries the whole arithmetic in the place the bigger number is read.
  //
  // Drawn right to left like everything else on this bar, so the committed amount is composed first
  // and sits outermost -- it is the qualifier, and the purse is what it qualifies.
  const std::uint32_t committed = m_state.orders.QueuedBuildCost();
  if (committed > 0)
  {
    const std::string spent = std::format("−{}", committed);
    DrawRight(_text, cursor, centered, spent, Ink::BLUE);
    cursor -= static_cast<float>(FontRenderer::MeasurePixels(spent)) + 6.0F;
  }

  const std::string credits = std::format("{} CR", m_state.player.credits);
  DrawRight(_text, cursor, centered, credits, Ink::TEXT_PRIMARY);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(credits)) + 14.0F;

  _shapes.FillRect(cursor, 13.0F, 1.0F, 22.0F, Ink::CARD_BORDER);
  cursor -= 15.0F;

  // The countdown is the one thing on the bar in the 16px cut, and it is amber because amber is the
  // warning colour: this is the deadline every order on the rail is racing (README "Frame").
  const std::string countdown = m_state.match.finished ? std::string{"--:--:--"} : FormatCountdown(m_state.match.secondsToLock);
  const std::int32_t bigY = CenterTextY(0.0F, Frame::TOP_BAR_HEIGHT, Face::MonoDisplay);

  // **Amber is the deadline colour, and at zero there is no deadline left to warn about** (screen
  // 06). A countdown that stayed amber on 00:00:00 read as "hurry" to a player who could no longer
  // do anything, which is the opposite of what the number means once it has run out.
  const bool atLock = m_state.orders.locked && !m_state.match.finished;
  DrawRight(_text, cursor, bigY, countdown, atLock ? Ink::NEUTRAL_DIM : Ink::AMBER, Face::MonoDisplay);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(countdown, Face::MonoDisplay)) + 8.0F;

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
  const std::string census = std::format("{} PLAYERS · {} SYSTEMS", m_state.player.playerCount, m_state.totalSystems);
  // **No `M<id>`** (ADR-091). The snapshot carries no match id, so `SnapshotView` was filling it
  // with the zero-padded TICK -- a four-digit number beside `D3/21` and `T9 LOCKS` that names
  // neither the match nor the tick, and changes every tick while looking like an identifier.
  const std::string stem = std::format("D{}/{}", m_state.match.day, m_state.match.totalDays);

  std::vector<std::string> candidates;
  if (!m_state.match.endsAt.empty())
  {
    candidates.push_back(std::format("{} · {} · ENDS {}", stem, census, m_state.match.endsAt));
  }
  candidates.push_back(std::format("{} · {}", stem, census));
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
  layout.height = 11.0F + static_cast<float>(TITLE_LINE_HEIGHT) + 2.0F + static_cast<float>(layout.details.size()) * lines + 4.0F;
  if (layout.hasVerdict)
  {
    layout.height += 4.0F + (1.0F + static_cast<float>(layout.verdictDetail.size())) * lines + 6.0F;
  }
  // **The action row is a BUTTON tall plus its two gaps** (ADR-100, ADR-110). It reserved one
  // `LINE_HEIGHT` while a button was 18 and centred on that line's baseline, which was near enough
  // to true to go unnoticed; at 44 the button reached a whole line above its row and painted over
  // the detail line there. A row that reserves less than it draws is the defect `LINE_HEIGHT` was
  // introduced for, one control further on. The gaps are reserved rather than borrowed from the
  // line above, because they are what the grown 44-pixel target reaches into.
  if (layout.hasActions)
  {
    layout.height += BUTTON_GAP + BUTTON_HEIGHT + BUTTON_GAP;
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
                   std::format("SINCE YOU LOOKED · T{} → T{}", m_state.lastSeenTick, m_state.match.tick), Ink::TEXT_MUTED);

    const std::string chip = std::format("{} TICKS", m_state.unreadTicks);
    const float chipWidth = static_cast<float>(FontRenderer::MeasurePixels(chip)) + 14.0F;
    _shapes.StrokeRect(Frame::DIGEST_WIDTH - RAIL_PADDING - chipWidth, BandTopForText(headerY, 18.0F), chipWidth, 18.0F, Ink::AMBER);
    _text.DrawText(static_cast<std::int32_t>(Frame::DIGEST_WIDTH - RAIL_PADDING - chipWidth + 7.0F), headerY, chip, Ink::AMBER);
  }
  else
  {
    _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), headerY, std::format("DIGEST - TICK {}", m_state.match.tick), Ink::TEXT_MUTED,
                   Face::MonoDisplay);

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
      // **A loss is red, and what marks one is a true minus** -- three bytes of UTF-8, not the
      // ASCII hyphen this compared against until 2026-09-13. `front() == '-'` went on compiling
      // when the character changed and silently stopped finding a single negative, which is the
      // failure mode a byte comparison against text has.
      _text.DrawText(static_cast<std::int32_t>(cellX), cellY, delta.cells[index].text, delta.cells[index].loss ? Ink::RED : Ink::AMBER);
    }
    y += boxHeight + 6.0F;

    // **What the backlog could not carry** (ADR-094). The server keeps the last `DIGEST_HISTORY`
    // ticks per player and sends the whole of it on arrival (ADR-044); a player who was away longer
    // gets that window and no warning that anything fell off the front of it. The delta box's
    // counts are honest about the ticks it HAS, which is exactly what makes the gap invisible.
    if (m_state.unreadTicks > DIGEST_HISTORY_TICKS)
    {
      _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), static_cast<std::int32_t>(y), "Older ticks were not kept.", Ink::NEUTRAL_DIM,
                     Face::SansRegular);
      y += static_cast<float>(LINE_HEIGHT) + 6.0F;
    }
  }

  // ---- The cards, and which of them are on the screen -------------------------------------------------
  //
  // **The column scrolls, by whole cards** (ADR-080, which took the digest out of ADR-052 option C).
  // The whole stack is measured first, because a card is the unit that scrolls and the only way to
  // know where one ends is to have worked out how tall it is -- which is also what paging needed
  // (ADR-061), so the measuring loop is unchanged and only what is done with it moved.
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

  // **Clamped so the last card is always reachable and never alone past the end.** A scroll
  // position is a card index and the player can push it anywhere; what stops it running off is
  // that a top with nothing under it is not a position, it is an empty column.
  const std::size_t lastTop = cards.empty() ? 0 : cards.size() - 1;
  m_digestTop = std::min(m_digestTop, lastTop);

  // What fits from here. One card always goes in even when it is taller than the column: a card
  // that fits nowhere is still better read cut off than not drawn at all.
  const std::size_t firstCard = m_digestTop;
  std::size_t lastCard = firstCard;
  float used = 0.0F;
  while (lastCard < layouts.size() && (lastCard == firstCard || used + layouts[lastCard].height <= room))
  {
    used += layouts[lastCard].height;
    ++lastCard;
  }
  m_cardsOnScreen = lastCard - firstCard;

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
    // The display cut (ADR-084): a card's title is what the card IS, and until there were two sizes
    // it was separated from the sentences under it by a weight step nobody could see at a glance.
    //
    // **And NOT shouted** (ADR-099). `Uppercased()` was here, over a detail line reading *7 of 10
    // lost (defending)*: a shout and a sentence about the same event. The titles are the longest
    // strings on the screen and uppercase is the least legible case for a long string, which the
    // display cut's 16px made louder rather than clearer. They are authored in sentence case in
    // `GameLogic` -- `Battle at Ulme`, `Shipyard L1 rising at Dothan` -- and this is the one place
    // that was shouting them.
    //
    // **The face is still MONO, and that is a constraint rather than a decision** (ADR-102): the
    // display cut is baked from Plex Mono only (`Font.h`, ADR-073), so a sentence-cased title at
    // this size has nowhere sans to go. ADR-074's rule is bent here and the ADR says where.
    _text.DrawText(static_cast<std::int32_t>(TEXT_LEFT), lineY, card.title, Ink::TEXT_PRIMARY, Face::MonoDisplay);
    if (!card.stamp.empty())
    {
      DrawRight(_text, Frame::DIGEST_WIDTH - RAIL_PADDING, lineY, card.stamp, Ink::TEXT_MUTED);
    }

    // The title of an actor card opens and closes it. Registered here rather than after the card's
    // own region, so that it wins: the region is INSERTED at `cardHitsBegin` below, which puts
    // everything added during the card in front of it in the reverse walk `HandleTap` makes.
    if (layout.collapsible)
    {
      AddHit(0.0F, top, Frame::DIGEST_WIDTH - 1.0F, TOUCH_FLOOR, Action::ToggleActorCard, card.actor);
    }
    lineY += TITLE_LINE_HEIGHT + 2;

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
      const float boxTop = static_cast<float>(lineY) - VERDICT_BOX_PADDING;
      const float boxHeight = static_cast<float>(1 + detail.size()) * static_cast<float>(LINE_HEIGHT) + 2.0F * VERDICT_BOX_PADDING;
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
      // **A gap above and below, and the hit fills both** (ADR-110). A button is 28 and a target is
      // 44, so the eight pixels either side are what the grown rectangle reaches into -- which is
      // why they are reserved here rather than left as whatever the line before happened to leave.
      lineY += static_cast<std::int32_t>(BUTTON_GAP);
      float buttonX = TEXT_LEFT;

      const auto buttonY = static_cast<float>(lineY);

      for (const EventAction& action : card.actions)
      {
        // `committed` is "this is already in the orders this tick goes in with", which two kinds of
        // control can be and the rest cannot. It is the same state a queued tile and a queued rail
        // row wear, and it is still a target, because every order is editable until the lock.
        //
        // A build has two more states and the NUMBER SEGMENT says which (ADR-053, ADR-110): queued,
        // so the next tap is known to take it back; or beyond the purse, dashed and dim with what is
        // missing, because the lock would refuse it and a refusal a tick later is the worst way to
        // learn a price.
        Button button{.label = action.label, .number = action.number};
        bool committed = false;
        bool unaffordable = false;
        if (action.kind == EventActionKind::QueueBuild)
        {
          committed = std::ranges::find(m_state.orders.queuedBuilds, action.target) != m_state.orders.queuedBuilds.end();
          unaffordable = !committed && !m_state.CanAffordBuild(action.target);
          if (committed && action.target >= 0 && action.target < static_cast<std::int32_t>(m_state.orders.builds.size()))
          {
            button.number = std::format("−{}", m_state.orders.builds[static_cast<std::size_t>(action.target)].cost);
          }
          else if (unaffordable)
          {
            button.number = std::format("NEED {} MORE", BuildShortfall(action.target));
            button.moneyReason = true;
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
            button.label = answered->accepted ? "ACCEPTED" : "DECLINED";
            committed = true;
          }
        }

        // **A focus chip is never inert and never locked** (ADR-081): it takes the eye somewhere and
        // reaches no wire, so it goes on working while every order on the screen is frozen.
        const bool focusOnly = action.kind == EventActionKind::Focus;
        button.state = ControlState::Outlined;
        if (unaffordable)
        {
          button.state = ControlState::Inert;
        }
        else if (committed)
        {
          button.state = ControlState::Committed;
        }
        else if (!OrdersEditable() && !focusOnly)
        {
          button.state = ControlState::Locked;
        }
        else if (action.primary && OrdersEditable())
        {
          button.state = ControlState::Primary;
        }

        // **Wide enough for a finger as well as for its label** (ADR-100, ADR-110). The box is what
        // the two cells need; the target is grown around it, because a row of buttons with gaps
        // between them is the isolated-chip case rather than the column one.
        float width = ButtonWidth(button);
        if (buttonX + width > Frame::DIGEST_WIDTH - RAIL_PADDING)
        {
          break;
        }

        const bool hovered = button.state != ControlState::Inert && button.state != ControlState::Locked && m_pointerXPixels >= buttonX &&
                             m_pointerXPixels < buttonX + width && m_pointerYPixels >= buttonY &&
                             m_pointerYPixels < buttonY + BUTTON_HEIGHT;

        // **A committed control says what the next tap does while the finger is on it** (ADR-110),
        // so taking an order back is never a surprise. The width is remeasured, because `TAKE BACK`
        // is not the width of the label it replaces -- and it is clamped to the resting width, so a
        // button under the pointer never pushes the one beside it along.
        if (hovered && button.state == ControlState::Committed && !focusOnly)
        {
          Button flipped = button;
          flipped.label = "TAKE BACK";
          if (action.kind == EventActionKind::QueueBuild && action.target >= 0 &&
              action.target < static_cast<std::int32_t>(m_state.orders.builds.size()))
          {
            flipped.number = std::format("+{}", m_state.orders.builds[static_cast<std::size_t>(action.target)].cost);
          }
          if (ButtonWidth(flipped) <= width)
          {
            button = flipped;
          }
        }

        DrawButton(_shapes, _text, buttonX, buttonY, width, button,
                   ControlInkFor(button.state, ControlKind::Button, hovered, button.moneyReason));

        if ((OrdersEditable() && !unaffordable) || focusOnly)
        {
          const Frame::Box target = Frame::GrownToFloor(buttonX, buttonY, width, BUTTON_HEIGHT);
          const DigestTarget destination = TargetOf(action);
          AddHit(target.x, target.y, target.width, target.height, destination.action, destination.index);
          m_hoverRegions.push_back(HoverRegion{buttonX, buttonY, width, BUTTON_HEIGHT});
        }
        buttonX += width + BUTTON_GAP;
      }
      lineY += static_cast<std::int32_t>(BUTTON_HEIGHT + BUTTON_GAP);
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
  // At the foot of the column, where the stack it is about ends. **It says what is hidden and what
  // the worst of it is** (ADR-080): `1 / 4 - MORE >` told a player how much column was left and
  // nothing at all about whether the battle they had not seen was in it. `< PREV` appears only once
  // there is something above, so the band never offers a direction that does nothing.
  if (paged)
  {
    const float bandY = Frame::SCREEN_HEIGHT - DIGEST_PAGE_HEIGHT;
    const std::int32_t bandText = CenterTextY(bandY, DIGEST_PAGE_HEIGHT);
    _shapes.FillRect(0.0F, bandY, Frame::DIGEST_WIDTH - 1.0F, 1.0F, Ink::DIVIDER);

    if (m_digestTop > 0)
    {
      _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), bandText, "‹ PREV", Ink::TEXT_MUTED);
      AddHit(0.0F, bandY, Frame::DIGEST_WIDTH * 0.5F, DIGEST_PAGE_HEIGHT, Action::ShowDigestPage,
             static_cast<std::int32_t>(PreviousDigestTop(layouts, room)));
    }

    const std::string hidden = HiddenSummary(cards, lastCard);
    DrawRight(_text, Frame::DIGEST_WIDTH - RAIL_PADDING, bandText, hidden.empty() ? std::string{"END"} : hidden + " ›",
              hidden.empty() ? Ink::NEUTRAL_DIM : Ink::TEXT_MUTED);
    if (!hidden.empty())
    {
      AddHit(Frame::DIGEST_WIDTH * 0.5F, bandY, Frame::DIGEST_WIDTH * 0.5F, DIGEST_PAGE_HEIGHT, Action::ShowDigestPage,
             static_cast<std::int32_t>(lastCard));
    }
  }
}

std::size_t MainPage::PreviousDigestTop(const std::vector<CardLayout>& _layouts, float _room) const
{
  // A screenful backwards, measured the way a screenful forwards is measured: cards are different
  // heights, so "one page" is however many of them fit and not a fixed number (ADR-061, ADR-080).
  std::size_t top = m_digestTop;
  float used = 0.0F;
  while (top > 0)
  {
    const float height = _layouts[top - 1].height;
    if (used > 0.0F && used + height > _room)
    {
      break;
    }
    used += height;
    --top;
  }
  return top;
}

MainPage::DigestTarget MainPage::TargetOf(const EventAction& _action) const noexcept
{
  switch (_action.kind)
  {
  case EventActionKind::RedirectFleet:
  {
    // **No digest control places an order any more; each of them opens the place the order is
    // about** (ADR-111). A move is given on the map from the sheet for the system the fleet is
    // standing on, so the index this screen needs is that SYSTEM and not the fleet the digest
    // named -- which is the whole reason a digest button's action and its index are chosen
    // together (ADR-057).
    const bool known = _action.target >= 0 && _action.target < static_cast<std::int32_t>(m_state.fleets.size());
    return DigestTarget{Action::OpenSystem, known ? m_state.fleets[static_cast<std::size_t>(_action.target)].from : EventRefs::NONE};
  }
  case EventActionKind::QueueBuild:
  {
    const bool known = _action.target >= 0 && _action.target < static_cast<std::int32_t>(m_state.orders.builds.size());
    return DigestTarget{Action::OpenSystem,
                        known ? PositionOfSystem(m_state, m_state.orders.builds[static_cast<std::size_t>(_action.target)].system)
                              : EventRefs::NONE};
  }
  case EventActionKind::AcceptProposal:
    return DigestTarget{Action::AcceptProposal, _action.target};
  case EventActionKind::DeclineProposal:
    return DigestTarget{Action::DeclineProposal, _action.target};
  case EventActionKind::Focus:
  default:
    // A focus chip carries the system it points at, not the event it sits on.
    return DigestTarget{Action::FocusSystem, _action.target};
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

  /// A sub-band inside a section: a muted label over the rows it groups, never a target (ADR-086).
  /// Lighter than `section` -- no rule and no count -- because it divides a list rather than
  /// starting one.
  const auto band = [&](std::string_view _label)
  {
    if (visible(static_cast<float>(LINE_HEIGHT) + 2.0F))
    {
      _text.DrawText(static_cast<std::int32_t>(contentX), static_cast<std::int32_t>(y), _label, Ink::TEXT_MUTED);
    }
    y += static_cast<float>(LINE_HEIGHT) + 2.0F;
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

  // **Grouped by where they are** (ADR-086). Ten rows reading `FLT 13 10 HOLD HOLLIS` is the
  // system name repeated ten times, the word `HOLD` repeated ten times, and two bare numbers at
  // equal weight -- so the one thing a player is scanning for, which of their systems is strong, is
  // the thing the column says least clearly. The system goes on a band and the rows under it carry
  // what differs.
  //
  // **A FLEETS row opens the picker, and for a standing fleet it is the only thing that does**
  // (ADR-077). The map draws no marker for a fleet that is not moving, and the digest's `MOVE` is a
  // standing move offered only when nothing else can be acted on (ADR-056). A fleet already on a
  // lane opens nothing, because the lock would refuse a second order on it, and focuses where it is
  // going instead. At the lock every row focuses (ADR-060).
  const auto fleetRow = [&](std::size_t _index, std::string_view _tail, std::string_view _status, const Color& _statusColor)
  {
    const Fleet& fleet = m_state.fleets[_index];
    const std::string name = Uppercased(fleet.name);
    const bool orderable = !navigateOnly && !fleet.underWay;
    row(std::format("{} · {}{}", name, fleet.ships, _tail), _status, _statusColor, orderable ? Action::OpenFleet : Action::FocusSystem,
        orderable ? static_cast<std::int32_t>(_index) : fleet.to, name.size());
  };

  // The systems holding something of yours, in the order the snapshot listed the fleets: stable
  // between two frames of one state, which is what stops the column reordering under a finger.
  std::vector<std::int32_t> standingAt;
  for (const Fleet& fleet : m_state.fleets)
  {
    if (fleet.owner == m_state.viewer && !fleet.OnALane() && std::ranges::find(standingAt, fleet.to) == standingAt.end())
    {
      standingAt.push_back(fleet.to);
    }
  }

  for (const std::int32_t at : standingAt)
  {
    std::uint32_t ships = 0;
    for (const Fleet& fleet : m_state.fleets)
    {
      ships += fleet.owner == m_state.viewer && !fleet.OnALane() && fleet.to == at ? fleet.ships : 0U;
    }
    band(std::format("{} · {} {}", NameOfSystem(m_state, at), ships, ships == 1 ? "SHIP" : "SHIPS"));

    for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
    {
      const Fleet& fleet = m_state.fleets[index];
      if (fleet.owner != m_state.viewer || fleet.OnALane() || fleet.to != at)
      {
        continue;
      }

      // **No right-hand column on a holding row**, because the band above it already said where and
      // `HOLD` said nothing else. The one exception is the fact that is not implied by standing
      // still: that this fleet is the incumbent and fights with the defender's bonus.
      const bool incumbent = fleet.status.find("incumbent") != std::string::npos;
      fleetRow(index, std::string_view{}, incumbent ? "+DEF" : std::string_view{}, incumbent ? Ink::BLUE : Ink::TEXT_MUTED);
    }
  }

  // Everything in transit under one band, because where they are is a lane rather than a place and
  // the thing they have in common is that none of them can be ordered.
  const bool anyUnderWay = std::any_of(m_state.fleets.begin(), m_state.fleets.end(),
                                       [this](const Fleet& _fleet) { return _fleet.owner == m_state.viewer && _fleet.OnALane(); });
  if (anyUnderWay)
  {
    band("UNDER WAY");
    for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
    {
      const Fleet& fleet = m_state.fleets[index];
      if (fleet.owner != m_state.viewer || !fleet.OnALane())
      {
        continue;
      }
      fleetRow(index, std::format(" → {}", NameOfSystem(m_state, fleet.to)), std::format("T{}", fleet.eta), Ink::TEXT_MUTED);
    }
  }

  // ---- BUILDS ------------------------------------------------------------------------------------
  //
  // The purse on the header and the price on every queued row, so the column adds up in front of
  // the player (ADR-053): what is queued, what it takes, and what is left when the clock hits zero.
  section("BUILDS", std::format("{} AVAIL · {} CR", m_state.orders.availableBuilds, m_state.player.credits));

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
      row(Uppercased(build.title), std::format("QUEUED −{}", build.cost), Ink::BLUE, buildAction, at);
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

  // What tapping a row does. It differs per panel, and it used to not exist: every row went to
  // `ChooseDestination`, so the BUILD panel listed two things a player could not tap.
  Action rowAction = Action::ChooseDestination;

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
      row.action = !OrdersEditable() ? Action::None : (ordered ? Action::CancelFleetOrder : Action::OpenFleet);
      row.target = index;
      fleets.push_back(std::move(row));
    }
    fleetCount = fleets.empty() ? std::string{} : (shipsHere == 1 ? std::string{"1 SHIP"} : std::format("{} SHIPS", shipsHere));
    break;
  }
  case Panel::Destination:
  {
    const Fleet& fleet = m_state.fleets[static_cast<std::size_t>(m_panelSubject)];
    title = std::format("MOVE {} - PICK LANE", Uppercased(fleet.name));
    // Where it stands, which is what `Action::ChooseDestination` orders from (ADR-077).
    const std::int32_t origin = fleet.from;

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

      // **A held candidate says whose it is in their own colour** (ADR-092). The row's owner square
      // already carries it; the line that says `P3 · 11 +DEF` is the one being read while the
      // decision is made, and in body ink it reads as a number rather than as a rival.
      rows.push_back(
        SheetRow{.title = Uppercased(node.name),
                 .detail = StandingAt(m_state, other),
                 .right = std::format("{} · ETA T{}", lane.cost == 1 ? std::string{"1 TICK"} : std::format("{} TICKS", lane.cost),
                                      m_state.OrdersTick() + lane.cost - 1),
                 .accent = OwnerColor(node.owner, m_state.viewer),
                 .target = !OrdersEditable() ? EventRefs::NONE : other,
                 .sortBy = lane.cost,
                 .detailInk = node.owner == NOBODY ? Ink::TEXT_MUTED : OwnerColor(node.owner, m_state.viewer)});
    }

    // **Nearest first, then by name** (ADR-092). The picker was in lane order, which is the order
    // the graph happens to store them in and means nothing to a player; how soon a fleet lands is
    // the first thing they weigh, and two lanes of the same length sort by name so the list does not
    // reshuffle between two frames of one state.
    std::ranges::stable_sort(rows,
                             [](const SheetRow& _a, const SheetRow& _b)
                             {
                               if (_a.sortBy != _b.sortBy)
                               {
                                 return _a.sortBy < _b.sortBy;
                               }
                               return _a.title < _b.title;
                             });
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
