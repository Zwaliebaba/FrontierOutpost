// ConnectionDialog.cpp -- screens 04 and 05.

#include "pch.h"
#include "ConnectionDialog.h"

#include "DesignTokens.h"

#include <algorithm>
#include <format>

namespace Lockstep
{

using Neuron::Color;
using Neuron::Face;
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

/// Baseline to baseline, from the font. See `MainPage::LINE_HEIGHT`.
constexpr std::int32_t LINE_HEIGHT = static_cast<std::int32_t>(Neuron::FontRenderer::LineHeightPixels());
constexpr float TITLE_GAP = 16.0F;
constexpr float BODY_GAP = 14.0F;

/// **One palette, bound to local names** (ADR-083). These were thirteen literal colours copied from
/// the same list, which is thirteen chances for one of them to be adjusted alone -- and the day the
/// contrast floor moved, three files would have kept the old number. The names stay local because
/// they are used a hundred times each in this file and `Ink::` at every site is noise; what moved is
/// where the VALUE comes from, which is the half that could ever be wrong.
constexpr Color APP_BACKGROUND = Ink::APP_BACKGROUND;
constexpr Color CARD_FILL = Ink::DIALOG_FILL;
constexpr Color CARD_BORDER = Ink::CARD_BORDER;
constexpr Color OUTLINE = Ink::OUTLINE;
constexpr Color TEXT_PRIMARY = Ink::TEXT_PRIMARY;
constexpr Color TEXT_DETAIL = Ink::TEXT_DETAIL;
constexpr Color BLUE = Ink::BLUE;
constexpr Color AMBER = Ink::AMBER;
constexpr Color RED = Ink::RED;

/// The scrim. Opaque enough that the screen behind reads as unavailable rather than as merely
/// dark, and transparent enough that a player can still see the map they are waiting to get back.
constexpr Color SCRIM = {7, 9, 13, 205};

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

void ConnectionDialog::Compose(std::string& _outTitle, Look& _outLook, std::vector<Paragraph>& _outBody,
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
    _outBody.push_back(Paragraph{m_facts.server, Neuron::Face::MonoRegular});
    _outBody.push_back(Paragraph{m_facts.greeted ? "Sending token - waiting for Welcome." : "Reaching the server."});
    _outBody.push_back(Paragraph{std::format("Waiting {}.", Seconds(m_shownSeconds))});
    _outButtons.push_back(Button{"CANCEL", Action::Cancel, false});
    return;

  case Kind::Waiting:
    // **The screen that did not exist, and the one that did the most damage by not existing.** A
    // client welcomed by a lobby gets no `State` at all -- the server has no match to make one from
    // -- so the composition root drew the reference fixture instead: invented empires, an invented
    // galaxy, an invented countdown, indistinguishable from a real match to the person waiting.
    _outTitle = "WAITING FOR THE HOST";
    _outLook = Look{BLUE, BLUE};
    _outBody.push_back(
      Paragraph{m_facts.seat >= 0 ? std::format("You are in. Seat {:02} is yours.", m_facts.seat + 1) : std::string{"You are in."}});
    _outBody.push_back(Paragraph{"The host has not started the match yet. The galaxy is generated when they do, for however "
                                 "many seats they arranged, so there is nothing to show until then."});
    _outBody.push_back(Paragraph{"This becomes the match the moment the first tick arrives."});
    _outButtons.push_back(Button{"QUIT", Action::Quit, false});
    return;

  case Kind::Refused:
    _outLook = Look{RED, RED};
    switch (m_facts.reason)
    {
    case Neuron::RefusalReason::AlreadyConnected:
      _outTitle = "REFUSED - SEAT IN USE";
      _outBody.push_back(Paragraph{"Someone is already connected on this token. If that was you a moment ago, wait a few "
                                   "seconds and retry - the seat frees when the old link drops."});
      _outButtons.push_back(Button{back, backAction, false});
      _outButtons.push_back(Button{"RETRY", Action::Retry, true});
      return;

    case Neuron::RefusalReason::Malformed:
      _outTitle = "REFUSED - NOT UNDERSTOOD";
      _outBody.push_back(Paragraph{"The server could not read what this client sent. That is a defect rather than a typo: "
                                   "the two ends disagree about the protocol, which usually means different builds."});
      _outButtons.push_back(Button{back, backAction, false});
      return;

    case Neuron::RefusalReason::UnknownToken:
    case Neuron::RefusalReason::MatchFinished:
    case Neuron::RefusalReason::None:
    default:
      _outTitle = "REFUSED - UNKNOWN TOKEN";
      _outBody.push_back(Paragraph{"This token is not on this match's list. Check it with the host - tokens are per match, "
                                   "and a host who restarted has issued a new set."});
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
    _outBody.push_back(Paragraph{"The server stopped answering."});
    // `back 4 time(s) already` is a placeholder that shipped. A screen that tells a player their
    // link keeps dropping should not also look unfinished while it does it (ADR-085).
    _outBody.push_back(
      Paragraph{m_facts.reconnects == 0 ? std::format("Reconnecting - next attempt in {}.", Seconds(m_facts.secondsToNextAttempt))
                : m_facts.reconnects == 1
                  ? std::format("Reconnecting - back once already - next attempt in {}.", Seconds(m_facts.secondsToNextAttempt))
                  : std::format("Reconnecting - back {} times already - next attempt in {}.", m_facts.reconnects,
                                Seconds(m_facts.secondsToNextAttempt))});

    // **The reference sheet promises more than this client does, so this says less.** Screen 04's
    // paragraph is "your unlocked orders are kept here and re-sent when the link returns", and
    // nothing re-sends them: orders tapped while this dialog is up reach `SendOrders`, which drops
    // them because the status is not `Playing`. What IS true is that everything sent before the
    // drop is already on the server, where the latest submission for a tick wins.
    _outBody.push_back(Paragraph{"Orders you already sent are on the server and still count. Anything you tap while this is "
                                 "up is not sent."});
    // **Past zero it has already locked, and saying it "still locks in 00:00:00" is a countdown
    // that has stopped counting.** The tick the player was editing for is gone; what they need to
    // know is that it went without them (ADR-085).
    if (!m_facts.lockCountdown.empty())
    {
      _outBody.push_back(Paragraph{m_facts.lockCountdown == "00:00:00"
                                     ? std::format("T{} locked while you were away.", m_facts.lockedTick)
                                     : std::format("The tick still locks in {} whether or not you are back.", m_facts.lockCountdown)});
    }
    _outButtons.push_back(Button{"QUIT", Action::Quit, false});
    _outButtons.push_back(Button{"RETRY NOW", Action::Retry, true});
    return;

  case Kind::Finished:
    _outTitle = "MATCH FINISHED";
    _outBody.push_back(Paragraph{"This match has ended."});

    // **The table, not a sentence about it** (ADR-097). `6 OF 6 · SCORE 35 · LEADER P6 95` tells a
    // player where they came and who won and nothing about the four empires in between -- on the
    // one screen whose entire job is to say how it went. Every row, in placement order, with the
    // reader's own in their own blue.
    for (const Facts::Standing& standing : m_facts.standings)
    {
      _outBody.push_back(Paragraph{standing.text, Neuron::Face::MonoRegular, standing.isYou ? BLUE : Neuron::Color{0, 0, 0, 0}});
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
  std::vector<Paragraph> body;
  std::vector<Button> buttons;
  Compose(title, look, body, buttons);

  if (!Modal())
  {
    DrawBanner(_shapes, _text, title, look, buttons);
    return;
  }

  // ---- How tall this one is ----------------------------------------------------------------------
  //
  // Measured before anything is drawn, because the card is centred vertically and a card that grew
  // downward from a fixed top would put a five-line paragraph off the bottom of the screen.
  const auto bodyWidth = static_cast<std::uint32_t>(CARD_WIDTH - 2.0F * CARD_PADDING);

  std::vector<Paragraph> lines;
  for (const Paragraph& paragraph : body)
  {
    if (!lines.empty())
    {
      lines.emplace_back();
    }
    for (std::string& line : FontRenderer::WrapToWidth(paragraph.text, bodyWidth))
    {
      lines.push_back(Paragraph{std::move(line), paragraph.face, paragraph.ink});
    }
  }

  const float bodyHeight = static_cast<float>(lines.size() * LINE_HEIGHT);
  const float cardHeight =
    CARD_PADDING + static_cast<float>(FontRenderer::GlyphHeightPixels()) + TITLE_GAP + bodyHeight + BODY_GAP + BUTTON_HEIGHT + CARD_PADDING;
  const float cardY = std::max(40.0F, (SCREEN_HEIGHT - cardHeight) * 0.5F);

  // ---- The scrim ---------------------------------------------------------------------------------
  _shapes.FillRect(0.0F, 0.0F, SCREEN_WIDTH, SCREEN_HEIGHT, SCRIM);

  _shapes.FillRect(CARD_X, cardY, CARD_WIDTH, cardHeight, CARD_FILL);
  _shapes.StrokeRect(CARD_X, cardY, CARD_WIDTH, cardHeight, look.border);

  const auto contentX = static_cast<std::int32_t>(CARD_X + CARD_PADDING);
  auto y = static_cast<std::int32_t>(cardY + CARD_PADDING);

  _text.DrawText(contentX, y, title, look.title, Face::MonoDisplay);
  y += static_cast<std::int32_t>(FontRenderer::GlyphHeightPixels() + TITLE_GAP);

  for (const Paragraph& line : lines)
  {
    _text.DrawText(contentX, y, line.text, line.ink.alpha == 0 ? TEXT_DETAIL : line.ink, line.face);
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

/// The band under the top bar, spanning all three columns (ADR-085).
///
/// **No scrim, and 44 pixels.** 44 is the top bar's height and the sheet row's, so the band reads as
/// another row of the frame rather than as something laid over it -- which is the whole point: the
/// board behind it is still readable and still works.
void ConnectionDialog::DrawBanner(ShapeRenderer& _shapes, FontRenderer& _text, const std::string& _title, const Look& _look,
                                  const std::vector<Button>& _buttons)
{
  constexpr float BANNER_TOP = 44.0F;
  constexpr float BANNER_HEIGHT = 44.0F;

  _shapes.FillRect(0.0F, BANNER_TOP, SCREEN_WIDTH, BANNER_HEIGHT, CARD_FILL);
  _shapes.StrokeRect(0.0F, BANNER_TOP, SCREEN_WIDTH, BANNER_HEIGHT, _look.border);

  // One line, not the card's four. The band has room for what is happening and when it will next be
  // tried, and the rest of what the card said is true whether or not it is on the screen.
  const std::string next = m_facts.reconnects == 0
                             ? std::format("{} - RECONNECTING IN {}", _title, Uppercased(Seconds(m_facts.secondsToNextAttempt)))
                             : std::format("{} - BACK {} ALREADY - RETRYING IN {}", _title,
                                           m_facts.reconnects == 1 ? std::string{"ONCE"} : std::format("{} TIMES", m_facts.reconnects),
                                           Uppercased(Seconds(m_facts.secondsToNextAttempt)));
  _text.DrawText(static_cast<std::int32_t>(CARD_PADDING) + 4, CenterTextY(BANNER_TOP, BANNER_HEIGHT), next, _look.title);

  const float buttonY = BANNER_TOP + (BANNER_HEIGHT - BUTTON_HEIGHT) * 0.5F;
  float right = SCREEN_WIDTH - CARD_PADDING - 4.0F;
  for (auto button = _buttons.rbegin(); button != _buttons.rend(); ++button)
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

  // A MODAL swallows everything else: a tap that fell through to the map behind would edit orders
  // this client cannot send, and the player would have no way to know which counted. **A banner
  // swallows only its own buttons** (ADR-085) -- the board behind it is the thing it deliberately
  // leaves reachable, and the page's own guards are what stop an order being given there.
  return Modal();
}

} // namespace Lockstep
