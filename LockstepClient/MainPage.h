#pragma once

// **Included by the HEADER, not only the .cpp**, since 2026-09-14: the page's layout constants are
// derived from `Frame::TOUCH_FLOOR`, which is a rule about every screen rather than a number this
// page owns (ADR-100). It costs a colour list and two helpers, which this page's .cpp already had.
#include "Controls.h"
#include "DesignTokens.h"
#include "FontRenderer.h"
#include "MapView.h"
#include "MeshRenderer.h"

#include "Starfield.h"
#include "KeyboardInput.h"
#include "MatchState.h"
#include "PointerInput.h"
#include "ShapeRenderer.h"

#include <optional>

namespace Lockstep
{

/// Ranked and grouped by `DigestView.h`, which this header does not include: what a card IS belongs
/// to the digest, and all this screen needs to say is that it lays one out.
struct DigestCard;

/// The single screen of Lockstep: digest, map, locks.
///
/// It is the screen a player opens once or twice a day (Design/Screens/README.md). The three
/// panes are not three features -- they are one loop: read what changed, look at where it
/// happened, adjust what you have committed for the next tick.
///
/// IT DRAWS ITSELF EVERY FRAME AND KEEPS ALMOST NO STATE. Layout is recomputed from MatchState on
/// each Draw, and the tappable rectangles are recorded as they are drawn. That is the immediate
/// mode this codebase has no widget tree for, and it suits a screen whose content is a digest
/// that is replaced wholesale every tick: there is no retained node that could disagree with the
/// state behind it.
///
/// The only state it does keep is what the PLAYER has done and the server has not seen yet: which
/// node the digest focused, which panel is open, and the orders they have edited but not locked.
///
/// **Defined across one translation unit per pane** -- `MainPage.cpp` says which -- with the control
/// vocabulary in `Controls.h` (ADR-110) and what the units share in `MainPageParts.h`.
class MainPage
{
public:
  /// Fixed 1280x720, and the three-column grid inside it. Every number below is from
  /// Design/UI/DESIGN-GUIDELINES.md "Frame": rows `44 | fill`, columns `400 | fill | 260`.
  ///
  /// **The digest is where orders are given** -- every event carries its own actions -- so it needs
  /// the width for buttons; the right rail is a read-only summary of what goes in at the lock and
  /// has no controls (ADR-034).
  static constexpr float TOP_BAR_HEIGHT = 44.0F;
  static constexpr float DIGEST_WIDTH = 400.0F;
  static constexpr float ORDERS_WIDTH = 260.0F;
  static constexpr float RAIL_PADDING = 14.0F;
  static constexpr float CARD_PADDING = 10.0F;
  /// A line of text, baseline to baseline, asked of the FONT rather than stated here.
  ///
  /// **It was 12 in five files until 2026-09-13** -- line-height 1.5 on a font whose every glyph was
  /// eight pixels tall (ADR-014). Plex at 12px has an ascent of 13 and a descent of 4, so a 12px
  /// advance set a 17px box: consecutive lines overlapped, and on the digest rail a card's detail
  /// line was drawn through the button under it. A number that has to be re-derived in five places
  /// every time the face changes is a number that will be re-derived in four.
  static constexpr std::int32_t LINE_HEIGHT = static_cast<std::int32_t>(Neuron::FontRenderer::LineHeightPixels());

  /// The 16px display cut's line, for the one string on a card that is set in it (ADR-084). Asked of
  /// the font for the same reason `LINE_HEIGHT` is: a title in a bigger cut over a line box sized
  /// for the smaller one is the overlap that number was introduced to stop.
  static constexpr std::int32_t TITLE_LINE_HEIGHT =
    static_cast<std::int32_t>(Neuron::FontRenderer::LineHeightPixels(Neuron::Face::MonoDisplay));

  /// **The touch floor** (ADR-098, ADR-100). Every tappable rectangle on every screen is at least
  /// this in both dimensions, and `TouchTargetTests` walks the recorded hit list to say so -- reading
  /// the constants is not evidence, because a control's box is composed from several of them and the
  /// one that goes wrong is the one nobody added up.
  ///
  /// **How a control reaches it depends on what it sits beside.** A target in a COLUMN OF SIBLINGS
  /// grows its box -- a rail row, a card button, a sheet row -- because a row drawn at 21 and
  /// tappable at 44 has boundaries a finger cannot see and two neighbours would overlap. An ISOLATED
  /// CHIP with space around it grows only its HIT: the map's garrison badge, the top bar's replay
  /// chip. A 44px badge beside a system name would be a different map, not a bigger box.
  static constexpr float TOUCH_FLOOR = Frame::TOUCH_FLOOR;

  /// A button's BOX, and the gap that carries it to the touch floor (ADR-110).
  ///
  /// **28 drawn and 44 tapped**, which is the isolated-chip half of ADR-100 rather than the column
  /// half: a row of buttons under a card's last line is not a column of siblings, so growing the
  /// box to 44 spent sixteen pixels of every card on ascender room a shouted label never uses. The
  /// gap is what makes the grown hit safe -- 28 + 8 + 8 is exactly 44, so a hit centred on one
  /// button reaches the middle of the gap and no further, and two neighbours cannot overlap.
  static constexpr float BUTTON_HEIGHT = 28.0F;
  static constexpr float BUTTON_GAP = 8.0F;

  /// Inside a button: the label segment's side padding, and the number segment's (ADR-110). The
  /// number is narrower because it is a number -- it is scanned rather than read, and the hairline
  /// or the shade beside it is doing the separating a wider gutter would otherwise have to.
  static constexpr float BUTTON_LABEL_PADDING = 10.0F;
  static constexpr float BUTTON_NUMBER_PADDING = 8.0F;

  /// The dash of the one dashed border in this client, which is what says *not a target*
  /// (ADR-110, `Ink::INERT_BORDER`). The same 3-on-3 the map's footprint ring is drawn with, so
  /// the tree has one dash pattern rather than two that nearly agree.
  static constexpr float INERT_DASH = 3.0F;
  static constexpr float INERT_GAP = 3.0F;

