// ShapeRenderer.cpp -- rectangles, lines and ellipses, tessellated on the CPU into one triangle
// list. See ShapeRenderer.h for why there is no signed-distance shader here, and ShapeBackend for
// how the list reaches a GPU.

#include "pch.h"
#include "ShapeRenderer.h"

namespace Neuron
{

namespace
{

constexpr float TWO_PI = 6.28318530717958647692F;

} // namespace

void ShapeRenderer::BeginFrame()
{
  // Cleared, not freed, and reserved on the first frame only: capacity survives clear(), so this
  // is a no-op from the second frame onwards and no frame's recording allocates.
  m_vertices.clear();
  m_vertices.reserve(MAX_VERTICES_PER_FRAME);
  m_takenThisFrame = 0;
  m_layerEnds.clear();
}

void ShapeRenderer::EndLayer()
{
  m_layerEnds.push_back(m_vertices.size());
}

void ShapeRenderer::AppendShadedTriangle(float _axPixels, float _ayPixels, std::uint32_t _aColor, float _bxPixels, float _byPixels,
                                         std::uint32_t _bColor, float _cxPixels, float _cyPixels, std::uint32_t _cColor)
{
  ASSERT_TEXT(m_vertices.size() + 3 <= MAX_VERTICES_PER_FRAME,
              L"More interface geometry in one frame than ShapeRenderer::MAX_VERTICES_PER_FRAME allows.");

  m_vertices.push_back({_axPixels, _ayPixels, _aColor});
  m_vertices.push_back({_bxPixels, _byPixels, _bColor});
  m_vertices.push_back({_cxPixels, _cyPixels, _cColor});
}

void ShapeRenderer::AppendTriangle(float _axPixels, float _ayPixels, float _bxPixels, float _byPixels, float _cxPixels, float _cyPixels,
                                   std::uint32_t _packedColor)
{
  AppendShadedTriangle(_axPixels, _ayPixels, _packedColor, _bxPixels, _byPixels, _packedColor, _cxPixels, _cyPixels, _packedColor);
}

void ShapeRenderer::AppendQuad(float _axPixels, float _ayPixels, float _bxPixels, float _byPixels, float _cxPixels, float _cyPixels,
                               float _dxPixels, float _dyPixels, std::uint32_t _packedColor)
{
  AppendTriangle(_axPixels, _ayPixels, _bxPixels, _byPixels, _cxPixels, _cyPixels, _packedColor);
  AppendTriangle(_axPixels, _ayPixels, _cxPixels, _cyPixels, _dxPixels, _dyPixels, _packedColor);
}

void ShapeRenderer::FillRect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, const Color& _color)
{
  if (_widthPixels <= 0.0F || _heightPixels <= 0.0F)
  {
    return;
  }

  const float right = _xPixels + _widthPixels;
  const float bottom = _yPixels + _heightPixels;
  AppendQuad(_xPixels, _yPixels, right, _yPixels, right, bottom, _xPixels, bottom, Pack(_color));
}

void ShapeRenderer::StrokeRect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, const Color& _color,
                               float _thicknessPixels)
{
  if (_widthPixels <= 0.0F || _heightPixels <= 0.0F)
  {
    return;
  }

  const float t = std::min(_thicknessPixels, std::min(_widthPixels, _heightPixels) * 0.5F);
  FillRect(_xPixels, _yPixels, _widthPixels, t, _color);
  FillRect(_xPixels, _yPixels + _heightPixels - t, _widthPixels, t, _color);
  // The left and right edges stop short of the top and bottom ones rather than overlapping them.
  // With no blending an overlap is invisible, but it doubles the triangles at every corner.
  FillRect(_xPixels, _yPixels + t, t, _heightPixels - 2.0F * t, _color);
  FillRect(_xPixels + _widthPixels - t, _yPixels + t, t, _heightPixels - 2.0F * t, _color);
}

void ShapeRenderer::Line(float _x0Pixels, float _y0Pixels, float _x1Pixels, float _y1Pixels, const Color& _color, float _thicknessPixels)
{
  const float deltaX = _x1Pixels - _x0Pixels;
  const float deltaY = _y1Pixels - _y0Pixels;
  const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
  if (length <= 0.0F || _thicknessPixels <= 0.0F)
  {
    return;
  }

  // The normal, scaled to half the thickness: the quad is the segment swept sideways by it.
  const float halfX = (-deltaY / length) * _thicknessPixels * 0.5F;
  const float halfY = (deltaX / length) * _thicknessPixels * 0.5F;

  AppendQuad(_x0Pixels + halfX, _y0Pixels + halfY, _x1Pixels + halfX, _y1Pixels + halfY, _x1Pixels - halfX, _y1Pixels - halfY,
             _x0Pixels - halfX, _y0Pixels - halfY, Pack(_color));
}

