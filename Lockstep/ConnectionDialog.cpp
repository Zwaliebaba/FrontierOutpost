// ConnectionDialog.cpp -- screens 04 and 05.

#include "pch.h"
#include "ConnectionDialog.h"

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

/// 420 in the reference sheet, where five of them sit side by side. One at a time over a 1280-wide
/// screen can afford the room, and the CONNECTION LOST paragraph needs it.
constexpr float CARD_WIDTH = 520.0F;
constexpr float CARD_X = (SCREEN_WIDTH - CARD_WIDTH) * 0.5F;
constexpr float CARD_PADDING = 18.0F;

constexpr float BUTTON_HEIGHT = 24.0F;
constexpr float BUTTON_GAP = 8.0F;
constexpr float BUTTON_PADDING = 12.0F;

constexpr std::int32_t LINE_HEIGHT = 12;
constexpr float TITLE_GAP = 16.0F;
constexpr float BODY_GAP = 14.0F;

constexpr Color APP_BACKGROUND = {11, 14, 20, 255};
constexpr Color CARD_FILL = {17, 21, 29, 255};
constexpr Color CARD_BORDER = {255, 255, 255, 26};
constexpr Color OUTLINE = {255, 255, 255, 51};
constexpr Color TEXT_PRIMARY = {240, 243, 247, 255};
constexpr Color TEXT_DETAIL = {214, 220, 228, 153};
constexpr Color BLUE = {94, 196, 255, 255};
constexpr Color AMBER = {255, 196, 87, 255};
constexpr Color RED = {255, 110, 96, 255};

/// The scrim. Opaque enough that the screen behind reads as unavailable rather than as merely
/// dark, and transparent enough that a player can still see the map they are waiting to get back.
constexpr Color SCRIM = {7, 9, 13, 205};

[[nodiscard]] std::int32_t CenterTextY(float _y, float _height)
{
  const auto glyph = static_cast<float>(FontRenderer::GLYPH_HEIGHT_TEXELS);
  return static_cast<std::int32_t>(_y + (_height - glyph) * 0.5F);
}

/// Seconds, as a person would say them. `next in 4s` rather than `next in 4.000000s`.
[[nodiscard]] std::string Seconds(double _seconds)
{
  const auto whole = static_cast<std::int64_t>(_seconds < 0.0 ? 0.0 : _seconds + 0.5);
  return std::format("{}s", whole);
}

} // namespace

void ConnectionDialog::Update(Kind _kind, const Facts& _facts, double _elapsedSeconds)
{
  // A change of kind restarts the clock. "Connecting for 40 seconds" carried over from a dialog
  // that was up before would be a number about the wrong thing.
  if (_kind != m_kind)
  {
    m_shownSeconds = 0.0;
    m_kind = _kind;
  }
  else
  {
    m_shownSeconds += _elapsedSeconds;
  }

  m_facts = _facts;
}

ConnectionDialog::Action ConnectionDialog::TakeAction() noexcept
{
  const Action taken = m_action;
  m_action = Action::None;
  return taken;
}