  /// The square that precedes a locked button's label (ADR-110). Six pixels, which is the level
  /// ladder's pip: a glyph this screen already draws at a size it already has.
  static constexpr float LOCK_GLYPH_SIZE = 6.0F;

  static constexpr float VERDICT_BOX_PADDING = 5.0F;

  /// How many ticks of digest the server keeps per player (`NeuronServer::Session::DIGEST_HISTORY`).
  ///
  /// **Stated here because the client cannot ask.** It does not link `NeuronServer` any more than it
  /// links `GameLogic`, and the number is not on the wire; what it is used for is one muted line
  /// admitting that a longer absence lost something (ADR-094). If the server's window changes and
  /// this does not, the line appears one tick early or late -- which is a cosmetic error about a
  /// cosmetic line, and the alternative is a wire field for a sentence.
  static constexpr std::uint32_t DIGEST_HISTORY_TICKS = 8;

  /// The digest's own two bands, both 22 pixels (ADR-061).
  ///
  /// **22 is what a label that is also a control costs on this screen** -- it is the section header
  /// on the locks rail, and the `SIGNALS` one has been a control since ADR-039. An actor card's
  /// title is the first of these and the page band at the foot of the column is the second, so the
  /// two things a player taps to see more of the digest are the same size as each other.
  /// An actor card's title band. It stays 22 on screen and its HIT is the floor: the band is the top
  /// of a card, so a taller box would push every card down by the height of a line nobody reads
  /// (ADR-100). The overlap falls on the card's own body region, which is inserted behind it.
  static constexpr float DIGEST_TITLE_HEIGHT = 22.0F;
  /// The page band at the foot of the column is a row in its own right and grows to the floor.
  static constexpr float DIGEST_PAGE_HEIGHT = TOUCH_FLOOR;

  /// The locks rail's page band, the same height as the digest's and for the same reason: it is a
  /// pair of controls, and a control is a touch target (ADR-100, ADR-101).
  static constexpr float RAIL_PAGE_HEIGHT = TOUCH_FLOOR;

  /// The sheet a panel is drawn as, anchored to the bottom of the map pane (ADR-052).
  ///
  /// **44 is the number that matters and the rest follow it.** It is the smallest target a finger
  /// hits reliably, and it is also the height of the top bar, so a sheet row and the bar read as
  /// the same unit of the frame. Six rows and the sheet still leaves over half the pane showing,
  /// which is the constraint the other direction: the map is what the choice is about.
  static constexpr float SHEET_MARGIN = 12.0F;
  static constexpr float SHEET_ROW_HEIGHT = 44.0F;
  static constexpr float SHEET_HEADER_HEIGHT = TOUCH_FLOOR;
  static constexpr float SHEET_ACTION_HEIGHT = TOUCH_FLOOR;
  /// A section band inside a sheet: a label over the rows under it, and **not a target**, which is
  /// why it is the one band that does not grow (ADR-064, ADR-100).
  static constexpr float SHEET_BAND_HEIGHT = 22.0F;
  /// The locks rail's section header, which IS a target on `SIGNALS` and so is a row like any other.
  static constexpr float RAIL_SECTION_HEIGHT = TOUCH_FLOOR;
  /// The row that says how many did not fit, which is shorter because nothing taps it.
  static constexpr float SHEET_CLIPPED_HEIGHT = 24.0F;
  static constexpr std::size_t SHEET_MAXIMUM_ROWS = 6;

  /// The build sheet's body, which is a GRID OF TILES where the other three sheets are a column of
  /// rows (ADR-107).
  ///
  /// **A build is a choice between four things of the same kind and a move is a choice between
  /// lanes**, which is why only this sheet changed: four tiles side by side can be compared at a
  /// glance, and four rows one under another are read in order. 96 is the height that takes an icon
  /// row, a detail line and a bottom line at the font's own 17px line without any of them being
  /// cramped, and it is more than twice the touch floor in the dimension a thumb is least accurate
  /// in.
  ///
  /// **Two columns, and the grid is inset by `CARD_PADDING` like everything else on the sheet** --
  /// so `(596 - 20 - 8) / 2` is 284, and a tile's text is 260 wide. The design reference drew the
  /// whole sheet at a 12-pixel inset and arrived at 282; this tree's sheet header, help line and
  /// rows are all at 10, and a grid inset two pixels further than the title above it reads as a
  /// mistake.
  ///
  /// Two rather than three because of the TOP LINE, which is the crowded one: an icon, ten pixels,
  /// the title, and a 24-pixel ladder against the right edge leaves 196 for the title, and the
  /// widest one the two built buildings can produce -- `Mining station L2 rising` -- measures 168.
  /// Three columns make a tile 186 and leave 98, so that title overruns by 70 into the tile beside
  /// it, which `ShapeRenderer` has no clip rectangle to prevent.
  static constexpr float SHEET_TILE_HEIGHT = 96.0F;
  static constexpr float SHEET_TILE_GAP = 8.0F;
  /// Above the first row of tiles and below the last, inside the sheet.
  static constexpr float SHEET_TILE_TOP = 4.0F;
  static constexpr float SHEET_TILE_BOTTOM = 12.0F;
  static constexpr std::size_t SHEET_TILE_COLUMNS = 2;
  /// Four roles, one tile each: mining station, shipyard, bastion, trade lane (blueprint §3). Two of
  /// them are drawn by nothing yet, and an EMPTY SLOT IS NOT DRAWN -- a two-row system is one row of
  /// two tiles and the sheet is `96 + 8` shorter.
  static constexpr std::size_t SHEET_TILE_SLOTS = 4;

  /// **How much of the map pane a sheet may take** (ADR-052): half of the 676-pixel pane, so more
  /// than half of it always stays map. It was a rule nothing measured against until the place sheet
  /// put a build grid and a fleet list in one sheet and came to 403 bare (ADR-111).
  static constexpr float SHEET_MAP_SHARE = 338.0F;

