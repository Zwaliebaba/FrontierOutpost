#pragma once

#include "MapView.h"
#include "MatchState.h"

#include "FontRenderer.h"
#include "ShapeRenderer.h"
#include "Starfield.h"

#include <cstdint>
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
  std::int32_t system = EventRefs::NONE;
  std::int32_t fleet = EventRefs::NONE;
  std::int32_t fleetsAt = EventRefs::NONE;
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
};

/// Whether a capture is still news, and so still labelled on the map (ADR-082).
///
/// **Three ticks, and then it is the map rather than the news.** A board a player is winning wore a
/// standing `CAPTURED Tn` under every system they had ever taken -- six of them on
/// `01-main-page.png` -- none of which had changed that tick or the two before it. Pure and named so
/// the rule can be asserted without a screen, which is the only half of this that is not drawing.
[[nodiscard]] bool CaptureIsNews(std::uint32_t _capturedAt, std::uint32_t _tick) noexcept;

/// Draws the galaxy, and returns what can be tapped in it.
///
/// Everything is projected through one camera, including the sky -- which is a sphere of directions
/// rather than a place, so it is in the same world and infinitely far away in it (ADR-032).
[[nodiscard]] std::vector<MapHit> DrawMap(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, const MapFrame& _frame);

} // namespace Lockstep