void ShapeRenderer::StrokePolygon(std::span<const ShapePoint> _points, const Color& _color, float _thicknessPixels)
{
  if (_points.size() < 2)
  {
    return;
  }

  for (std::size_t corner = 0; corner < _points.size(); ++corner)
  {
    const ShapePoint& from = _points[corner];
    const ShapePoint& to = _points[(corner + 1) % _points.size()];
    Line(from.xPixels, from.yPixels, to.xPixels, to.yPixels, _color, _thicknessPixels);
  }
}

void ShapeRenderer::DashedLine(float _x0Pixels, float _y0Pixels, float _x1Pixels, float _y1Pixels, const Color& _color,
                               float _thicknessPixels, float _dashPixels, float _gapPixels, float _offsetPixels)
{
  const float deltaX = _x1Pixels - _x0Pixels;
  const float deltaY = _y1Pixels - _y0Pixels;
  const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
  const float period = _dashPixels + _gapPixels;
  if (length <= 0.0F || period <= 0.0F)
  {
    return;
  }

  const float stepX = deltaX / length;
  const float stepY = deltaY / length;

  // The offset is taken modulo the period, so an animation driven by a clock that has been running
  // for an hour is the same arithmetic as one that started a moment ago. Reduced first and made
  // positive, because `fmod` keeps the sign of its left operand and a negative phase would start
  // the walk past the first endpoint.
  float phase = std::fmod(_offsetPixels, period);
  if (phase < 0.0F)
  {
    phase += period;
  }

  // Counted by dash INDEX rather than by accumulating a float, so the last dash of a long line
  // lands where arithmetic says it should rather than where the accumulated error left it. The
  // walk starts one whole period behind the first endpoint: the dash that the phase has pushed
  // only partly onto the line is the one that makes the pattern appear to enter it.
  const auto dashes = static_cast<std::uint32_t>(std::ceil((length + period) / period));
  for (std::uint32_t dash = 0; dash < dashes; ++dash)
  {
    const float travelled = static_cast<float>(dash) * period - period + phase;
    const float dashStart = std::max(travelled, 0.0F);
    const float dashEnd = std::min(travelled + _dashPixels, length);
    if (dashEnd <= dashStart)
    {
      continue;
    }
    Line(_x0Pixels + stepX * dashStart, _y0Pixels + stepY * dashStart, _x0Pixels + stepX * dashEnd, _y0Pixels + stepY * dashEnd, _color,
         _thicknessPixels);
  }
}

void ShapeRenderer::FillEllipse(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels, const Color& _color)
{
  if (_radiusXPixels <= 0.0F || _radiusYPixels <= 0.0F)
  {
    return;
  }

  const std::uint32_t segments = SegmentsForRadius(std::max(_radiusXPixels, _radiusYPixels));
  const std::uint32_t packed = Pack(_color);

  // A fan from the center. Every triangle is degenerate-free because the radii are positive and
  // the step is a whole fraction of a turn.
  float previousX = _centerXPixels + _radiusXPixels;
  float previousY = _centerYPixels;
  for (std::uint32_t segment = 1; segment <= segments; ++segment)
  {
    const float angle = TWO_PI * static_cast<float>(segment) / static_cast<float>(segments);
    const float x = _centerXPixels + _radiusXPixels * std::cos(angle);
    const float y = _centerYPixels + _radiusYPixels * std::sin(angle);
    AppendTriangle(_centerXPixels, _centerYPixels, previousX, previousY, x, y, packed);
    previousX = x;
    previousY = y;
  }
}

void ShapeRenderer::StrokeEllipse(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels,
                                  const Color& _color, float _thicknessPixels)
{
  if (_radiusXPixels <= 0.0F || _radiusYPixels <= 0.0F)
  {
    return;
  }

  const std::uint32_t segments = SegmentsForRadius(std::max(_radiusXPixels, _radiusYPixels));
  float previousX = _centerXPixels + _radiusXPixels;
  float previousY = _centerYPixels;
  for (std::uint32_t segment = 1; segment <= segments; ++segment)
  {
    const float angle = TWO_PI * static_cast<float>(segment) / static_cast<float>(segments);
    const float x = _centerXPixels + _radiusXPixels * std::cos(angle);
    const float y = _centerYPixels + _radiusYPixels * std::sin(angle);
    Line(previousX, previousY, x, y, _color, _thicknessPixels);
    previousX = x;
    previousY = y;
  }
}

