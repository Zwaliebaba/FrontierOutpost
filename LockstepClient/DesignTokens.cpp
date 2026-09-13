// DesignTokens.cpp -- the four helpers behind the tokens.
//
// Four functions rather than none, because `Uppercased` allocates and the two text helpers measure,
// and a header that did all three inline would put them in every translation unit that wanted a
// colour.

#include "pch.h"
#include "DesignTokens.h"

#include <algorithm>
#include <cmath>

namespace Lockstep
{

std::string Uppercased(std::string_view _text)
{
  std::string shouted{_text};
  std::ranges::transform(shouted, shouted.begin(), [](unsigned char _letter) { return static_cast<char>(std::toupper(_letter)); });
  return shouted;
}

void DrawCentered(Neuron::FontRenderer& _text, float _centerXPixels, std::int32_t _yPixels, std::string_view _string,
                  const Neuron::Color& _color, Neuron::Face _face, std::uint32_t _scale)
{
  const auto width = static_cast<float>(Neuron::FontRenderer::MeasurePixels(_string, _face, _scale));
  _text.DrawText(static_cast<std::int32_t>(std::lround(_centerXPixels - width * 0.5F)), _yPixels, _string, _color, _face, _scale);
}

void DrawRight(Neuron::FontRenderer& _text, float _rightXPixels, std::int32_t _yPixels, std::string_view _string,
               const Neuron::Color& _color, Neuron::Face _face, std::uint32_t _scale)
{
  const auto width = static_cast<float>(Neuron::FontRenderer::MeasurePixels(_string, _face, _scale));
  _text.DrawText(static_cast<std::int32_t>(std::lround(_rightXPixels - width)), _yPixels, _string, _color, _face, _scale);
}

std::int32_t CenterTextY(float _bandTop, float _bandHeight, Neuron::Face _face, std::uint32_t _scale) noexcept
{
  const float glyph = static_cast<float>(Neuron::FontRenderer::GlyphHeightPixels(_face, _scale));
  return static_cast<std::int32_t>(std::floor(_bandTop + (_bandHeight - glyph) * 0.5F));
}

float BandTopForText(std::int32_t _textY, float _bandHeight, Neuron::Face _face, std::uint32_t _scale) noexcept
{
  const float glyph = static_cast<float>(Neuron::FontRenderer::GlyphHeightPixels(_face, _scale));
  return static_cast<float>(_textY) - (_bandHeight - glyph) * 0.5F;
}

} // namespace Lockstep