  /// The least a place sheet's scrolling body is given, whatever the help line above it costs.
  ///
  /// A band and one row of tiles, which is the smallest body that says anything: a sheet whose
  /// grid is scrolled out of sight is a sheet about a place with nothing on it. When the help line
  /// is long enough to push the total past `SHEET_MAP_SHARE`, the sentence wins and the sheet is
  /// taller than half the pane -- which is the one case ADR-052's rule is bent, and the sentence is
  /// there to say why nothing on the sheet can be ordered (ADR-065).
  static constexpr float SHEET_BODY_MINIMUM = SHEET_BAND_HEIGHT + SHEET_TILE_TOP + SHEET_TILE_HEIGHT;

  /// How many of a place's fleet rows are PINNED above the bottom bar when the body scrolls
  /// (ADR-111, following ADR-093's pinned concede). Two, because the pinned block must not become
  /// the sheet: at 22 + 44 + 44 it is already 110 of a 122-pixel minimum body.
  static constexpr std::size_t SHEET_PINNED_FLEETS = 2;

  /// The confirm strip's destination rows are a GRID (ADR-113), where every other sheet's body is a
  /// column. Two columns, because the rows are the FALLBACK for the map above them -- the primary
  /// way to choose is to tap a lit system -- so the strip stays short and the map stays open.
  static constexpr std::size_t STRIP_COLUMNS = 2;

  /// Inside a tile. **12 across and 10 down**, which is not `CARD_PADDING`: a tile is a box with a
  /// border, where a digest card is a region of a rail, so its ink has to clear a line rather than
  /// an edge. 10 + 22 + 10 + 17 + 10 + 17 + 10 is exactly 96, which is what fixes the vertical one.
  static constexpr float TILE_PADDING_X = 12.0F;
  static constexpr float TILE_PADDING_Y = 10.0F;

  /// A tile's own furniture: the icon, and one square of the level ladder beside its neighbour.
  static constexpr float TILE_ICON_SIZE = 22.0F;
  static constexpr float TILE_ICON_GAP = 10.0F;
  static constexpr float TILE_PIP_SIZE = 6.0F;
  static constexpr float TILE_PIP_GAP = 3.0F;
  /// The bar along a rising tile's bottom inside edge, showing the ticks that are in.
  static constexpr float TILE_PROGRESS_HEIGHT = 3.0F;

  /// How many levels a building has, which is how many squares a level ladder draws (ADR-069).
  ///
  /// **The one number about the RULES this screen states rather than reads**, and it is stated
  /// because a ladder is a picture of the whole curve: the snapshot carries the level a row would
  /// build and what that level costs, pays and takes, and nothing on the wire says how many there
  /// are in total. It agrees with `GameLogic/MatchRules.h`'s `BUILDING_LEVELS` by hand; if that
  /// moves, a ladder draws the wrong number of squares and nothing else breaks (ADR-107).
  static constexpr std::uint32_t BUILDING_LEVELS = 3;

  /// What a tap does. The screen has no free text and no chat, so this is the complete list of
  /// things a player can express on it (one-pager, "What it is not").
  enum class Action : std::uint8_t
  {
    /// **A rectangle that is not a control and consumes the tap anyway** -- a sheet's own
    /// background (ADR-111). A sheet is a modal and only its rows were ever targets, so a tap on
    /// the band between two of them fell through to the map underneath and opened a different
    /// sheet; a fleet marker drawn at progress zero sits under the very sheet the move was ordered
    /// from (ADR-055), which is where that was found. No control is ever recorded with this: the
    /// rails pass it to mean *not a target* and never add a hit for one.
    None,
    /// Focus the map on what this digest event is about. Its index is a DIGEST index.
    FocusEvent,
    /// Focus the map on one system. Its index is a SYSTEM position, which is why it is not
    /// `FocusEvent` (ADR-057): one action carrying two kinds of index is an action that reads the
    /// wrong array, and the bounds check turned that into a button that did nothing at all.
    FocusSystem,
    /// Open one system's place sheet (ADR-111). Its index is a SYSTEM position.
    OpenSystem,
    /// Take this fleet's move onto the map (ADR-113). Its index is a FLEET position.
    BeginMove,
    /// Open the place sheet from a garrison badge (ADR-079, ADR-111). Its index is a SYSTEM
    /// position, and it is a separate action from `OpenSystem` because the badge is a separate
    /// target from the disc it sits beside -- the disc is the system and the badge is the ships.
    OpenFleetsAt,
    /// Queue or unqueue a build. An order: local until the lock.
    ToggleBuild,
    /// Open the list of things this player could say to somebody (ADR-039).
    OpenSignals,
    /// Queue or unqueue one of them. Also an order, and it locks with the rest.
    ToggleSignal,
    /// Answer a proposal. Also an order, and it locks with the others.
    AcceptProposal,
    DeclineProposal,
    /// Open or close one actor card's per-event lines. Its index is an `OwnerId`, because that is
    /// what a card groups and the index a card sits at changes with the ranking.
    ToggleActorCard,
    /// Put the digest column's top at one card. Its index is a CARD position in the stack
    /// `CardsOf` composed, which is what the band's two halves carry (ADR-080).
    ShowDigestPage,
    /// Step through the last resolved tick.
    OpenReplay,
    /// Light one of the systems the map has offered, which is a SELECTION and not an order
    /// (ADR-113). Its index is a SYSTEM position. The order is `SendMove`.
    ChooseDestination,
    /// Queue the move the map is showing, and leave the mode.
    SendMove,
    /// Leave the mode with no order.
    CancelMove,
    /// Put the camera back where the map opened (ADR-090). Drawn only when it is somewhere else.
    ResetCamera,
    /// Move the locks rail by one bandful. Its index is a DIRECTION, +1 down and -1 up, and not a
    /// position: the rail scrolls in pixels and a tap that carried one would be a tap that had to
    /// know how tall the band came out (ADR-101).
    PageRail,
    /// Take back a fleet's queued move, leaving it standing where it is. Its index is a FLEET
    /// position (ADR-111). A build is taken back by `ToggleBuild`, which is the same tap on the
    /// other order kind -- two actions rather than one, because a build row and a fleet are
    /// different arrays and one index must mean one thing (ADR-057).
    CancelFleetOrder,
    ClosePanel
  };

