// JoinPage.cpp -- screen 03, the only screen in this game that types.

#include "pch.h"
#include "JoinPage.h"

#include <algorithm>
#include <format>

namespace Lockstep
{

using Neuron::Color;
using Neuron::FontRenderer;
using Neuron::ShapeRenderer;

namespace
{

constexpr float SCREEN_WIDTH = 1280.0F;
constexpr float SCREEN_HEIGHT = 720.0F;

/// The centre column, from DESIGN-GUIDELINES via SCREENS.md 03: 480 wide, centred.
constexpr float COLUMN_WIDTH = 480.0F;
constexpr float COLUMN_X = (SCREEN_WIDTH - COLUMN_WIDTH) * 0.5F;

constexpr float TITLE_Y = 166.0F;
constexpr float SUBTITLE_Y = 197.0F;

constexpr float CARD_Y = 228.0F;
constexpr float CARD_HEIGHT = 282.0F;
constexpr float CARD_PADDING = 19.0F;
constexpr float FIELD_X = COLUMN_X + CARD_PADDING;
constexpr float FIELD_WIDTH = COLUMN_WIDTH - 2.0F * CARD_PADDING;
constexpr float FIELD_HEIGHT = 30.0F;

constexpr float SERVER_LABEL_Y = 250.0F;
constexpr float TOKEN_LABEL_Y = 315.0F;
constexpr float EXPLANATION_Y = 381.0F;
constexpr float SEAT_Y = 422.0F;
constexpr float SEAT_HEIGHT = 27.0F;
constexpr float BUTTON_Y = 468.0F;
constexpr float BUTTON_HEIGHT = 23.0F;
constexpr float FOOTER_Y = 534.0F;

constexpr std::int32_t LINE_HEIGHT = 12;

constexpr Color APP_BACKGROUND = {11, 14, 20, 255};
constexpr Color CARD_FILL = {255, 255, 255, 10};
constexpr Color CARD_BORDER = {255, 255, 255, 26};
constexpr Color OUTLINE = {255, 255, 255, 51};
constexpr Color TEXT_PRIMARY = {240, 243, 247, 255};
constexpr Color TEXT_MUTED = {214, 220, 228, 140};
constexpr Color TEXT_DETAIL = {214, 220, 228, 153};
constexpr Color BLUE = {94, 196, 255, 255};
constexpr Color RED = {255, 110, 96, 255};
constexpr Color STAR = {214, 220, 228, 220};

/// What a tap can hit. Small enough to be an integer rather than an enum shared with `MainPage`:
/// this screen's vocabulary is four things and none of them is an order.
constexpr std::int32_t HIT_SERVER = 1;
constexpr std::int32_t HIT_TOKEN = 2;
constexpr std::int32_t HIT_SHOW = 3;
constexpr std::int32_t HIT_JOIN = 4;

/// The caret blinks at this period, in seconds. Slow enough not to nag, fast enough to say which
/// field is listening.
constexpr double BLINK_SECONDS = 1.0;

[[nodiscard]] std::int32_t CenterTextY(float _y, float _height, std::uint32_t _scale = 1)
{
  const auto glyph = static_cast<float>(FontRenderer::GLYPH_HEIGHT_TEXELS * _scale);
  return static_cast<std::int32_t>(_y + (_height - glyph) * 0.5F);
}

void DashedRect(ShapeRenderer& _shapes, float _x, float _y, float _width, float _height, const Color& _color)
{
  constexpr float DASH = 4.0F;
  constexpr float GAP = 4.0F;
  _shapes.DashedLine(_x, _y, _x + _width, _y, _color, 1.0F, DASH, GAP);
  _shapes.DashedLine(_x, _y + _height, _x + _width, _y + _height, _color, 1.0F, DASH, GAP);
  _shapes.DashedLine(_x, _y, _x, _y + _height, _color, 1.0F, DASH, GAP);
  _shapes.DashedLine(_x + _width, _y, _x + _width, _y + _height, _color, 1.0F, DASH, GAP);
}

} // namespace

JoinPage::JoinPage()
{
  // A fixed view of the sky. The map's camera orbits because a player drives it; this one has
  // nothing to look at but stars, and a background that moved on its own would be the only thing
  // on the screen competing with the field you are meant to be typing into.
  m_camera.SetViewport(0.0F, 0.0F, SCREEN_WIDTH, SCREEN_HEIGHT);
  m_camera.SetTarget({0.0F, 0.0F, 0.0F});
  m_camera.SetDistance(700.0F);
  m_camera.SetOrientation(0.6F, 0.35F);
}

void JoinPage::Offer(std::string_view _server, std::string_view _token)
{
  m_server.Set(_server);
  m_token.Set(_token);

  // The caret starts in the token, because the server is usually already right and the token never
  // is: it is the thing the player was handed and has come here to type.
  m_focus = Focus::Token;
}

void JoinPage::SetStatus(Status _status, std::string_view _detail)
{
  m_status = _status;
  m_detail = _detail;
}

void JoinPage::AskToJoin() noexcept
{
  m_joinRequested = true;
}

void JoinPage::FocusToken() noexcept
{
  m_focus = Focus::Token;
}

void JoinPage::SetSeat(std::int32_t _seat, std::int32_t _players, const Color& _color)
{
  m_seat = _seat;
  m_players = _players;
  m_seatColor = _color;
}

void JoinPage::SetMatchSummary(std::string_view _summary)
{
  m_matchSummary = _summary;
}

void JoinPage::Update(double _elapsedSeconds)
{
  m_blinkSeconds += _elapsedSeconds;
}

bool JoinPage::TakeJoinRequest() noexcept
{
  const bool asked = m_joinRequested;
  m_joinRequested = false;
  return asked;
}

void JoinPage::AddHit(float _x, float _y, float _width, float _height, std::int32_t _action)
{
  m_hits.push_back(Hit{_x, _y, _width, _height, _action});
}

void JoinPage::HandleTyped(std::string_view _typed)
{
  if (m_status == Status::Connecting)
  {
    return;
  }

  Neuron::TextField& field = m_focus == Focus::Server ? m_server : m_token;
  for (const char letter : _typed)
  {
    field.Type(letter);
  }
}

void JoinPage::HandleKey(Neuron::KeyboardInput::Key _key)
{
  using Key = Neuron::KeyboardInput::Key;

  // Tab and Enter are about the screen; everything else is about the field with the caret.
  if (_key == Key::Tab)
  {
    m_focus = m_focus == Focus::Server ? Focus::Token : Focus::Server;
    return;
  }
  if (_key == Key::Enter)
  {
    // Enter is JOIN. A form with one button and a keyboard in front of it should not need the
    // mouse to finish.
    m_joinRequested = m_status != Status::Connecting && !m_token.Empty() && !m_server.Empty();
    return;
  }

  if (m_status == Status::Connecting)
  {
    return;
  }

  Neuron::TextField& field = m_focus == Focus::Server ? m_server : m_token;
  switch (_key)
  {
  case Key::Backspace:
    field.Backspace();
    return;
  case Key::Delete:
    field.Delete();
    return;
  case Key::Left:
    field.CaretLeft();
    return;
  case Key::Right:
    field.CaretRight();
    return;
  case Key::Home:
    field.CaretHome();
    return;
  case Key::End:
    field.CaretEnd();
    return;
  case Key::Escape:
  case Key::Tab:
  case Key::Enter:
  default:
    return;
  }
}

bool JoinPage::HandleTap(float _xPixels, float _yPixels)
{
  for (auto hit = m_hits.rbegin(); hit != m_hits.rend(); ++hit)
  {
    const bool inside = _xPixels >= hit->x && _xPixels < hit->x + hit->width && _yPixels >= hit->y && _yPixels < hit->y + hit->height;
    if (!inside)
    {
      continue;
    }

    switch (hit->action)
    {
    case HIT_SERVER:
      m_focus = Focus::Server;
      m_server.CaretEnd();
      return true;
    case HIT_TOKEN:
      m_focus = Focus::Token;
      m_token.CaretEnd();
      return true;
    case HIT_SHOW:
      m_revealToken = !m_revealToken;
      return true;
    case HIT_JOIN:
      m_joinRequested = m_status != Status::Connecting && !m_token.Empty() && !m_server.Empty();
      return true;
    default:
      return true;
    }
  }
  return false;
}

void JoinPage::DrawWorld(ShapeRenderer& _shapes, FontRenderer& _text)
{
  (void)_text;
  m_hits.clear();

  _shapes.FillRect(0.0F, 0.0F, SCREEN_WIDTH, SCREEN_HEIGHT, APP_BACKGROUND);
  m_sky.Draw(_shapes, m_camera, STAR);
}

void JoinPage::DrawField(ShapeRenderer& _shapes, FontRenderer& _text, float _y, std::string_view _label, std::string_view _right,
                         const Neuron::TextField& _field, bool _focused, bool _masked, std::int32_t _action)
{
  _text.DrawText(static_cast<std::int32_t>(FIELD_X), static_cast<std::int32_t>(_y), _label, TEXT_MUTED);

  if (!_right.empty())
  {
    const auto width = static_cast<float>(FontRenderer::MeasurePixels(_right));
    const float rightX = FIELD_X + FIELD_WIDTH - width;
    _text.DrawText(static_cast<std::int32_t>(rightX), static_cast<std::int32_t>(_y), _right, _focused && _masked ? BLUE : TEXT_MUTED);

    // SHOW is the only label on this screen that is also a control, so it gets a hit of its own
    // rather than being part of the field.
    if (_action == HIT_TOKEN)
    {
      AddHit(rightX - 4.0F, _y - 4.0F, width + 8.0F, 16.0F, HIT_SHOW);
    }
  }

  const float boxY = _y + 14.0F;
  _shapes.FillRect(FIELD_X, boxY, FIELD_WIDTH, FIELD_HEIGHT, APP_BACKGROUND);
  _shapes.StrokeRect(FIELD_X, boxY, FIELD_WIDTH, FIELD_HEIGHT, _focused ? BLUE : CARD_BORDER);
  AddHit(FIELD_X, boxY, FIELD_WIDTH, FIELD_HEIGHT, _action);

  const std::string shown = _field.Shown(_masked);
  const std::int32_t textY = CenterTextY(boxY, FIELD_HEIGHT);
  _text.DrawText(static_cast<std::int32_t>(FIELD_X) + 10, textY, shown, TEXT_PRIMARY);

  // The caret, on the focused field only, blinking. It is drawn as a glyph rather than a rectangle
  // so that it sits on the text's baseline without a second set of metrics to keep in step.
  const bool lit = std::fmod(m_blinkSeconds, BLINK_SECONDS) < BLINK_SECONDS * 0.6;
  if (_focused && lit)
  {
    const auto caretX = static_cast<std::int32_t>(FIELD_X) + 10 +
                        static_cast<std::int32_t>(FontRenderer::AdvancePixels(1)) * static_cast<std::int32_t>(_field.CaretColumn());
    _text.DrawText(caretX, textY, "_", BLUE);
  }
}

void JoinPage::DrawInterface(ShapeRenderer& _shapes, FontRenderer& _text)
{
  // ---- The name, at 2x -----------------------------------------------------------------------
  //
  // The only other place this font is drawn at 2x is the lock countdown (DESIGN-GUIDELINES
  // "Font"). Both are the one thing on their screen that has to be read first.
  _text.DrawText(static_cast<std::int32_t>(COLUMN_X), static_cast<std::int32_t>(TITLE_Y), "LOCKSTEP", TEXT_PRIMARY,
                 FontRenderer::COUNTDOWN_SCALE);
  _text.DrawText(static_cast<std::int32_t>(COLUMN_X), static_cast<std::int32_t>(SUBTITLE_Y), "JOIN A MATCH - ONE SEAT PER TOKEN",
                 TEXT_MUTED);

  // ---- The card ------------------------------------------------------------------------------
  //
  // INK FIRST, then the card's own 4% white. Everywhere else in this game a card sits on the app
  // background and the fill is the whole of it; here it sits on the sky, and 4% white over stars is
  // stars. The guidelines say a dialog has an ink background for the same reason -- what is behind
  // it must stop being readable.
  _shapes.FillRect(COLUMN_X, CARD_Y, COLUMN_WIDTH, CARD_HEIGHT, APP_BACKGROUND);
  _shapes.FillRect(COLUMN_X, CARD_Y, COLUMN_WIDTH, CARD_HEIGHT, CARD_FILL);
  _shapes.StrokeRect(COLUMN_X, CARD_Y, COLUMN_WIDTH, CARD_HEIGHT, CARD_BORDER);

  DrawField(_shapes, _text, SERVER_LABEL_Y, "SERVER", "LAST USED", m_server, m_focus == Focus::Server, false, HIT_SERVER);
  DrawField(_shapes, _text, TOKEN_LABEL_Y, "TOKEN", "SHOW", m_token, m_focus == Focus::Token, !m_revealToken, HIT_TOKEN);

  // ---- What a token is -----------------------------------------------------------------------
  //
  // ADR-029's whole decision in two lines, because a player who thinks this is a password will
  // treat losing it as an account problem rather than as losing a seat.
  const std::size_t columns = FontRenderer::FitCharacters(static_cast<std::uint32_t>(FIELD_WIDTH));
  std::int32_t lineY = static_cast<std::int32_t>(EXPLANATION_Y);
  for (const std::string& line :
       FontRenderer::Wrap("The match gives you a token. It names your seat, not you: whoever types it plays that empire.", columns))
  {
    _text.DrawText(static_cast<std::int32_t>(FIELD_X), lineY, line, TEXT_DETAIL);
    lineY += LINE_HEIGHT;
  }

  // ---- The seat --------------------------------------------------------------------------------
  //
  // Dashed until the server has welcomed somebody, because a seat nobody has confirmed is a guess,
  // and this screen's whole job is to stop two people playing the same empire.
  DashedRect(_shapes, FIELD_X, SEAT_Y, FIELD_WIDTH, SEAT_HEIGHT, CARD_BORDER);
  const std::int32_t seatTextY = CenterTextY(SEAT_Y, SEAT_HEIGHT);
  _text.DrawText(static_cast<std::int32_t>(FIELD_X) + 12, seatTextY, "SEAT", TEXT_MUTED);

  if (m_seat >= 0)
  {
    // `SEAT 3` until the player count is known, and `3 OF 12` after. The Welcome message carries a
    // seat and not a census (Protocol.h), so the total only arrives with the first snapshot -- and
    // a made-up denominator on the one screen whose job is to tell you which seat you got would be
    // the worst place in the game to guess.
    const std::string seatLine = m_players > 0 ? std::format("{} OF {}", m_seat + 1, m_players) : std::format("SEAT {}", m_seat + 1);
    const auto seatWidth = static_cast<float>(FontRenderer::MeasurePixels(seatLine));
    _text.DrawText(static_cast<std::int32_t>(FIELD_X + FIELD_WIDTH - seatWidth) - 12, seatTextY, seatLine, TEXT_PRIMARY);

    // The colour swatch says which empire, because ADR-027 makes colour the thing a player reads
    // the map by and "you are always blue" is only true for you.
    _shapes.FillRect(FIELD_X + FIELD_WIDTH - seatWidth - 26.0F, static_cast<float>(seatTextY), 8.0F, 8.0F, m_seatColor);
  }
  else
  {
    const char* waiting = "NOT YET CONFIRMED";
    const auto width = static_cast<float>(FontRenderer::MeasurePixels(waiting));
    _text.DrawText(static_cast<std::int32_t>(FIELD_X + FIELD_WIDTH - width) - 12, seatTextY, waiting, TEXT_MUTED);
  }

  // ---- JOIN ------------------------------------------------------------------------------------
  const bool ready = m_status != Status::Connecting && !m_token.Empty() && !m_server.Empty();
  const char* label = m_status == Status::Connecting ? "CONNECTING" : "JOIN >";
  const auto buttonWidth = static_cast<float>(FontRenderer::MeasurePixels(label)) + 20.0F;
  const float buttonX = FIELD_X + FIELD_WIDTH - buttonWidth;

  if (ready)
  {
    _shapes.FillRect(buttonX, BUTTON_Y, buttonWidth, BUTTON_HEIGHT, BLUE);
    _text.DrawText(static_cast<std::int32_t>(buttonX) + 10, CenterTextY(BUTTON_Y, BUTTON_HEIGHT), label, APP_BACKGROUND);
    AddHit(buttonX, BUTTON_Y, buttonWidth, BUTTON_HEIGHT, HIT_JOIN);
  }
  else
  {
    _shapes.StrokeRect(buttonX, BUTTON_Y, buttonWidth, BUTTON_HEIGHT, OUTLINE);
    _text.DrawText(static_cast<std::int32_t>(buttonX) + 10, CenterTextY(BUTTON_Y, BUTTON_HEIGHT), label, TEXT_MUTED);
  }

  // A refusal is said next to the button that caused it, in the colour refusals are said in.
  //
  // **This line is for failures on THIS machine only** -- an address that resolves to nothing, a
  // port with nothing behind it. Anything the server actually answered with goes to
  // `ConnectionDialog` instead (screen 05), because a refusal that came back over the wire has
  // something to explain and two things the player can do about it, and neither fits on one line.
  if (m_status == Status::Refused && !m_detail.empty())
  {
    _text.DrawText(static_cast<std::int32_t>(FIELD_X), CenterTextY(BUTTON_Y, BUTTON_HEIGHT), m_detail, RED);
  }

  // ---- The footer ------------------------------------------------------------------------------
  if (!m_matchSummary.empty())
  {
    _text.DrawText(static_cast<std::int32_t>(COLUMN_X), static_cast<std::int32_t>(FOOTER_Y), m_matchSummary, TEXT_MUTED);
  }

  // The command line is still there and still works. Saying so costs one line and saves somebody
  // discovering it in a source file.
  _text.DrawText(static_cast<std::int32_t>(COLUMN_X) + 286, static_cast<std::int32_t>(FOOTER_Y), "ALSO: --join SERVER TOKEN", TEXT_MUTED);
}

} // namespace Lockstep
