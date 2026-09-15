// Controls.cpp -- the state table and the button, as drawn (ADR-110).

#include "pch.h"
#include "Controls.h"

#include "DesignTokens.h"
#include "MainPage.h"

namespace Lockstep
{

namespace
{

using Neuron::Color;
using Neuron::Face;
using Neuron::FontRenderer;
using Neuron::ShapeRenderer;

} // namespace

[[nodiscard]] ControlInk ControlInkFor(ControlState _state, ControlKind _kind, bool _hovered, bool _moneyReason) noexcept
{
  constexpr Color NO_INK = {0, 0, 0, 0};
  switch (_state)
  {
  case ControlState::Primary:
    // **The ring goes in the border slot, because a filled control has no room for a hover FILL**:
    // its rest state is already the brightest thing on the screen, so the press is said by lifting
    // the fill toward white and ringing it in the colour it came from.
    return ControlInk{.border = _hovered ? Ink::BLUE : NO_INK,
                      .dashed = false,
                      .fill = _hovered ? Ink::BUTTON_PRIMARY_HOVER : Ink::BLUE,
                      .label = Ink::APP_BACKGROUND,
                      .number = Ink::APP_BACKGROUND,
                      .segmentRule = NO_INK,
                      .segmentFill = Ink::BUTTON_SEGMENT_SHADE};

  case ControlState::Committed:
    return ControlInk{.border = Ink::BLUE,
                      .dashed = false,
                      .fill = _hovered ? Ink::COMMITTED_HOVER_FILL : Ink::TILE_COMMITTED_FILL,
                      .label = Ink::BLUE,
                      .number = Ink::BLUE,
                      .segmentRule = WithAlpha(Ink::BLUE, 90),
                      .segmentFill = NO_INK};

  case ControlState::Inert:
    return ControlInk{.border = Ink::INERT_BORDER,
                      .dashed = true,
                      .fill = NO_INK,
                      .label = Ink::NEUTRAL_DIM,
                      .number = _moneyReason ? Ink::AMBER : Ink::NEUTRAL_DIM,
                      .segmentRule = Ink::INERT_BORDER,
                      .segmentFill = NO_INK};

  case ControlState::Locked:
    // **A BUTTON at the lock is the filled grey the rail's chip wears; a tile, a sheet row and a
    // rail row dim in place** (ADR-065, amended by ADR-110). The grey says "this is a receipt now"
    // on a control the size of a chip, and it is the same statement the `LOCKED` chip above it
    // makes. At the size of a 284x96 tile or a 260-pixel row it is not that statement at all: a
    // locked sheet would become four light-grey boxes, which is the screen inverted rather than
    // gone quiet, and ADR-065 settled that a sheet at the lock stays where it is and fades.
    if (_kind == ControlKind::Button)
    {
      return ControlInk{.border = NO_INK,
                        .dashed = false,
                        .fill = Ink::LOCKED_FILL,
                        .label = Ink::APP_BACKGROUND,
                        .number = Ink::APP_BACKGROUND,
                        .segmentRule = NO_INK,
                        .segmentFill = Ink::BUTTON_SEGMENT_SHADE};
    }
    return ControlInk{.border = Ink::DIVIDER,
                      .dashed = false,
                      .fill = NO_INK,
                      .label = Ink::NEUTRAL_DIM,
                      .number = Ink::NEUTRAL_DIM,
                      .segmentRule = Ink::DIVIDER,
                      .segmentFill = NO_INK};

  case ControlState::Outlined:
  default:
    return ControlInk{.border = _hovered ? Ink::OUTLINE_HOVER : Ink::OUTLINE,
                      .dashed = false,
                      .fill = _hovered ? Ink::HOVER_FILL : NO_INK,
                      .label = Ink::TEXT_PRIMARY,
                      .number = Ink::TEXT_MUTED,
                      .segmentRule = Ink::DIVIDER,
                      .segmentFill = NO_INK};
  }
}

void DrawControlBox(ShapeRenderer& _shapes, float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, const ControlInk& _ink)
{
  if (_ink.fill.alpha != 0)
  {
    _shapes.FillRect(_xPixels, _yPixels, _widthPixels, _heightPixels, _ink.fill);
  }
  if (_ink.border.alpha == 0)
  {
    return;
  }
  if (!_ink.dashed)
  {
    _shapes.StrokeRect(_xPixels, _yPixels, _widthPixels, _heightPixels, _ink.border);
    return;
  }

  const float right = _xPixels + _widthPixels;
  const float bottom = _yPixels + _heightPixels;
  const float dash = MainPage::INERT_DASH;
  const float gap = MainPage::INERT_GAP;
  _shapes.DashedLine(_xPixels, _yPixels + 0.5F, right, _yPixels + 0.5F, _ink.border, 1.0F, dash, gap);
  _shapes.DashedLine(_xPixels, bottom - 0.5F, right, bottom - 0.5F, _ink.border, 1.0F, dash, gap);
  _shapes.DashedLine(_xPixels + 0.5F, _yPixels, _xPixels + 0.5F, bottom, _ink.border, 1.0F, dash, gap);
  _shapes.DashedLine(right - 0.5F, _yPixels, right - 0.5F, bottom, _ink.border, 1.0F, dash, gap);
}

[[nodiscard]] float ButtonWidth(const Button& _button)
{
  float width = 2.0F * MainPage::BUTTON_LABEL_PADDING + static_cast<float>(FontRenderer::MeasurePixels(_button.label, Face::MonoMedium));
  if (_button.state == ControlState::Locked)
  {
    width += MainPage::LOCK_GLYPH_SIZE + 6.0F;
  }
  if (!_button.number.empty())
  {
    width +=
      1.0F + 2.0F * MainPage::BUTTON_NUMBER_PADDING + static_cast<float>(FontRenderer::MeasurePixels(_button.number, Face::MonoMedium));
  }
  return width;
}

void DrawButton(ShapeRenderer& _shapes, FontRenderer& _text, float _xPixels, float _yPixels, float _widthPixels, const Button& _button,
                const ControlInk& _ink)
{
  DrawControlBox(_shapes, _xPixels, _yPixels, _widthPixels, MainPage::BUTTON_HEIGHT, _ink);

  const std::int32_t labelY = CenterTextY(_yPixels, MainPage::BUTTON_HEIGHT, Face::MonoMedium);
  float labelX = _xPixels + MainPage::BUTTON_LABEL_PADDING;
  if (_button.state == ControlState::Locked)
  {
    // A square rather than a padlock: at six pixels a padlock is four grey dots, and the ladder on
    // a build tile already teaches this screen's reader that a small square is a state.
    _shapes.FillRect(labelX, _yPixels + (MainPage::BUTTON_HEIGHT - MainPage::LOCK_GLYPH_SIZE) * 0.5F, MainPage::LOCK_GLYPH_SIZE,
                     MainPage::LOCK_GLYPH_SIZE, _ink.label);
    labelX += MainPage::LOCK_GLYPH_SIZE + 6.0F;
  }
  _text.DrawText(static_cast<std::int32_t>(labelX), labelY, _button.label, _ink.label, Face::MonoMedium);

  if (_button.number.empty())
  {
    return;
  }

  const float segmentWidth =
    2.0F * MainPage::BUTTON_NUMBER_PADDING + static_cast<float>(FontRenderer::MeasurePixels(_button.number, Face::MonoMedium));
  const float segmentX = _xPixels + _widthPixels - segmentWidth;

  if (_ink.segmentFill.alpha != 0)
  {
    _shapes.FillRect(segmentX, _yPixels, segmentWidth, MainPage::BUTTON_HEIGHT, _ink.segmentFill);
  }
  else if (_ink.dashed)
  {
    _shapes.DashedLine(segmentX - 0.5F, _yPixels, segmentX - 0.5F, _yPixels + MainPage::BUTTON_HEIGHT, _ink.segmentRule, 1.0F,
                       MainPage::INERT_DASH, MainPage::INERT_GAP);
  }
  else if (_ink.segmentRule.alpha != 0)
  {
    _shapes.FillRect(segmentX - 1.0F, _yPixels, 1.0F, MainPage::BUTTON_HEIGHT, _ink.segmentRule);
  }

  _text.DrawText(static_cast<std::int32_t>(segmentX + MainPage::BUTTON_NUMBER_PADDING), labelY, _button.number, _ink.number,
                 Face::MonoMedium);
}

} // namespace Lockstep