  /// Which modal the screen is showing over the map, if any.
  enum class Panel : std::uint8_t
  {
    None,
    /// **One system, and everything it can do this tick** (ADR-111): what it can build, and the
    /// fleets standing on it. It replaces the build sheet and the fleet list, which were two
    /// sheets about one place reached through two different doors.
    Place,
    SignalList,
    Replay
  };

  /// Where a fleet may be sent this tick, and how long it takes to get there (ADR-113).
  ///
  /// **One lane and no further, because that is what the rules allow.** `Match::Validate` refuses
  /// any destination that is not one lane from where the fleet stands (`NoLaneToDestination`), so
  /// the handoff's "multi-hop within the fleet's range if the rules allow" resolves to the
  /// adjacent systems and nothing else.
  struct MoveTargetSystem
  {
    std::int32_t system = EventRefs::NONE;
    std::uint32_t ticks = 0;
    std::uint32_t arrivesAt = 0;
  };

  /// The move being chosen on the map, if one is (ADR-113).
  struct MoveMode
  {
    /// The fleet, as a POSITION in `m_state.fleets` and as the id that survives a snapshot: a
    /// position is not stable across one and an id is (ADR-057, ADR-065).
    std::int32_t fleet = EventRefs::NONE;
    std::int32_t fleetId = EventRefs::NONE;
    /// Where it is standing, as a system position.
    std::int32_t origin = EventRefs::NONE;
    /// Where it would go, or `NONE` before a system has been lit.
    std::int32_t selected = EventRefs::NONE;
  };

  void Create(MatchState _state);

  /// Whether the two things on this screen that move on their own are held at phase zero.
  ///
  /// **A capture of a pulsing ring is a capture of whichever phase the shutter caught** (ADR-113).
  /// The move mode's ring and its marching lane dash are pure functions of `m_animationSeconds`, so
  /// freezing them is refusing to advance it -- which is what `--still` does, and what every
  /// headless test already does by never calling `Update`.
  void SetStill(bool _still) noexcept
  {
    m_still = _still;
  }

  /// Whether this build shows the controls that are not finished yet (ADR-091).
  ///
  /// **`REPLAY` is the only one, and screen 07 is a stub behind it.** A button whose own title says
  /// `NOT YET WIRED` is a button teaching a player that this screen's controls may not work, which
  /// is the opposite of what every other decision here has been for (ADR-053, ADR-077). It stays
  /// reachable for whoever is building it, behind `--dev`.
  void SetDeveloperControls(bool _shown) noexcept
  {
    m_developerControls = _shown;
  }

  /// Whether the link to the server is down (ADR-085).
  ///
  /// **It gates ORDERS and nothing else.** A client that cannot send cannot order, so every control
  /// that gives one goes inert exactly as it does at the lock -- but reading the digest, focusing
  /// the map, orbiting it and opening a sheet all still work, because none of them reaches the
  /// wire. That is the whole difference between the banner this goes with and the modal it replaced.
  void SetOffline(bool _offline) noexcept
  {
    m_offline = _offline;
  }

  /// Whether an order can be given at all: the orders are unlocked, the match is running, and the
  /// link is up. One question asked in one place, because "can this be tapped" was three
  /// conditions in twelve places and the third was missing from all of them.
  [[nodiscard]] bool OrdersEditable() const noexcept
  {
    return !m_state.orders.locked && !m_state.match.finished && !m_offline;
  }

  /// Advances the live countdown. At zero the orders lock: the rail flips UNLOCKED to LOCKED and
  /// every control on it goes inert, which is the whole of the tick discipline the client
  /// enforces (the server decides what actually resolves).
  void Update(double _elapsedSeconds);

  /// A tap at a screen pixel. Returns true when it hit something, so the caller can tell a
  /// handled tap from one that fell on the background.
  bool HandleTap(float _xPixels, float _yPixels);

  /// A drag. Only a drag that STARTED on the map rotates it; one that started on a rail is
  /// ignored, so a slipped finger on the orders list never spins the galaxy. Returns true when
  /// the drag was consumed.
  bool HandleDrag(const Neuron::PointerInput::Drag& _drag);

  /// A wheel notch or a pinch step, and where the pointer was when it arrived. Returns true when
  /// something moved.
  ///
  /// **One entry point because there is one banked count** (`PointerInput::TakeZoomSteps`), and the
  /// pane under the pointer is what decides its meaning: over the digest column it scrolls the card
  /// stack (ADR-080). Positive is one notch away from the player, which scrolls DOWN the column --
  /// the direction every other list on this platform goes.
  bool HandleZoom(std::int32_t _steps, float _xPixels, float _yPixels);

  /// A key. `PageUp` and `PageDown` move the digest a screenful, which is the keyboard's half of
  /// ADR-080 and the only thing on this screen a key does.
  bool HandleKey(Neuron::KeyboardInput::Key _key);

  /// Where the pointer is, so the locks rail can fill the row under it (`Ink::HOVER_FILL`).
  ///
  /// **It returns whether the ROW changed, not whether the pointer moved**, because the page
  /// redraws only when something on it is different (ADR-047) and a mouse crossing a row is the
  /// only movement that changes a pixel. Pass a point off the screen when the pointer has left the
  /// window.
  bool SetPointer(float _xPixels, float _yPixels);

  /// Puts the camera back where the screen opened. There is no other way back to the authored
  /// framing once the map has been orbited, and hunting for it by eye is not a thing to ask
  /// (ADR-017).
  void ResetView() noexcept
  {
    m_mapView.ResetView();
  }

  [[nodiscard]] const MapView& Map() const noexcept
  {
    return m_mapView;
  }

