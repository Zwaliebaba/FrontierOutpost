#pragma once

#include "FontRenderer.h"
#include "MapView.h"

#include "Starfield.h"
#include "KeyboardInput.h"
#include "MatchState.h"
#include "PointerInput.h"
#include "ShapeRenderer.h"

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

  /// A button inside an event card, and the padding inside the verdict box.
  ///
  /// 18 is unchanged from the 8x8 font and deliberately so: a line BOX grew from 8 to 17, but the
  /// ink in an uppercase button label did not -- Plex's cap height at 12px is 8.4px, within half a
  /// pixel of the height the old capitals had. Growing the chrome to match the box would inflate
  /// every control on the rail to fit ascender room that a shouted label never uses. What had to
  /// change is where the box is PUT, which `BandTopForText` now answers.
  static constexpr float BUTTON_HEIGHT = 18.0F;
  static constexpr float VERDICT_BOX_PADDING = 5.0F;

  /// The digest's own two bands, both 22 pixels (ADR-061).
  ///
  /// **22 is what a label that is also a control costs on this screen** -- it is the section header
  /// on the locks rail, and the `SIGNALS` one has been a control since ADR-039. An actor card's
  /// title is the first of these and the page band at the foot of the column is the second, so the
  /// two things a player taps to see more of the digest are the same size as each other.
  static constexpr float DIGEST_TITLE_HEIGHT = 22.0F;
  static constexpr float DIGEST_PAGE_HEIGHT = 22.0F;

  /// The sheet a panel is drawn as, anchored to the bottom of the map pane (ADR-052).
  ///
  /// **44 is the number that matters and the rest follow it.** It is the smallest target a finger
  /// hits reliably, and it is also the height of the top bar, so a sheet row and the bar read as
  /// the same unit of the frame. Six rows and the sheet still leaves over half the pane showing,
  /// which is the constraint the other direction: the map is what the choice is about.
  static constexpr float SHEET_MARGIN = 12.0F;
  static constexpr float SHEET_ROW_HEIGHT = 44.0F;
  static constexpr float SHEET_HEADER_HEIGHT = 36.0F;
  static constexpr float SHEET_ACTION_HEIGHT = 40.0F;
  /// A section band inside a sheet: a label over the rows under it, and not a target. The same 22
  /// the locks rail's section headers take, because it is the same thing (ADR-064).
  static constexpr float SHEET_BAND_HEIGHT = 22.0F;
  /// The row that says how many did not fit, which is shorter because nothing taps it.
  static constexpr float SHEET_CLIPPED_HEIGHT = 24.0F;
  static constexpr std::size_t SHEET_MAXIMUM_ROWS = 6;

  /// What a tap does. The screen has no free text and no chat, so this is the complete list of
  /// things a player can express on it (one-pager, "What it is not").
  enum class Action : std::uint8_t
  {
    None,
    /// Focus the map on what this digest event is about. Its index is a DIGEST index.
    FocusEvent,
    /// Focus the map on one system. Its index is a SYSTEM position, which is why it is not
    /// `FocusEvent` (ADR-057): one action carrying two kinds of index is an action that reads the
    /// wrong array, and the bounds check turned that into a button that did nothing at all.
    FocusSystem,
    /// Open a system's build list.
    OpenSystem,
    /// Open a fleet's destination picker, lane-constrained.
    OpenFleet,
    /// Open what is standing at one system, from its garrison badge (ADR-079). Its index is a
    /// SYSTEM position: one fleet of the viewer's there goes straight to that fleet's picker, and
    /// several open the sheet that picks between them first.
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
    /// Pick a destination in the open picker.
    ChooseDestination,
    ClosePanel
  };

  /// Which modal the screen is showing over the map, if any.
  enum class Panel : std::uint8_t
  {
    None,
    BuildList,
    Destination,
    /// Which of the several fleets standing at one system (ADR-079). Its subject is a SYSTEM, and
    /// it exists only because a badge totals ships and a picker has to be about one fleet.
    FleetList,
    SignalList,
    Replay
  };

  void Create(MatchState _state);

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
  void DrawWorld(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);
  void DrawInterface(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  [[nodiscard]] const MatchState& State() const noexcept
  {
    return m_state;
  }
  [[nodiscard]] Panel OpenPanel() const noexcept
  {
    return m_panel;
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

  /// What a build sheet is priced against when the queue has already taken part of the purse, or
  /// an empty string when it has not (ADR-078).
  ///
  /// **The purse on the top bar is not the number a sheet refuses a build by**, and until this
  /// sentence existed nothing on the sheet said so: a row reading `30 CR - NEED 4 MORE` sat under a
  /// bar reading `46 CR`, and both were correct. Public and pure for the reason `FormatCountdown`
  /// is -- the arithmetic is what must be right, and asserting it needs no screen.
  [[nodiscard]] std::string PurseSentence() const;

  /// Ticks for a fleet to reach a system from where it is, along lanes. Breadth-first over lane
  /// costs -- the picker shows it against every reachable destination, and it is the number the
  /// player is actually choosing between.
  [[nodiscard]] std::uint32_t TicksTo(std::int32_t _fromSystem, std::int32_t _toSystem) const;

private:
  struct HitRegion
  {
    float x;
    float y;
    float width;
    float height;
    Action action;
    std::int32_t index;
  };

  /// One tappable row of the locks rail, kept so `SetPointer` can tell when the pointer crossed
  /// from one to another. Only the rail's rows are here: it is the only list that draws a hover.
  struct RailRow
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

  /// Which of `m_railRows` the pointer is over, or `EventRefs::NONE`.
  [[nodiscard]] std::int32_t RailRowUnderPointer() const noexcept;

  /// Where the digest column would start if it went back one screenful, measured from the card
  /// heights the frame just laid out (ADR-080).
  [[nodiscard]] std::size_t PreviousDigestTop(const std::vector<CardLayout>& _layouts, float _room) const;

  /// Moves the digest by `_cards`, clamped. True when it moved.
  bool ScrollDigest(std::int32_t _cards);

  /// Puts back the sheet a new state arrived under, if what it was about is still there (ADR-065).
  void ReopenPanel(Panel _panel, std::int32_t _subjectId, std::int32_t _subject);

  /// What the rail and an open sheet both say at the lock. One sentence, said once, because two
  /// copies of it is one wrong tick number waiting.
  [[nodiscard]] std::string LockSentence() const;

  void AddHit(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, Action _action, std::int32_t _index);

  /// The viewer's own fleets standing at one system and able to take an order, as indices into
  /// `m_state.fleets`. What a garrison badge opens, and what the fleet-list sheet lists (ADR-079).
  [[nodiscard]] std::vector<std::int32_t> StandingFleetsAt(std::int32_t _system) const;

  /// How many credits short the purse is of build row `_index` on top of what is already queued;
  /// zero when it is affordable or names no row. The number a dim build control shows (ADR-053).
  [[nodiscard]] std::uint32_t BuildShortfall(std::int32_t _index) const noexcept;

  void DrawTopBar(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);
  void DrawDigestRail(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);
  [[nodiscard]] static Action ActionFor(EventActionKind _kind) noexcept;
  void DrawLocksRail(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  /// A circle lying ON the ground plane, projected. Shadows and the sealed region are both this:
  /// what shape they make on screen is the camera's business, not theirs (ADR-017).
  void DrawPanel(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  /// Text centred on a point, snapped to a whole pixel. Every centred label on the map goes
  /// through this so that none of them lands on a half pixel.

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
  /// How many cards the last frame drew, so a page key can move by what a page actually was. Layout
  /// is the only thing that knows, and it knows it a frame late -- which is the same frame-old hit
  /// list every tap on this screen is already tested against.
  std::size_t m_cardsOnScreen = 1;
  /// Drag distance banked toward the next whole card, for the finger's half of scrolling. A drag is
  /// continuous and the column moves in cards, so what is left over is kept rather than thrown away
  /// -- the same bargain `PointerInput` makes with a high-resolution wheel.
  float m_digestDragPixels = 0.0F;

  Panel m_panel = Panel::None;
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

  /// The locks rail's rows, rebuilt with it, and where the pointer is over them.
  std::vector<RailRow> m_railRows;
  float m_pointerXPixels = -1.0F;
  float m_pointerYPixels = -1.0F;
  std::int32_t m_hoveredRailRow = EventRefs::NONE;
};

} // namespace Lockstep