void ShapeRenderer::DashedEllipse(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels,
                                  const Color& _color, float _thicknessPixels, float _dashPixels, float _gapPixels)
{
  if (_radiusXPixels <= 0.0F || _radiusYPixels <= 0.0F)
  {
    return;
  }

  // The dash pattern is walked in CHORD length around the outline. An ellipse's arc length has no
  // closed form, but the chord of one segment is within a fraction of a pixel of its arc at the
  // segment counts SegmentsForRadius returns -- and a dashed outline is a texture, not a
  // measurement, so paying for a numerical arc length here would buy nothing anybody can see.
  const std::uint32_t segments = SegmentsForRadius(std::max(_radiusXPixels, _radiusYPixels));
  const float period = _dashPixels + _gapPixels;
  if (period <= 0.0F)
  {
    return;
  }

  float previousX = _centerXPixels + _radiusXPixels;
  float previousY = _centerYPixels;
  float travelled = 0.0F;

  for (std::uint32_t segment = 1; segment <= segments; ++segment)
  {
    const float angle = TWO_PI * static_cast<float>(segment) / static_cast<float>(segments);
    const float x = _centerXPixels + _radiusXPixels * std::cos(angle);
    const float y = _centerYPixels + _radiusYPixels * std::sin(angle);

    // A segment is drawn when its MIDPOINT falls in the "on" part of the pattern. Subdividing the
    // segment at the dash boundary would be more exact and would also make the dashes uneven,
    // because the segments are not equal length on an ellipse.
    const float chordX = x - previousX;
    const float chordY = y - previousY;
    const float chord = std::sqrt(chordX * chordX + chordY * chordY);
    if (std::fmod(travelled + chord * 0.5F, period) < _dashPixels)
    {
      Line(previousX, previousY, x, y, _color, _thicknessPixels);
    }

    travelled += chord;
    previousX = x;
    previousY = y;
  }
}

void ShapeRenderer::FillTriangle(float _axPixels, float _ayPixels, float _bxPixels, float _byPixels, float _cxPixels, float _cyPixels,
                                 const Color& _color)
{
  AppendTriangle(_axPixels, _ayPixels, _bxPixels, _byPixels, _cxPixels, _cyPixels, Pack(_color));
}

void ShapeRenderer::FillVerticalGradient(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, const Color& _top,
                                         const Color& _middle, float _middleFraction, const Color& _bottom)
{
  if (_widthPixels <= 0.0F || _heightPixels <= 0.0F)
  {
    return;
  }

  const float right = _xPixels + _widthPixels;
  const float split = _yPixels + _heightPixels * std::clamp(_middleFraction, 0.0F, 1.0F);
  const float bottom = _yPixels + _heightPixels;

  const std::uint32_t top = Pack(_top);
  const std::uint32_t middle = Pack(_middle);
  const std::uint32_t low = Pack(_bottom);

  // Two bands, each two triangles. The interpolator does the rest.
  //
  // **It interpolates in full precision and then quantizes to eight bits, which is where banding
  // comes from rather than where it is avoided.** Measured down the map's own gradient, the steps
  // are one level at a time -- red every 40 pixels, blue every 15 -- which is as smooth as R8G8B8A8
  // can be and is still a step somebody with a good display may see on an area this large. Removing
  // it needs a dither in the pixel shader, which nobody has asked for.
  AppendShadedTriangle(_xPixels, _yPixels, top, right, _yPixels, top, right, split, middle);
  AppendShadedTriangle(_xPixels, _yPixels, top, right, split, middle, _xPixels, split, middle);
  AppendShadedTriangle(_xPixels, split, middle, right, split, middle, right, bottom, low);
  AppendShadedTriangle(_xPixels, split, middle, right, bottom, low, _xPixels, bottom, low);
}

void ShapeRenderer::FillRadialGradient(float _centerXPixels, float _centerYPixels, float _radiusXPixels, float _radiusYPixels,
                                       const Color& _center, const Color& _rim)
{
  if (_radiusXPixels <= 0.0F || _radiusYPixels <= 0.0F)
  {
    return;
  }

  const std::uint32_t segments = SegmentsForRadius(std::max(_radiusXPixels, _radiusYPixels));
  const std::uint32_t center = Pack(_center);
  const std::uint32_t rim = Pack(_rim);

  float previousX = _centerXPixels + _radiusXPixels;
  float previousY = _centerYPixels;
  for (std::uint32_t segment = 1; segment <= segments; ++segment)
  {
    const float angle = TWO_PI * static_cast<float>(segment) / static_cast<float>(segments);
    const float x = _centerXPixels + _radiusXPixels * std::cos(angle);
    const float y = _centerYPixels + _radiusYPixels * std::sin(angle);
    AppendShadedTriangle(_centerXPixels, _centerYPixels, center, previousX, previousY, rim, x, y, rim);
    previousX = x;
    previousY = y;
  }
}

ShapeRenderer::Batch ShapeRenderer::TakeUnflushed() noexcept
{
  // Up to the next boundary when there is one, and that boundary is spent; otherwise everything.
  std::size_t end = m_vertices.size();
  if (!m_layerEnds.empty())
  {
    end = std::min(m_layerEnds.front(), end);
    m_layerEnds.erase(m_layerEnds.begin());
  }

  const Batch batch = {
    .vertices = std::span{m_vertices}.subspan(m_takenThisFrame, end - m_takenThisFrame),
    .firstVertex = static_cast<std::uint32_t>(m_takenThisFrame),
  };
  m_takenThisFrame = end;
  return batch;
}

} // namespace Neuron
