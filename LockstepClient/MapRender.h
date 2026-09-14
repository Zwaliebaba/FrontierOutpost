#pragma once

#include "MapView.h"
#include "MatchState.h"

#include "FontRenderer.h"
#include "MeshRenderer.h"
#include "ShapeRenderer.h"
#include "Starfield.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Lockstep
{

/// Something on the map a finger can land on.
///
/// **A system or a fleet, and not an action.** The map knows what things are; the page knows what
/// tapping one does. Returning hits rather than calling back into the page is what lets the map
/// leave `MainPage` without taking a reference to it -- and it keeps `AddHit` beside the `FillRect`
/// that put the thing there, which is the invariant ADR-041 turns on.
struct MapHit
{
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;

  /// Exactly one of these is set; the others are `EventRefs::NONE`.
  ///
  /// `fleetsAt` is a SYSTEM position like `system` is, and it is a separate field rather than a flag
  /// beside that one because the two mean different things to tap: the disc is the system and the
  /// badge beside it is the fleets standing there (ADR-079). One index, one meaning (ADR-057).
  /// `moveTarget` is a third system position and a third meaning: while the map is taking a move,
  /// a lit system is a DESTINATION rather than a place to open (ADR-113).
  std::int32_t system = EventRefs::NONE;
  std::int32_t fleet = EventRefs::NONE;
  std::int32_t fleetsAt = EventRefs::NONE;
  std::int32_t moveTarget = EventRefs::NONE;
};

/// One system a move may be sent to, and what it costs to get there (ADR-113).
///
/// The page works this out -- it is the page that knows the rules the lock would refuse the order
/// by -- and the map draws what it is handed.
struct MoveTarget
{
  std::int32_t system = EventRefs::NONE;
  std::uint32_t ticks = 0;
  std::uint32_t arrivesAt = 0;
};

/// Everything the map needs and does not own.
///
/// The camera is taken by reference and MUTATED -- drawing sets the viewport and frames the content,
/// because both depend on the pane the map was given and the pane is the page's to decide. That is
/// the one thing here that is not read-only, and it is why this is a struct of references rather
/// than a copy.
struct MapFrame
{
  const MatchState& state;
  MapView& view;
  Neuron::Starfield& sky;

  /// What the camera frames: the middle of the galaxy and how far it reaches.
  Neuron::OrbitCamera::WorldPoint contentCenter;
  float contentRadius = 1.0F;

  /// The system the digest last pointed at, drawn with a spotlight. `EventRefs::NONE` for none.
  std::int32_t focusedSystem = EventRefs::NONE;

  /// A clock for the one thing on this map that moves on its own: the dashes travelling along a
  /// fleet's route (ADR-055). Seconds since the page was created, and nothing reads it but the
  /// dash offset -- a map drawn twice at the same value is identical, which is what keeps a
  /// screenshot test meaningful.
  float animationSeconds = 0.0F;

  /// Whether a sheet is open over the pane. The legend sits in the bottom twenty pixels, which is
  /// exactly where a sheet's `CANCEL` bar goes, so it was drawn half-clipped under one in every
  /// sheet capture this project has taken (ADR-082). The map does not know what a panel is; it is
  /// told whether its own bottom edge is covered.
  bool sheetOpen = false;

  /// **The move being chosen ON this map** (ADR-113), which is the one mode that changes what the
  /// map itself means rather than covering it: the system a fleet is standing at, the systems it
  /// may be sent to with the ticks each takes, and whichever of them is lit.
  ///
  /// `EventRefs::NONE` and an empty span is no move, which is every other frame.
  std::int32_t moveOrigin = EventRefs::NONE;
  std::int32_t moveSelected = EventRefs::NONE;
  std::span<const MoveTarget> moveTargets;
};

/// `MAP` or `MAP - FOCUS: PELL`: what the pane is currently pointed at, in the top-left corner.
///
/// Exposed because the `RESET` chip is placed immediately after it (ADR-090) and a second copy of
/// the composition is a second place for the two to disagree about how wide it is.
[[nodiscard]] std::string FocusLine(const MatchState& _state, std::int32_t _focusedSystem);

/// Whether a capture is still news, and so still labelled on the map (ADR-082).
///
/// **Three ticks, and then it is the map rather than the news.** A board a player is winning wore a
/// standing `CAPTURED Tn` under every system they had ever taken -- six of them on
/// `01-main-page.png` -- none of which had changed that tick or the two before it. Pure and named so
/// the rule can be asserted without a screen, which is the only half of this that is not drawing.
[[nodiscard]] bool CaptureIsNews(std::uint32_t _capturedAt, std::uint32_t _tick) noexcept;

/// How tall a station stands, in world units, from what it produces (ADR-103).
///
/// **The map's third axis carries data.** Height was a fixed number per kind and said nothing; now
/// a stem is as tall as the system's yield, so a rich system is read at a glance and a poor one is
/// not. Pure and named so the rule can be asserted without a screen: zero -- a system nobody
/// holds, or a board the server has not priced -- stands at the plain height rather than lying on
/// the ground, and a capital never stands lower than it did before the axis meant anything.
[[nodiscard]] float StemHeightFor(std::uint32_t _production, bool _capital) noexcept;

/// How far a station's dashed footprint reaches on the plane, in world units, from what it
/// produces: the same number as the height, read the other way (ADR-103).
[[nodiscard]] float FootprintRadiusFor(std::uint32_t _production) noexcept;

/// Draws the galaxy, and returns what can be tapped in it.
///
/// Everything is projected through one camera, including the sky -- which is a sphere of directions
/// rather than a place, so it is in the same world and infinitely far away in it (ADR-032).
///
/// The stations' balls go to `_meshes` and everything else to `_shapes` and `_text`, in two shape
/// layers with a boundary between them (`ShapeRenderer::EndLayer`): the ground, the lanes, the
/// stems and the shadows are recorded first, then the boundary, then the rings, the arrowheads and
/// the badges that sit over a ball. The caller drains shapes, meshes, shapes, text (ADR-103).
[[nodiscard]] std::vector<MapHit> DrawMap(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, Neuron::MeshRenderer& _meshes,
                                          const MapFrame& _frame);

} // namespace Lockstep