void ConnectionDialog::Compose(std::string& _outTitle, Look& _outLook, std::vector<std::string>& _outBody,
                               std::vector<Button>& _outButtons) const
{
  _outTitle.clear();
  _outLook = Look{CARD_BORDER, TEXT_PRIMARY};
  _outBody.clear();
  _outButtons.clear();

  const std::string back = m_facts.canGoBack ? "BACK" : "QUIT";
  const Action backAction = m_facts.canGoBack ? Action::Back : Action::Quit;

  switch (m_kind)
  {
  case Kind::Connecting:
    _outTitle = "CONNECTING";
    _outBody.push_back(m_facts.server);
    _outBody.push_back("Sending token - waiting for Welcome.");
    _outBody.push_back(std::format("Waiting {}.", Seconds(m_shownSeconds)));
    _outButtons.push_back(Button{"CANCEL", Action::Cancel, false});
    return;

  case Kind::Waiting:
    // **The screen that did not exist, and the one that did the most damage by not existing.** A
    // client welcomed by a lobby gets no `State` at all -- the server has no match to make one from
    // -- so the composition root drew the reference fixture instead: invented empires, an invented
    // galaxy, an invented countdown, indistinguishable from a real match to the person waiting.
    _outTitle = "WAITING FOR THE HOST";
    _outLook = Look{BLUE, BLUE};
    _outBody.push_back(m_facts.seat >= 0 ? std::format("You are in. Seat {:02} is yours.", m_facts.seat + 1) : std::string{"You are in."});
    _outBody.push_back("The host has not started the match yet. The galaxy is generated when they do, for however "
                       "many seats they arranged, so there is nothing to show until then.");
    _outBody.push_back("This becomes the match the moment the first tick arrives.");
    _outButtons.push_back(Button{"QUIT", Action::Quit, false});
    return;

  case Kind::Refused:
    _outLook = Look{RED, RED};
    switch (m_facts.reason)
    {
    case Neuron::RefusalReason::AlreadyConnected:
      _outTitle = "REFUSED - SEAT IN USE";
      _outBody.push_back("Someone is already connected on this token. If that was you a moment ago, wait a few "
                         "seconds and retry - the seat frees when the old link drops.");
      _outButtons.push_back(Button{back, backAction, false});
      _outButtons.push_back(Button{"RETRY", Action::Retry, true});
      return;

    case Neuron::RefusalReason::Malformed:
      _outTitle = "REFUSED - NOT UNDERSTOOD";
      _outBody.push_back("The server could not read what this client sent. That is a defect rather than a typo: "
                         "the two ends disagree about the protocol, which usually means different builds.");
      _outButtons.push_back(Button{back, backAction, false});
      return;

    case Neuron::RefusalReason::UnknownToken:
    case Neuron::RefusalReason::MatchFinished:
    case Neuron::RefusalReason::None:
    default:
      _outTitle = "REFUSED - UNKNOWN TOKEN";
      _outBody.push_back("This token is not on this match's list. Check it with the host - tokens are per match, "
                         "and a host who restarted has issued a new set.");
      _outButtons.push_back(Button{back, backAction, false});
      if (m_facts.canGoBack)
      {
        _outButtons.push_back(Button{"EDIT TOKEN", Action::EditToken, true});
      }
      return;
    }

  case Kind::Lost:
    _outTitle = "CONNECTION LOST";
    _outLook = Look{AMBER, AMBER};
    _outBody.push_back("The server stopped answering.");
    _outBody.push_back(m_facts.reconnects == 0 ? std::format("Reconnecting - next attempt in {}.", Seconds(m_facts.secondsToNextAttempt))
                                               : std::format("Reconnecting - back {} time(s) already - next attempt in {}.",
                                                             m_facts.reconnects, Seconds(m_facts.secondsToNextAttempt)));

    // **The reference sheet promises more than this client does, so this says less.** Screen 04's
    // paragraph is "your unlocked orders are kept here and re-sent when the link returns", and
    // nothing re-sends them: orders tapped while this dialog is up reach `SendOrders`, which drops
    // them because the status is not `Playing`. What IS true is that everything sent before the
    // drop is already on the server, where the latest submission for a tick wins.
    _outBody.push_back("Orders you already sent are on the server and still count. Anything you tap while this is "
                       "up is not sent.");
    if (!m_facts.lockCountdown.empty())
    {
      _outBody.push_back(std::format("The tick still locks in {} whether or not you are back.", m_facts.lockCountdown));
    }
    _outButtons.push_back(Button{"QUIT", Action::Quit, false});
    _outButtons.push_back(Button{"RETRY NOW", Action::Retry, true});
    return;

  case Kind::Finished:
    _outTitle = "MATCH FINISHED";
    _outBody.push_back("This match has ended. The final standings are in the last digest.");
    if (!m_facts.standings.empty())
    {
      _outBody.push_back(m_facts.standings);
    }
    _outButtons.push_back(Button{"QUIT", Action::Quit, false});
    _outButtons.push_back(Button{"VIEW LAST DIGEST", Action::ViewLastDigest, true});
    return;

  case Kind::None:
  default:
    return;
  }
}

