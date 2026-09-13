#pragma once

namespace Neuron
{

/// Where the canvas sits inside the surface it is presented on, and at what whole-number scale.
///
/// THIS HEADER NAMES NO GRAPHICS API AND NO OPERATING SYSTEM, and that is the whole point of it. A
/// window on Windows and a surface handed over by Android or iOS differ in how you ask for their
/// size and in nothing else that reaches this arithmetic: the canvas is magnified by a whole
/// number, centered in whole pixels, and everything outside it is letterbox (ADR-075). So the
/// arithmetic is written once, here, and a second platform reuses this file unchanged.
///
/// R8: a plain aggregate. It is passed by value to anything that needs it and stored by value by
/// PointerInput; it is five integers and there is nothing to own.
struct Presentation
{
  /// The canvas. Fixed, not configurable: Design/README.md section 1 makes it a design constraint
  /// rather than a setting, and every number in Design/UI/DESIGN-GUIDELINES.md is one of these
  /// pixels (ADR-075).
  static constexpr std::uint32_t CANVAS_WIDTH_PIXELS = 1280;
  static constexpr std::uint32_t CANVAS_HEIGHT_PIXELS = 720;

  /// A whole number and never zero. Nothing on the path from a vertex to the display resamples
  /// anything, which is exactly what a scale of 1.5 would break: the canvas is read with an
  /// integer texel load, so a fractional scale has no meaning here rather than a blurry one
  /// (ADR-075).
  std::uint32_t scale = 1;
  /// Where the canvas's top-left corner lands on the surface. Everything outside
  /// [offset, offset + canvas * scale) in either axis is letterbox and is cleared to black.
  std::uint32_t offsetXPixels = 0;
  std::uint32_t offsetYPixels = 0;
  std::uint32_t surfaceWidthPixels = CANVAS_WIDTH_PIXELS;
  std::uint32_t surfaceHeightPixels = CANVAS_HEIGHT_PIXELS;

  /// The largest whole scale a surface of this size has room for, and never less than 1.
  ///
  /// **The arithmetic lives here rather than beside the window it is asked about**, because there
  /// are two callers with different ideas of what the surface is -- a window's work area minus its
  /// frame, and a whole monitor (ADR-076) -- and a phone's is a third. One divide each and the
  /// smaller answer; no multiplication, so nothing can overflow on the way.
  ///
  /// Never less than 1: a display too small for the canvas gets a surface it cannot show all of,
  /// which a person can see and work around, where a scale of zero is a division by zero in
  /// ToCanvas.
  [[nodiscard]] static constexpr std::uint32_t LargestScaleFor(std::uint32_t _surfaceWidthPixels,
                                                               std::uint32_t _surfaceHeightPixels) noexcept
  {
    const std::uint32_t byWidth = _surfaceWidthPixels / CANVAS_WIDTH_PIXELS;
    const std::uint32_t byHeight = _surfaceHeightPixels / CANVAS_HEIGHT_PIXELS;
    const std::uint32_t fits = byWidth < byHeight ? byWidth : byHeight;

    return fits < 1 ? 1 : fits;
  }

  /// The canvas centered in a surface of the given size at the given scale.
  ///
  /// A surface SMALLER than the presented canvas gets a zero offset rather than a negative one,
  /// which crops instead of wrapping. Nothing produces that today -- the scale is chosen to fit --
  /// and the alternative is an unsigned subtraction that underflows to two billion.
  ///
  /// _scale of 0 is clamped to 1. This is the type's invariant rather than defensiveness: ToCanvas
  /// divides by it, and a Presentation that cannot be mapped through is not a state worth being
  /// able to represent.
  [[nodiscard]] static constexpr Presentation For(std::uint32_t _surfaceWidthPixels, std::uint32_t _surfaceHeightPixels,
                                                  std::uint32_t _scale) noexcept
  {
    const std::uint32_t scale = _scale == 0 ? 1 : _scale;
    const std::uint32_t presentedWidthPixels = CANVAS_WIDTH_PIXELS * scale;
    const std::uint32_t presentedHeightPixels = CANVAS_HEIGHT_PIXELS * scale;

    return Presentation{
      .scale = scale,
      .offsetXPixels = _surfaceWidthPixels > presentedWidthPixels ? (_surfaceWidthPixels - presentedWidthPixels) / 2 : 0,
      .offsetYPixels = _surfaceHeightPixels > presentedHeightPixels ? (_surfaceHeightPixels - presentedHeightPixels) / 2 : 0,
      .surfaceWidthPixels = _surfaceWidthPixels,
      .surfaceHeightPixels = _surfaceHeightPixels,
    };
  }

  /// Maps a point on the surface onto the canvas, and says whether it landed on the canvas at all.
  ///
  /// The arithmetic stays in float on purpose. At a scale of 2 a surface pixel is half a canvas
  /// pixel, and the pages already take their positions as float -- rounding here would throw away
  /// precision the caller has a use for and would make a drag at scale 2 move in steps of two.
  ///
  /// False means the point is in the letterbox. A caller deciding whether to START something --
  /// a press, a hover -- honours that; a caller finishing something already begun, such as the
  /// move or the release of a contact that is already down, uses the coordinates anyway, because
  /// a drag that leaves the canvas still has to end cleanly.
  [[nodiscard]] bool ToCanvas(float _surfaceXPixels, float _surfaceYPixels, float& _outXPixels, float& _outYPixels) const noexcept
  {
    _outXPixels = (_surfaceXPixels - static_cast<float>(offsetXPixels)) / static_cast<float>(scale);
    _outYPixels = (_surfaceYPixels - static_cast<float>(offsetYPixels)) / static_cast<float>(scale);

    return OnCanvas(_outXPixels, _outYPixels);
  }

  /// Whether a point ALREADY in canvas pixels is on the canvas. The far edge is exclusive: 1280 is
  /// the first column that is not the canvas.
  ///
  /// Separate from ToCanvas because its two callers want different halves of it. Deciding whether
  /// to BEGIN something -- a press, a hover -- asks both questions; continuing something already
  /// begun wants the coordinates and not the verdict.
  [[nodiscard]] static constexpr bool OnCanvas(float _canvasXPixels, float _canvasYPixels) noexcept
  {
    return _canvasXPixels >= 0.0F && _canvasXPixels < static_cast<float>(CANVAS_WIDTH_PIXELS) && _canvasYPixels >= 0.0F &&
           _canvasYPixels < static_cast<float>(CANVAS_HEIGHT_PIXELS);
  }
};

} // namespace Neuron
