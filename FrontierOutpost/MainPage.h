#pragma once

#include "FontRenderer.h"
#include "MapView.h"

#include "Starfield.h"
#include "MatchState.h"
#include "PointerInput.h"
#include "ShapeRenderer.h"

namespace Frontier
{

/// The single screen of Frontier Outpost: digest, map, orders.
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
  /// Design/Screens/README.md "Frame".
  static constexpr float TOP_BAR_HEIGHT = 48.0F;
  static constexpr float DIGEST_WIDTH = 300.0F;
  static constexpr float ORDERS_WIDTH = 330.0F;
  static constexpr float RAIL_PADDING = 14.0F;
  static constexpr float CARD_PADDING = 10.0F;
  /// Line-height 1.5 on an 8px font (README "Frame").
  static constexpr std::int32_t LINE_HEIGHT = 12;

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

  void Draw(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

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

  void DrawTopBar(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);
  void DrawDigestRail(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);
  void DrawMap(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);
  void DrawOrdersRail(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);
  void DrawSystem(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, std::int32_t _index);
  void DrawFleet(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, std::int32_t _index);

  /// A circle lying ON the ground plane, projected. Shadows and the sealed region are both this:
  /// what shape they make on screen is the camera's business, not theirs (ADR-017).
  void DrawGroundCircle(Neuron::ShapeRenderer& _shapes, float _designX, float _designY, float _radius, const Neuron::Color& _fill,
                        const Neuron::Color& _outline, bool _dashed, float _height = 0.0F);
  void DrawPanel(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  /// Text centred on a point, snapped to a whole pixel. Every centred label on the map goes
  /// through this so that none of them lands on a half pixel.
  static void DrawCentered(Neuron::FontRenderer& _text, float _centerXPixels, std::int32_t _yPixels, std::string_view _string,
                           const Neuron::Color& _color, std::uint32_t _scale = Neuron::FontRenderer::DEFAULT_SCALE);
  static void DrawRight(Neuron::FontRenderer& _text, float _rightXPixels, std::int32_t _yPixels, std::string_view _string,
                        const Neuron::Color& _color, std::uint32_t _scale = Neuron::FontRenderer::DEFAULT_SCALE);

  MatchState m_state;

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

} // namespace Frontier