void ConnectionDialog::Draw(ShapeRenderer& _shapes, FontRenderer& _text)
{
  m_hits.clear();
  if (m_kind == Kind::None)
  {
    return;
  }

  std::string title;
  Look look{CARD_BORDER, TEXT_PRIMARY};
  std::vector<std::string> body;
  std::vector<Button> buttons;
  Compose(title, look, body, buttons);

  // ---- How tall this one is ----------------------------------------------------------------------
  //
  // Measured before anything is drawn, because the card is centred vertically and a card that grew
  // downward from a fixed top would put a five-line paragraph off the bottom of the screen.
  const auto columns = FontRenderer::FitCharacters(static_cast<std::uint32_t>(CARD_WIDTH - 2.0F * CARD_PADDING));

  std::vector<std::string> lines;
  for (const std::string& paragraph : body)
  {
    if (!lines.empty())
    {
      lines.emplace_back();
    }
    for (std::string& line : FontRenderer::Wrap(paragraph, columns))
    {
      lines.push_back(std::move(line));
    }
  }

  const float bodyHeight = static_cast<float>(lines.size() * LINE_HEIGHT);
  const float cardHeight =
    CARD_PADDING + static_cast<float>(FontRenderer::GLYPH_HEIGHT_TEXELS) + TITLE_GAP + bodyHeight + BODY_GAP + BUTTON_HEIGHT + CARD_PADDING;
  const float cardY = std::max(40.0F, (SCREEN_HEIGHT - cardHeight) * 0.5F);

  // ---- The scrim ---------------------------------------------------------------------------------
  _shapes.FillRect(0.0F, 0.0F, SCREEN_WIDTH, SCREEN_HEIGHT, SCRIM);

  _shapes.FillRect(CARD_X, cardY, CARD_WIDTH, cardHeight, CARD_FILL);
  _shapes.StrokeRect(CARD_X, cardY, CARD_WIDTH, cardHeight, look.border);

  const auto contentX = static_cast<std::int32_t>(CARD_X + CARD_PADDING);
  auto y = static_cast<std::int32_t>(cardY + CARD_PADDING);

  _text.DrawText(contentX, y, title, look.title);
  y += static_cast<std::int32_t>(FontRenderer::GLYPH_HEIGHT_TEXELS + TITLE_GAP);

  for (const std::string& line : lines)
  {
    _text.DrawText(contentX, y, line, TEXT_DETAIL);
    y += LINE_HEIGHT;
  }

  // ---- The buttons, right to left ----------------------------------------------------------------
  //
  // The primary is rightmost, which is where a thumb is. Laid out from the right edge so that a
  // longer label grows into the card rather than off it.
  const float buttonY = cardY + cardHeight - CARD_PADDING - BUTTON_HEIGHT;
  float right = CARD_X + CARD_WIDTH - CARD_PADDING;

  for (auto button = buttons.rbegin(); button != buttons.rend(); ++button)
  {
    const float width = static_cast<float>(FontRenderer::MeasurePixels(button->label)) + 2.0F * BUTTON_PADDING;
    const float x = right - width;

    if (button->filled)
    {
      _shapes.FillRect(x, buttonY, width, BUTTON_HEIGHT, BLUE);
    }
    else
    {
      _shapes.StrokeRect(x, buttonY, width, BUTTON_HEIGHT, OUTLINE);
    }

    _text.DrawText(static_cast<std::int32_t>(x + BUTTON_PADDING), CenterTextY(buttonY, BUTTON_HEIGHT), button->label,
                   button->filled ? APP_BACKGROUND : TEXT_PRIMARY);
    m_hits.push_back(Hit{x, buttonY, width, BUTTON_HEIGHT, button->action});

    right = x - BUTTON_GAP;
  }
}

bool ConnectionDialog::HandleTap(float _xPixels, float _yPixels)
{
  if (m_kind == Kind::None)
  {
    return false;
  }

  for (const Hit& hit : m_hits)
  {
    if (_xPixels >= hit.x && _xPixels < hit.x + hit.width && _yPixels >= hit.y && _yPixels < hit.y + hit.height)
    {
      m_action = hit.action;
      return true;
    }
  }

  // Everything else is swallowed. A tap that fell through to the map behind would edit orders this
  // client cannot send, and the player would have no way to know which of their taps counted.
  return true;
}

} // namespace Lockstep