  /// The frame, in two layers, because the caller has to flush between them.
  ///
  /// **The interface is two renderers and each is one batch** (ADR-014): every shape, then every
  /// glyph. Drawn as one layer, that put every glyph over every shape whatever order they were
  /// recorded in -- so a panel opened over the map covered the map's dots and lanes and left its
  /// LABELS floating on top of the panel. A modal that text shows through is not a modal.
  ///
  /// So the world is drawn, both renderers are flushed, and then the interface is drawn over it.
  /// `DrawWorld` starts the frame: it clears the hit list, which `DrawInterface` then fills.
  ///
  /// The world takes a third recorder, for the map's stations: lit solids, drawn by a pass that
  /// tests depth, between the shapes under them and the shapes over them (`DrawMap`, ADR-103).
  /// The interface has no use for it -- nothing on a rail is a solid.
  void DrawWorld(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, Neuron::MeshRenderer& _meshes);
  void DrawInterface(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  [[nodiscard]] const MatchState& State() const noexcept
  {
    return m_state;
  }
  [[nodiscard]] Panel OpenPanel() const noexcept
  {
    return m_panel;
  }
  /// The move the map is taking, if any. Public for the reason `OpenPanel` is: a mode that cannot be
  /// observed cannot be tested (ADR-041).
  [[nodiscard]] const std::optional<MoveMode>& MoveOrder() const noexcept
  {
    return m_moveMode;
  }

  /// Which rival's card is open, and which card the digest column starts at (ADR-061, ADR-080).
  /// Both are how a player is READING the digest rather than anything about the match, and both are
  /// here for the same reason `OpenPanel` is: a control that cannot be observed cannot be pressed
  /// by a test.
  [[nodiscard]] OwnerId ExpandedActor() const noexcept
  {
    return m_expandedActor;
  }
  [[nodiscard]] std::size_t DigestTop() const noexcept
  {
    return m_digestTop;
  }
  /// How many cards the last frame put on the screen. What a page is, measured rather than assumed.
  [[nodiscard]] std::size_t CardsOnScreen() const noexcept
  {
    return m_cardsOnScreen;
  }

  /// Whether anything on this page moves on its own and so needs a frame even when nobody has
  /// touched anything.
  ///
  /// **The idle throttle is what this exists for.** The loop sleeps rather than redrawing when
  /// nothing has changed, which is right for a board where the only moving thing is a countdown
  /// that ticks once a second -- and wrong for a fleet whose route is animated. Asked rather than
  /// assumed, so a map with nothing in transit still costs nothing to sit in front of.
  [[nodiscard]] bool Animating() const noexcept;
  [[nodiscard]] std::int32_t FocusedSystem() const noexcept
  {
    return m_focusedSystem;
  }

  /// The countdown as HH:MM:SS. Static and pure, so the format is testable without a screen.
  [[nodiscard]] static std::string FormatCountdown(double _seconds);

  /// 4 -> `4TH`. Ordinals, because `4 / 12` reads as a fraction and a placement is not one.
  ///
  /// Public because the final standings table spells placements the same way and is composed in the
  /// composition root, where `MatchState` and `ConnectionDialog` meet (ADR-097). Two copies of an
  /// ordinal rule is one place for `1TH` to appear.
  [[nodiscard]] static std::string FormatPlacement(std::uint32_t _placement);

  /// What a build sheet is priced against when the queue has already taken part of the purse, or
  /// an empty string when it has not (ADR-078).
  ///
  /// **The purse on the top bar is not the number a sheet refuses a build by**, and until this
  /// sentence existed nothing on the sheet said so: a row reading `30 CR - NEED 4 MORE` sat under a
  /// bar reading `46 CR`, and both were correct. Public and pure for the reason `FormatCountdown`
  /// is -- the arithmetic is what must be right, and asserting it needs no screen.
  [[nodiscard]] std::string PurseSentence() const;

  /// What a build sheet says above a system that is already building: *Xerev cannot take another
  /// order until this lands. Two of three ticks are in.* (ADR-070, ADR-107).
  ///
  /// **The count is spelled in words below ten**, because this is a sentence in the sans face and
  /// not a status column -- `2 of 3` is data and belongs on the tile, which carries it as
  /// `2 OF 3 TICKS`. Static and pure for the reason `PurseSentence` is: the arithmetic is what must
  /// be right, and asserting it must not need a screen. Empty when there are no ticks to count,
  /// which is a remembered system carrying no construction (ADR-022).
  [[nodiscard]] static std::string RisingSentence(std::string_view _systemName, std::uint32_t _ticksIn, std::uint32_t _ticks);

  /// Ticks for a fleet to reach a system from where it is, along lanes. Breadth-first over lane
  /// costs -- the picker shows it against every reachable destination, and it is the number the
  /// player is actually choosing between.
  [[nodiscard]] std::uint32_t TicksTo(std::int32_t _fromSystem, std::int32_t _toSystem) const;

  /// One tappable rectangle, recorded beside the `FillRect` that drew the thing it is about.
  ///
  /// Public so it can be AUDITED (ADR-098). A target that cannot be measured cannot be held to the
  /// 44-pixel floor, and the floor is the whole of what UI-02 is: reading the constants is not
  /// evidence, because a control's box is composed from several of them and the one that went wrong
  /// was the one nobody added up. Same reasoning as `OpenPanel` (ADR-041) -- a control that cannot
  /// be observed cannot be pressed by a test.
  struct HitRegion
  {
    float x;
    float y;
    float width;
    float height;
    Action action;
    std::int32_t index;
  };

  /// Every tappable rectangle the last `DrawWorld`/`DrawInterface` pair recorded.
  [[nodiscard]] const std::vector<HitRegion>& Hits() const noexcept
  {
    return m_hits;
  }

private:
  /// One rectangle that draws a HOVER, kept so `SetPointer` can tell when the pointer crossed from
  /// one to another and the page has to be redrawn (ADR-047).
  ///
  /// **Every control that has a hover state is here, not only the rail's rows** (ADR-110). The rail
  /// was the one list that filled under the pointer; the control vocabulary gives a hover to every
  /// outlined and committed button too, and a button whose hover the redraw never noticed would be
  /// a button that lit only when something else on the screen changed.
  struct HoverRegion
  {
    float x;
    float y;
    float width;
    float height;
  };

  /// What one digest card takes, worked out once and used twice (ADR-061).
  ///
  /// **The wrap is the expensive half and the height depends on it**, so a pass that measured and
  /// a pass that drew would wrap every line twice and could disagree about the answer. This is
  /// computed before anything is drawn -- it is what decides which page a card falls on -- and the
  /// draw then reads the same strings back rather than recomputing them.
  struct CardLayout
  {
    /// The detail, wrapped. Empty on a collapsed actor card, which is the whole of collapsing.
    std::vector<std::string> details;
    /// The verdict box's second and later lines, wrapped two characters narrower for its border.
    std::vector<std::string> verdictDetail;
    bool hasVerdict = false;
    bool hasActions = false;
    /// Whether this card's title is a target that opens and closes it.
    bool collapsible = false;
    float height = 0.0F;
  };

  [[nodiscard]] CardLayout LayoutCard(const DigestCard& _card, std::uint32_t _widthPixels) const;

  /// Measures the galaxy's bounding sphere, so the camera can frame it. Called once, from
  /// Create: the graph does not move between ticks.
  void MeasureContent();

  /// Which of `m_hoverRegions` the pointer is over, or `EventRefs::NONE`.
  [[nodiscard]] std::int32_t RegionUnderPointer() const noexcept;

  /// Where the digest column would start if it went back one screenful, measured from the card
  /// heights the frame just laid out (ADR-080).
  [[nodiscard]] std::size_t PreviousDigestTop(const std::vector<CardLayout>& _layouts, float _room) const;

  /// Moves the digest by `_cards`, clamped. True when it moved.
  bool ScrollDigest(std::int32_t _cards);

  /// Moves the locks rail by `_pixels`, clamped to what the last frame measured. True when it moved.
  bool ScrollRail(float _pixels);

  /// Puts back the sheet a new state arrived under, if what it was about is still there (ADR-065).
  void ReopenPanel(Panel _panel, std::int32_t _subjectId, std::int32_t _subject);

  /// Opens the place sheet on one system, or closes whatever is open when the position is `NONE`.
  ///
  /// One function because four controls lead here -- the map's disc, its garrison badge, a digest
  /// button and a rail row (ADR-111) -- and each of them has to reset the same three things: the
  /// subject, the id that survives a snapshot (ADR-057), and the scroll, which is about the sheet
  /// in front of you rather than about the place.
  void OpenPlace(std::int32_t _system);

  /// Moves the place sheet's body by `_blocks`, clamped to what the last frame measured. True when
  /// it moved.
  bool ScrollSheet(std::int32_t _blocks);

  /// Takes one fleet's move onto the map, or leaves the mode (ADR-113).
  void EnterMove(std::int32_t _fleet);
  void ExitMove() noexcept;

  /// Puts back the move a new state arrived under, if the fleet it was about can still take one
  /// (ADR-065, ADR-077). Its argument is a fleet ID, which is what survives a snapshot (ADR-057).
  void ReopenMove(std::int32_t _fleetId);

  /// Where fleet `_fleet` may be sent, nearest first and then by name (ADR-092). Empty for a fleet
  /// the lock would refuse an order on.
  [[nodiscard]] std::vector<MoveTargetSystem> ReachableFor(std::int32_t _fleet) const;

  /// The banner across the map and the confirm strip under it (ADR-113). Interface rather than
  /// world, because both have to cover the map's own labels and a shape cannot (ADR-014).
  void DrawMoveMode(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  /// One ink at the share of itself the digest wears while the map is taking a move (ADR-113).
  [[nodiscard]] Neuron::Color Faded(const Neuron::Color& _color) const noexcept;

  /// What the rail and an open sheet both say at the lock. One sentence, said once, because two
  /// copies of it is one wrong tick number waiting.
  [[nodiscard]] std::string LockSentence() const;

  void AddHit(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, Action _action, std::int32_t _index);

  /// The same, refused while the map is taking a move (ADR-113). The digest's every control goes
  /// through it: the column fades and stops being a target for as long as the mode is on, and one
  /// guard beside `AddHit` is what keeps that from being eleven conditions that have to agree.
  void AddHitUnlessMoving(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, Action _action, std::int32_t _index);

  /// The viewer's own fleets that BELONG to one place this tick, as indices into `m_state.fleets`.
  ///
  /// **Standing there, or ordered off it and not gone yet** (ADR-111). A move given this tick puts
  /// a fleet on a lane at progress zero from the moment it is given (ADR-055) while leaving it
  /// where it is until the lock (ADR-077), so a list of fleets STANDING at a system loses the one
  /// the player just ordered -- and that is exactly the row they need in order to take it back.
  /// `from` is the place a fleet belongs to for as long as the order is still an edit.
  [[nodiscard]] std::vector<std::int32_t> FleetsAtPlace(std::int32_t _system) const;

  /// How many credits short the purse is of build row `_index` on top of what is already queued;
  /// zero when it is affordable or names no row. The number a dim build control shows (ADR-053).
  [[nodiscard]] std::uint32_t BuildShortfall(std::int32_t _index) const noexcept;

  void DrawTopBar(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  /// The width a card's text wraps to: the column less the dot's gutter and the padding either side.
  /// A class constant rather than a local, because the pass that measures a card and the pass that
  /// draws one are two members now and a second copy of this is a second answer.
  static constexpr float CARD_TEXT_LEFT = RAIL_PADDING + 8.0F + 10.0F;
  static constexpr std::uint32_t CARD_TEXT_WIDTH = static_cast<std::uint32_t>(DIGEST_WIDTH - CARD_TEXT_LEFT - RAIL_PADDING);

  /// The chrome and inks one control is drawn in, as `Controls.h` chose them. Named here so that a
  /// member can fade one; the table itself is not this class's business.
  using ControlInk = Lockstep::ControlInk;
  [[nodiscard]] ControlInk FadedInk(ControlInk _ink) const;

  /// The digest column: its header and delta, one card, one card's buttons, and the page band.
  [[nodiscard]] float DrawDigestHeader(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);
  void DrawDigestCard(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const DigestCard& _card, const CardLayout& _layout,
                      float& _yPixels);
  void DrawDigestActions(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const DigestCard& _card, std::int32_t& _lineYPixels);
  void DrawDigestPageBand(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const std::vector<DigestCard>& _cards,
                          const std::vector<CardLayout>& _layouts, std::size_t _lastCard, float _roomPixels, bool _paged);
  void DrawDigestRail(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  /// What a digest button does, and what it names.
  ///
  /// **Two numbers rather than one, because the two enums count different things.** What an event
  /// OFFERS is a fact about the match (`EventActionKind`, whose target is a build row, a fleet or a
  /// proposal); what a tap DOES is a fact about this screen, and the index it needs is not always
  /// the one the action carries (ADR-057).
  struct DigestTarget
  {
    Action action = Action::None;
    std::int32_t index = EventRefs::NONE;
  };
  [[nodiscard]] DigestTarget TargetOf(const EventAction& _action) const noexcept;

  /// The locks rail's own vocabulary, complete in `MainPageRail.cpp` and nowhere else: one order,
  /// one place, and where the column is up to inside the band it scrolls in (ADR-101, ADR-112).
  struct OrderRow;
  struct PlaceRow;
  struct RailCursor;

  /// The rail's ground, header, help line and band; the cursor it returns is where the sections
  /// start and what they are culled against.
  [[nodiscard]] RailCursor BeginRail(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, bool _atLock);
  [[nodiscard]] std::vector<OrderRow> ComposeOrders(bool _navigateOnly) const;
  [[nodiscard]] std::vector<PlaceRow> ComposePlaces(bool _navigateOnly) const;
  void DrawRailSection(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, RailCursor& _cursor, std::string_view _label,
                       std::string_view _count);
  void DrawRailRow(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, RailCursor& _cursor, std::string_view _label,
                   std::string_view _status, const Neuron::Color& _statusColor, Action _action, std::int32_t _index,
                   std::size_t _dimHead = 0);
  void DrawOrderRow(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, RailCursor& _cursor, const OrderRow& _order);
  void DrawPlaceRow(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, RailCursor& _cursor, const PlaceRow& _place);
  void DrawRailNothing(Neuron::FontRenderer& _text, RailCursor& _cursor, std::string_view _text2);
  void DrawRailSignals(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, RailCursor& _cursor);
  void DrawRailProposals(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, RailCursor& _cursor);
  /// The page band and the pinned footer, read from what the sections came to.
  void DrawRailFooter(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const RailCursor& _cursor, bool _atLock);
  void DrawLocksRail(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  /// The sheet's own vocabulary, complete in `MainPageSheet.cpp` and nowhere else: what one row
  /// says, one of the viewer's fleets on the place, one build tile, one block of the place sheet's
  /// body, the whole of what a sheet has composed before any of it is drawn, and what its body came
  /// to once laid out (ADR-052, ADR-107, ADR-111).
  struct SheetRow;
  struct PlaceFleet;
  struct BuildTile;
  struct SheetBlock;
  struct Sheet;
  struct SheetBody;

  /// One composition per panel kind, each writing only what it is about. The place sheet's is false
  /// when the position it was opened on is not on the board, and then nothing is drawn at all.
  [[nodiscard]] bool ComposePlaceSheet(Sheet& _sheet) const;
  void ComposeSignalSheet(Sheet& _sheet) const;
  void ComposeReplaySheet(Sheet& _sheet) const;

  /// Lays the place sheet's body out as blocks against the room the frame has left it, pins the
  /// FLEETS section when it fits, clamps the scroll, and records what the next notch can move
  /// (ADR-111). Every sheet goes through it, so a notch over a sheet with no blocks moves nothing.
  [[nodiscard]] SheetBody LayoutSheetBody(const Sheet& _sheet, float _widthPixels, float _bodyCapPixels);

  /// The header: the disc, the name, the status slot, what the place is, and the `X` (ADR-111).
  void DrawSheetHeader(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const Sheet& _sheet, bool _atLock, float _xPixels,
                       float _yPixels, float _widthPixels);
  /// The place sheet's body: the scrolled blocks, then the pinned ones, each drawn by its kind.
  void DrawPlaceBody(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const Sheet& _sheet, const SheetBody& _body,
                     float _xPixels, float _bodyTopPixels, float _widthPixels);
  void DrawSheetBlock(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const Sheet& _sheet, const SheetBody& _body,
                      const SheetBlock& _block, float _xPixels, float _yPixels, float _widthPixels);
  /// One tile of the build grid, and the whole of it is the target (ADR-107).
  void DrawBuildTile(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const BuildTile& _tile, float _xPixels, float _yPixels,
                     float _widthPixels);
  /// One fleet's row: a box, the name, what it is doing, and the one control on it (ADR-111).
  void DrawFleetRow(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const PlaceFleet& _fleet, float _xPixels, float _yPixels,
                    float _widthPixels);
  /// The capped list, the line that counts what did not fit, then whatever is pinned below it
  /// (ADR-093); `_rowYPixels` ends where the bottom bar begins.
  void DrawSheetRows(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const Sheet& _sheet, std::size_t _shown, float _xPixels,
                     float& _rowYPixels, float _widthPixels);
  /// One row or one band, advancing `_rowYPixels` by what it took. A pinned row is an ordinary row
  /// that is simply not counted, which is why there is one of these rather than two.
  void DrawSheetRow(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const SheetRow& _row, Action _rowAction, float _xPixels,
                    float& _rowYPixels, float _widthPixels, bool& _previousWasBand);

  /// The sheet against the bottom of the map pane: composed, laid out, drawn (ADR-052, ADR-111).
  void DrawPanel(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  MatchState m_state;

  /// Which `Concede` row the player has tapped once. NONE unless one is armed.
  ///
  /// **Conceding is the only order on this screen that cannot be taken back**, because it resolves
  /// into handing the empire to a custodian permanently. Everything else here is an edit until the
  /// lock, so the second tap is not a modal dialog -- it is the row itself changing to say what the
  /// next tap will do. It disarms on any other tap, and it is per-screen rather than per-order
  /// state: the arming is about fingers, not about what is sent.
  std::int32_t m_armedConcede = EventRefs::NONE;

  /// Which rival's card is open, or `NOBODY`. **One at a time** (ADR-061): a column that can only
  /// show one card's worth of lines should not be able to hold two open and page them apart.
  OwnerId m_expandedActor = NOBODY;

  /// Which card the digest column starts at. Reset by `Create`, because a digest is replaced
  /// wholesale and card thirty of the last one is nowhere in this one (ADR-080).
  std::size_t m_digestTop = 0;

  /// Whether the LAST frame found more rail than fits, which is what reserves the page band.
  ///
  /// **A frame late on purpose, and it cannot oscillate.** Whether the rail overflows depends on
  /// how tall the band is, and how tall the band is depends on whether it overflows; the loop is
  /// broken by reserving from the previous answer. Reserving the band only ever makes the column
  /// shorter, so a rail that was paged stays paged and a rail that was not becomes paged at most
  /// once -- there is no content height that flickers between the two.
  bool m_railPaged = false;

  /// How far the locks rail is scrolled, in PIXELS rather than in rows (ADR-101).
  ///
  /// **The digest scrolls by cards and this scrolls by pixels, and the difference is what each
  /// column is.** A digest is a stack of cards of wildly different heights, and stopping part-way
  /// through one puts a title off the top; the rail is a list of fixed 44px rows and 44px bands
  /// under section headers, so every pixel offset lands somewhere legible and snapping would only
  /// make the gesture feel stickier than the column looks.
  ///
  /// NOT reset by `Create`: the rail is a summary of the same empire tick after tick, so a player
  /// who has scrolled to their signals expects to still be looking at them when the tick lands.
  /// Clamped in the draw, which is the only place that knows how tall the content came out.
  float m_railScrollPixels = 0.0F;
  /// What the last frame measured the rail's content and viewport as, so a scroll can be clamped
  /// against something real rather than against a guess.
  float m_railContentPixels = 0.0F;
  float m_railViewportPixels = 0.0F;
  /// How many cards the last frame drew, so a page key can move by what a page actually was. Layout
  /// is the only thing that knows, and it knows it a frame late -- which is the same frame-old hit
  /// list every tap on this screen is already tested against.
  std::size_t m_cardsOnScreen = 1;
  /// Drag distance banked toward the next whole card, for the finger's half of scrolling. A drag is
  /// continuous and the column moves in cards, so what is left over is kept rather than thrown away
  /// -- the same bargain `PointerInput` makes with a high-resolution wheel.
  float m_digestDragPixels = 0.0F;

  Panel m_panel = Panel::None;
  /// The move being chosen on the map (ADR-113). A mode rather than a panel, because it changes
  /// what the MAP means -- a sheet sits over the map and this one is played on it.
  std::optional<MoveMode> m_moveMode;
  /// Which BLOCK of the place sheet's body is at the top of its scrolling region (ADR-111).
  ///
  /// **Blocks and not pixels**, and the difference is what the two columns are. The locks rail is a
  /// list of 44-pixel rows, so every pixel offset lands somewhere legible (ADR-101); this body's
  /// tallest block is a 96-pixel tile row in a viewport that can be 134, and `ShapeRenderer` has no
  /// clip rectangle -- a part-scrolled tile is either painted over the header above it or dropped
  /// whole. The digest made the same trade for the same reason (ADR-080).
  ///
  /// Reset when a sheet opens, because a scroll position is about the sheet in front of you.
  std::size_t m_sheetScroll = 0;
  /// Drag distance banked toward the next whole block, the finger's half of scrolling it.
  float m_sheetDragPixels = 0.0F;
  /// How many blocks the last frame's body held and how many it could show, so a scroll can be
  /// clamped against something measured rather than guessed.
  std::size_t m_sheetBlocks = 0;
  std::size_t m_sheetBlocksShown = 0;
  /// Which system's build list or which fleet's picker is open, as a POSITION in the view's lists.
  std::int32_t m_panelSubject = EventRefs::NONE;
  /// The same subject as the id the simulation knows it by, which is what survives a new state.
  ///
  /// **A position is not stable across a snapshot and an id is** (ADR-057): the graph is fogged, so
  /// the tenth system a player can see this tick may be the eleventh next tick. A sheet that stayed
  /// open on a position would be a sheet about a different system (ADR-065).
  std::int32_t m_panelSubjectId = EventRefs::NONE;
  /// The node the digest last pointed at. Drawn with a focus ring; -1 when nothing is focused.
  std::int32_t m_focusedSystem = EventRefs::NONE;

  /// Whether `--dev` was passed (ADR-091). Off in every shipped run.
  bool m_developerControls = false;

  /// Whether `--still` was passed: the clock that drives the route's dashes and the move mode's
  /// pulse does not advance, so a capture is always of phase zero (ADR-113).
  bool m_still = false;

  /// The placement this page last drew, so the chip can say a place was LOST rather than only what
  /// it is (ADR-091). Session memory and nothing more: it starts at zero, which no placement is, and
  /// a client that joins mid-match simply has no previous place until it has drawn one.
  std::uint32_t m_placementDrawn = 0;

  /// Whether the link is down (ADR-085). Set by the match loop from the connection's state; it is
  /// not part of `MatchState` because it is a fact about this client's socket rather than about the
  /// match, and a snapshot that carried it would be a snapshot that could disagree with the wire.
  bool m_offline = false;

  /// The camera looking at the galaxy, and the ground plane it orbits (ADR-017).
  MapView m_mapView;

  /// How long this page has been on the screen. It drives the rolling dashes on a fleet's route
  /// and nothing else (ADR-055), and it runs whether or not the orders are locked: a fleet under
  /// way is under way while the tick resolves.
  float m_animationSeconds = 0.0F;

  /// The sky. Built once and never changed: it is the same stars from every angle, which is what
  /// makes turning the map feel like turning rather than like sliding a backdrop (ADR-032).
  Neuron::Starfield m_sky;

  /// The bounding sphere of everything the map draws, in world space. What the camera frames.
  Neuron::OrbitCamera::WorldPoint m_contentCenter = {0.0F, 0.0F, 0.0F};
  float m_contentRadius = 1.0F;

  /// Rebuilt every Draw. A tap is tested against the PREVIOUS frame's rectangles, which is
  /// invisible at any frame rate a person can tap through and is what lets layout and hit
  /// testing be the same code rather than two that must agree.
  std::vector<HitRegion> m_hits;

  /// Everything that fills under the pointer, rebuilt with the frame, and where the pointer is.
  std::vector<HoverRegion> m_hoverRegions;
  float m_pointerXPixels = -1.0F;
  float m_pointerYPixels = -1.0F;
  std::int32_t m_hoveredRegion = EventRefs::NONE;
};

} // namespace Lockstep
