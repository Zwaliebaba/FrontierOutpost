#pragma once

#include "OrbitCamera.h"

#include <algorithm>
#include <cstdint>

namespace Lockstep
{

/// The galaxy's ground plane, and the camera looking at it.
///
/// It replaces `MapProjection`, which was an authored curve rather than a camera and could not be
/// orbited (ADR-017). What survives from it is the DESIGN SPACE: the graph is still authored in
/// the handoff's 800x560 coordinates, because that is what `Design/Screens/README.md` specifies
/// and what every position in the fixture was measured in. This is the one place those become
/// world coordinates.
///
/// Design x runs across the plane and design y runs INTO it, so the mapping is
/// `(designX, designY) -> (designX - 400, 0, designY - 280)`: one design unit is one world unit,
/// the plane lies at y = 0, and the middle of the design space is the world origin. Height is the
/// axis design space did not have -- it is what stems rise along.
class MapView
{
public:
  static constexpr float DESIGN_WIDTH = 800.0F;
  static constexpr float DESIGN_HEIGHT = 560.0F;
  static constexpr float DESIGN_CENTER_X = DESIGN_WIDTH * 0.5F;
  static constexpr float DESIGN_CENTER_Y = DESIGN_HEIGHT * 0.5F;

  /// Where the camera looks from when the screen opens.
  ///
  /// Yaw zero looks along -z, which puts the design space the same way round as the reference:
  /// Idris and Vesk on the left, the sealed region on the right. The pitch is TUNED, not derived
  /// -- it is the closest a real perspective camera gets to the angle the handoff was drawn at.
  /// An exact match was never available: the authored curve compressed the far half of the plane
  /// far more than any lens does, and ADR-017 says what that gave up.
  static constexpr float DEFAULT_YAW_RADIANS = 0.0F;
  static constexpr float DEFAULT_PITCH_RADIANS = 0.62F;

  /// How much room to leave around the content when the camera frames it. 1.0 would put the
  /// outermost system exactly on the edge of the pane, where its label would be clipped and it
  /// would look like an accident.
  static constexpr float FRAMING_MARGIN = 1.06F;

  /// How far a pixel of drag turns the camera. Horizontal and vertical differ because the axes
  /// do: yaw runs all the way round, pitch has about 73 degrees of travel between its clamps, so
  /// the same sensitivity on both would make the tilt slam into its limits.
  static constexpr float YAW_RADIANS_PER_PIXEL = 0.006F;
  static constexpr float PITCH_RADIANS_PER_PIXEL = 0.004F;

  /// How far a wheel notch or a pinch step moves the zoom, and how far it may go (ADR-090).
  ///
  /// **The bounds are of the AUTHORED framing, not of a distance.** `FrameContent` recomputes the
  /// distance from the galaxy's extent on every frame -- a different pane, a different graph and a
  /// different aspect all change it -- so a zoom kept as a distance would mean something different
  /// every time the content did. Kept as a factor, `1.0` is always exactly what the map opens at.
  ///
  /// 2.5 in is enough to read a crowded cluster; 0.6 out is enough to see the sealed region's rim
  /// with the whole galaxy inside it, and going further only adds emptiness. Twelve percent a notch
  /// takes about eight of them to cross the range, which is a wheel gesture rather than a flick.
  static constexpr float ZOOM_PER_STEP = 1.12F;
  static constexpr float ZOOM_NEAREST = 2.5F;
  static constexpr float ZOOM_FARTHEST = 0.6F;

  MapView() noexcept
  {
    m_camera.SetOrientation(DEFAULT_YAW_RADIANS, DEFAULT_PITCH_RADIANS);
  }

  void SetViewport(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels) noexcept
  {
    m_camera.SetViewport(_xPixels, _yPixels, _widthPixels, _heightPixels);
    m_viewportAspect = _widthPixels / _heightPixels;
  }

