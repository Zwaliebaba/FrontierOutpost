#pragma once

#include "FontRenderer.h"
#include "MapView.h"

#include "Starfield.h"
#include "MatchState.h"
#include "PointerInput.h"
#include "ShapeRenderer.h"

namespace Lockstep
{

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
  /// Line-height 1.5 on an 8px font (README "Frame").
  static constexpr std::int32_t LINE_HEIGHT = 12;

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
  /// The row that says how many did not fit, which is shorter because nothing taps it.
  static constexpr float SHEET_CLIPPED_HEIGHT = 24.0F;
  static constexpr std::size_t SHEET_MAXIMUM_ROWS = 6;

  /// What a tap does. The screen has no free text and no chat, so this is the complete list of
  /// things a player can express on it (one-pager, "What it is not").
  enum class Action : std::uint8_t
  {
    None,
    /// Focus the map on what this digest event is about.
    FocusEvent,
    /// Open a system's build list.
    OpenSystem,
    /// Open a fleet's destination picker, lane-constrained.
    OpenFleet,
    /// Queue or unqueue a build. An order: local until the lock.
    ToggleBuild,
    /// Open the list of things this player could say to somebody (ADR-039).
    OpenSignals,
    /// Queue or unqueue one of them. Also an order, and it locks with the rest.
    ToggleSignal,
    /// Answer a proposal. Also an order, and it locks with the others.
    AcceptProposal,
    DeclineProposal,
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
  [[nodiscard]] std::int32_t FocusedSystem() const noexcept
  {
    return m_focusedSystem;
  }

  /// The countdown as HH:MM:SS. Static and pure, so the format is testable without a screen.
  [[nodiscard]] static std::string FormatCountdown(double _seconds);

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

  /// Measures the galaxy's bounding sphere, so the camera can frame it. Called once, from
  /// Create: the graph does not move between ticks.
  void MeasureContent();

  void AddHit(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, Action _action, std::int32_t _index);

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

  Panel m_panel = Panel::None;
  /// Which system's build list or which fleet's picker is open.
  std::int32_t m_panelSubject = EventRefs::NONE;
  /// The node the digest last pointed at. Drawn with a focus ring; -1 when nothing is focused.
  std::int32_t m_focusedSystem = EventRefs::NONE;

  /// The camera looking at the galaxy, and the ground plane it orbits (ADR-017).
  MapView m_mapView;

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
};

} // namespace Lockstep
