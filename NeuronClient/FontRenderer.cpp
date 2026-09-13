// FontRenderer.cpp -- Font.h's baked atlas as data, and strings turned into quads. See
// FontBackend for how the quads and the atlas reach a GPU.

#include "pch.h"
#include "FontRenderer.h"

namespace Neuron
{

std::vector<std::string> FontRenderer::WrapToWidth(std::string_view _text, std::uint32_t _widthPixels, Face _face, std::uint32_t _scale)
{
  std::vector<std::string> lines;

  // A line with room for no glyph at all has nowhere to put the text, and every loop below would
  // make no progress. This is the character-count form's `_maxCharacters == 0` guard, said in
  // pixels.
  if (PrefixThatFits(" ", _widthPixels, _face, _scale) == 0)
  {
    return lines;
  }

  std::string current;
  std::size_t start = 0;
  while (start <= _text.size())
  {
    const std::size_t space = _text.find(' ', start);
    const std::size_t end = (space == std::string_view::npos) ? _text.size() : space;
    std::string_view word = _text.substr(start, end - start);

    // A word wider than the line is hard-broken rather than allowed to overflow. Nothing in the
    // reference copy is, but a system name from a server is not something this screen gets to
    // assume anything about.
    while (MeasurePixels(word, _face, _scale) > _widthPixels)
    {
      if (!current.empty())
      {
        lines.push_back(current);
        current.clear();
      }
      // At least one character, always. A single glyph wider than the whole line cannot be broken
      // any smaller, and taking none of it would spin here forever.
      const std::size_t fitted = PrefixThatFits(word, _widthPixels, _face, _scale);
      const std::size_t taken = (fitted == 0) ? 1 : fitted;
      lines.emplace_back(word.substr(0, taken));
      word = word.substr(taken);
    }

    const std::uint32_t needed =
      current.empty() ? MeasurePixels(word, _face, _scale)
                      : MeasurePixels(current, _face, _scale) + AdvanceOf(U' ', _face, _scale) + MeasurePixels(word, _face, _scale);
    if (needed > _widthPixels && !current.empty())
    {
      lines.push_back(current);
      current.assign(word);
    }
    else
    {
      if (!current.empty())
      {
        current.push_back(' ');
      }
      current.append(word);
    }

    if (space == std::string_view::npos)
    {
      break;
    }
    start = space + 1;
  }

  if (!current.empty())
  {
    lines.push_back(current);
  }
  return lines;
}

void FontRenderer::BeginFrame()
{
  // Cleared, not freed, and reserved on the first frame only: capacity survives clear(), so this
  // is a no-op from the second frame onwards and no frame's recording allocates.
  m_vertices.clear();
  m_vertices.reserve(MAX_VERTICES_PER_FRAME);
  m_takenThisFrame = 0;
  m_drawnStrings.clear();
  ClearClipRect();
}

void FontRenderer::SetClipRect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels) noexcept
{
  m_clipLeftPixels = _xPixels;
  m_clipTopPixels = _yPixels;
  m_clipRightPixels = _xPixels + _widthPixels;
  m_clipBottomPixels = _yPixels + _heightPixels;
  m_clipping = true;
}

void FontRenderer::ClearClipRect() noexcept
{
  m_clipping = false;
}

void FontRenderer::DrawText(std::int32_t _xPixels, std::int32_t _yPixels, std::string_view _text, const Color& _color, Face _face,
                            std::uint32_t _scale)
{
  ASSERT_TEXT(_scale > 0, L"A glyph scale of zero would draw nothing and is a caller mistake, not a way to hide text.");

  // Recorded BEFORE the string becomes glyph boxes, because by then it is boxes with no word
  // boundaries and no face. This is what lets `LockstepTests` hold the face rule to account
  // (ADR-074).
  m_drawnStrings.emplace_back(std::string{_text}, _face);

  // `_yPixels` is the top of the LINE, not the top of the first glyph's ink, so a string keeps
  // landing where its caller put it whatever the face does with bearings. The baseline is that
  // many pixels down; a glyph sits `bearingY` above the baseline.
  const std::uint32_t ascent = FaceOf(_face).ascent * _scale;
  const std::uint32_t boxHeight = GlyphHeightPixels(_face, _scale);
  const std::uint32_t color = Pack(_color);

  std::int32_t pen = _xPixels;
  for (std::size_t at = 0; at < _text.size();)
  {
    const Decoded decoded = DecodeUtf8(_text, at);
    at += decoded.bytes;

    const FontGlyph& glyph = GlyphOf(decoded.codepoint, _face);
    const auto advance = static_cast<std::int32_t>(glyph.advance * _scale);

    // The clip box is the ADVANCE box and not the ink box, so that whether a glyph is clipped
    // depends on where the cursor is rather than on how much ink the letter happens to have.
    // Whole glyphs rather than partial ones, for the reason SetClipRect gives.
    const auto boxLeft = static_cast<float>(pen);
    const auto boxTop = static_cast<float>(_yPixels);
    const float boxRight = boxLeft + static_cast<float>(advance);
    const float boxBottom = boxTop + static_cast<float>(boxHeight);
    const bool clipped = m_clipping && (boxLeft < m_clipLeftPixels || boxRight > m_clipRightPixels || boxTop < m_clipTopPixels ||
                                        boxBottom > m_clipBottomPixels);

    // A space has no ink and needs no quad. The cursor still advances, so a clipped or blank glyph
    // leaves the rest of the string where it would have been -- a clipped label loses letters, it
    // does not shuffle up.
    if (!clipped && glyph.width != 0 && glyph.height != 0)
    {
      ASSERT_TEXT(m_vertices.size() + VERTICES_PER_GLYPH <= MAX_VERTICES_PER_FRAME,
                  L"More text in one frame than FontRenderer::MAX_CHARACTERS_PER_FRAME allows.");

      const auto left = static_cast<float>(pen + glyph.bearingX * static_cast<std::int32_t>(_scale));
      const auto top =
        static_cast<float>(_yPixels + static_cast<std::int32_t>(ascent) - glyph.bearingY * static_cast<std::int32_t>(_scale));
      const float right = left + static_cast<float>(glyph.width * _scale);
      const float bottom = top + static_cast<float>(glyph.height * _scale);

      // The quad spans _scale screen pixels per texel, so the interpolator hands the pixel shader
      // a fractional texel and its truncation is what turns one texel into a _scale-square block
      // of pixels -- the same integer divide the resolve pass did before ADR-011 removed it.
      const auto atlasLeft = static_cast<float>(glyph.atlasX);
      const auto atlasTop = static_cast<float>(glyph.atlasY);
      const float atlasRight = atlasLeft + static_cast<float>(glyph.width);
      const float atlasBottom = atlasTop + static_cast<float>(glyph.height);

      m_vertices.push_back({left, top, atlasLeft, atlasTop, color});
      m_vertices.push_back({right, top, atlasRight, atlasTop, color});
      m_vertices.push_back({left, bottom, atlasLeft, atlasBottom, color});
      m_vertices.push_back({right, top, atlasRight, atlasTop, color});
      m_vertices.push_back({right, bottom, atlasRight, atlasBottom, color});
      m_vertices.push_back({left, bottom, atlasLeft, atlasBottom, color});
    }

    pen += advance;
  }
}

FontRenderer::Batch FontRenderer::TakeUnflushed() noexcept
{
  const Batch batch = {
    .vertices = std::span{m_vertices}.subspan(m_takenThisFrame),
    .firstVertex = static_cast<std::uint32_t>(m_takenThisFrame),
  };
  m_takenThisFrame = m_vertices.size();
  return batch;
}

} // namespace Neuron