  /// Aims the camera at the middle of the galaxy and pulls back until all of it fits, at every
  /// orientation the player can reach.
  ///
  /// It is COMPUTED rather than tuned, and that is the point: the fixture's galaxy is illustrative
  /// (Design/Screens/README.md "Fidelity") and the generator will make a different one every
  /// match, so a hand-picked distance would frame this sample and no other.
  ///
  /// THE CONTENT IS A FLAT DISC, NOT A BALL, and fitting it as one matters. A galaxy lies in the
  /// ground plane, so it is `_groundRadius` wide however the camera is yawed, but only
  /// `_groundRadius * sin(pitch)` tall -- at the default low angle barely half as tall as it is
  /// wide. Bounding it with a sphere and fitting that was the first attempt and framed the map
  /// about a quarter smaller than it needed to be, all of it wasted above and below.
  ///
  /// The distance is solved ONCE, against the widest the content can ever look: full width
  /// horizontally, and the vertical extent at the steepest pitch the camera is allowed to reach.
  /// So it never re-fits while the player orbits -- a camera that dollied in and out as you
  /// tilted it would be its own kind of wrong.
  void FrameContent(const Neuron::OrbitCamera::WorldPoint& _center, float _groundRadius, float _height) noexcept
  {
    m_camera.SetTarget(_center);

    const float halfVertical = Neuron::OrbitCamera::DEFAULT_FIELD_OF_VIEW_RADIANS * 0.5F;
    const float halfHorizontal = std::atan(std::tan(halfVertical) * m_viewportAspect);

    const float forWidth = _groundRadius / std::tan(halfHorizontal);
    const float tallest = _groundRadius * std::sin(Neuron::OrbitCamera::MAX_PITCH_RADIANS) + _height;
    const float forHeight = tallest / std::tan(halfVertical);

    // The authored distance, then the player's zoom. Dividing rather than multiplying because a
    // bigger zoom means a closer eye, and the factor reads as a magnification everywhere else.
    m_camera.SetDistance(std::max(forWidth, forHeight) * FRAMING_MARGIN / m_zoom);
  }

  /// Moves the zoom by whole steps. Positive is in. True when it actually moved, which is false at
  /// either end of the range -- so a wheel spun against the stop costs no redraw (ADR-047).
  bool Zoom(std::int32_t _steps) noexcept
  {
    const float was = m_zoom;
    float wanted = m_zoom;
    for (std::int32_t step = 0; step < _steps; ++step)
    {
      wanted *= ZOOM_PER_STEP;
    }
    for (std::int32_t step = 0; step > _steps; --step)
    {
      wanted /= ZOOM_PER_STEP;
    }
    m_zoom = std::clamp(wanted, ZOOM_FARTHEST, ZOOM_NEAREST);
    return m_zoom != was;
  }

  /// Whether the camera is exactly where the map opened: the authored orientation and no zoom.
  /// What decides whether `RESET` is drawn at all -- a control that does nothing is one to leave off
  /// the screen rather than to draw dim (ADR-090).
  [[nodiscard]] bool AtAuthoredFraming() const noexcept
  {
    return m_zoom == 1.0F && m_camera.YawRadians() == DEFAULT_YAW_RADIANS && m_camera.PitchRadians() == DEFAULT_PITCH_RADIANS;
  }

  /// Drag to orbit, as though the ground itself were under the finger: drag right and the galaxy
  /// goes right, drag up and it tips away from you so you see more of it from above.
  ///
  /// BOTH SIGNS ARE NEGATIVE and both are load-bearing. The camera moves opposite the finger,
  /// because what the player is grabbing is the world rather than the viewpoint -- swinging the
  /// eye left is what sweeps the galaxy right. Dragging up with the other sign was tried and is
  /// wrong in a way that is obvious the moment you do it: the view flattens towards the horizon
  /// when everything about the gesture says it should rise (ADR-017).
  void Drag(float _deltaXPixels, float _deltaYPixels) noexcept
  {
    m_camera.Orbit(-_deltaXPixels * YAW_RADIANS_PER_PIXEL, -_deltaYPixels * PITCH_RADIANS_PER_PIXEL);
  }

  void ResetView() noexcept
  {
    m_camera.SetOrientation(DEFAULT_YAW_RADIANS, DEFAULT_PITCH_RADIANS);
    m_zoom = 1.0F;
  }

  /// A point on the ground plane, from design coordinates.
  [[nodiscard]] static Neuron::OrbitCamera::WorldPoint Ground(float _designX, float _designY) noexcept
  {
    return Neuron::OrbitCamera::WorldPoint{_designX - DESIGN_CENTER_X, 0.0F, _designY - DESIGN_CENTER_Y};
  }

  /// A point above the ground plane -- the top of a stem, a hovering fleet.
  [[nodiscard]] static Neuron::OrbitCamera::WorldPoint Above(float _designX, float _designY, float _height) noexcept
  {
    return Neuron::OrbitCamera::WorldPoint{_designX - DESIGN_CENTER_X, _height, _designY - DESIGN_CENTER_Y};
  }

  [[nodiscard]] const Neuron::OrbitCamera& Camera() const noexcept
  {
    return m_camera;
  }

private:
  Neuron::OrbitCamera m_camera;
  float m_viewportAspect = 1.0F;
  /// A magnification of the authored framing, not a distance. 1.0 is what the map opens at.
  float m_zoom = 1.0F;
};

} // namespace Lockstep
