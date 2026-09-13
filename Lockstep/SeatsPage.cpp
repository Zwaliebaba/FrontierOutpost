// SeatsPage.cpp -- who is playing, and what they have to type.

#include "pch.h"
#include "SeatsPage.h"

#include "DesignTokens.h"

#include "MatchRules.h"
#include "Prng.h"

#include <chrono>
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

/// The console: every control on this screen, in one card over the sky.
///
/// **Sized to its contents rather than to the screen** (owner, 2026-09-12). Six cards and one
/// panel stretched across all 1280x720 left a third of the frame as empty background and made a
/// six-seat lobby read as a twelve-seat one with holes in it. Centred over the star field screen
/// 03 already sits on, the lobby and the join screen look like the one moment of the game they
/// are, and the console is the only thing the eye has to find.
constexpr float CONSOLE_WIDTH = 960.0F;
constexpr float CONSOLE_X = (SCREEN_WIDTH - CONSOLE_WIDTH) * 0.5F;
constexpr float CONSOLE_Y = 180.0F;
constexpr float CONSOLE_PADDING = 16.0F;

/// The name and what this screen is, above the console. The 2x scale is the same exception
/// `JoinPage` takes: DESIGN-GUIDELINES allows it for the one thing on a screen read first.
constexpr float TITLE_Y = 120.0F;
constexpr float SUBTITLE_Y = 151.0F;

constexpr float FOOTER_HEIGHT = 44.0F;

/// Baseline to baseline, from the font. See `MainPage::LINE_HEIGHT`.
constexpr std::int32_t LINE_HEIGHT = static_cast<std::int32_t>(Neuron::FontRenderer::LineHeightPixels());

/// The grid and the detail panel, inside the console.
constexpr float PANEL_WIDTH = 256.0F;
constexpr float GRID_X = CONSOLE_X + CONSOLE_PADDING;
constexpr float GRID_TOP = CONSOLE_Y + CONSOLE_PADDING;
constexpr float GRID_WIDTH = CONSOLE_WIDTH - 3.0F * CONSOLE_PADDING - PANEL_WIDTH;
constexpr std::int32_t GRID_COLUMNS = 3;
constexpr float CARD_GAP = 10.0F;
constexpr float CARD_WIDTH = (GRID_WIDTH - CARD_GAP * (GRID_COLUMNS - 1)) / GRID_COLUMNS;
/// A seat card, tall enough for what it stacks rather than a number that was once tall enough.
///
/// **120 was right for an 8px font and wrong the moment a line became 17px.** The card fills from
/// the top -- header, TOKEN, the token, the status -- and anchors its HUMAN/BOT AT T1/BOT toggle to
/// the BOTTOM, so the two grew towards each other and met: the status line was drawn straight
/// through the toggle row. Adding the pieces up is what stops that happening again the next time a
/// face changes. The four gaps are the ones `DrawSeatCard` uses, in the order it uses them.
constexpr float CARD_TOP_PADDING = 12.0F;
constexpr float CARD_TOGGLE_HEIGHT = 18.0F;
constexpr float CARD_TOGGLE_MARGIN = 10.0F;
constexpr float CARD_HEIGHT = CARD_TOP_PADDING + static_cast<float>(4 * LINE_HEIGHT + 12 + 2 + 8) + CARD_TOGGLE_HEIGHT + CARD_TOGGLE_MARGIN;
constexpr float GRID_HEIGHT = 2.0F * CARD_HEIGHT + CARD_GAP;

/// The practice offer, in the space two rows of cards leave under them.
///
/// **It is on this screen because this is where a beginner is stuck** (ADR-051). A first match at
/// six hours a tick answers two orders and then asks the player to come back tomorrow; the offer
/// has to be in front of them at the moment they would otherwise start one.
constexpr float PRACTICE_X = GRID_X;
constexpr float PRACTICE_Y = GRID_TOP + GRID_HEIGHT + 14.0F;
constexpr float PRACTICE_WIDTH = GRID_WIDTH;
constexpr float PRACTICE_HEIGHT = 80.0F;

constexpr float PANEL_X = CONSOLE_X + CONSOLE_WIDTH - CONSOLE_PADDING - PANEL_WIDTH;
constexpr float PANEL_TOP = GRID_TOP;
constexpr float PANEL_HEIGHT = GRID_HEIGHT + 14.0F + PRACTICE_HEIGHT;

constexpr float FOOTER_Y = PANEL_TOP + PANEL_HEIGHT + CONSOLE_PADDING;
constexpr float CONSOLE_HEIGHT = FOOTER_Y + FOOTER_HEIGHT - CONSOLE_Y;

/// **One palette, bound to local names** (ADR-083). These were thirteen literal colours copied from
/// the same list, which is thirteen chances for one of them to be adjusted alone -- and the day the
/// contrast floor moved, three files would have kept the old number. The names stay local because
/// they are used a hundred times each in this file and `Ink::` at every site is noise; what moved is
/// where the VALUE comes from, which is the half that could ever be wrong.
constexpr Color APP_BACKGROUND = Ink::APP_BACKGROUND;
constexpr Color CARD_FILL = Ink::CARD_FILL;
constexpr Color CARD_BORDER = Ink::CARD_BORDER;
constexpr Color OUTLINE = Ink::OUTLINE;
constexpr Color DIVIDER = Ink::DIVIDER;
constexpr Color TEXT_PRIMARY = Ink::TEXT_PRIMARY;
constexpr Color TEXT_MUTED = Ink::TEXT_MUTED;
constexpr Color TEXT_DETAIL = Ink::TEXT_DETAIL;
constexpr Color NEUTRAL_DIM = Ink::NEUTRAL_DIM;
constexpr Color BLUE = Ink::BLUE;
constexpr Color AMBER = Ink::AMBER;
constexpr Color RED = Ink::RED;
constexpr Color STAR = Ink::STAR;

constexpr std::int32_t ACTION_SELECT = 1;
constexpr std::int32_t ACTION_HUMAN = 3;
constexpr std::int32_t ACTION_BOT = 4;
constexpr std::int32_t ACTION_COPY = 5;
constexpr std::int32_t ACTION_NEW_TOKEN = 6;
constexpr std::int32_t ACTION_PRACTICE = 7;
/// The middle of the card's three-way: a person's seat that a bot takes at the first lock if they
/// have not arrived (ADR-066). There is no opposite action, because `ACTION_HUMAN` is it.
constexpr std::int32_t ACTION_BOT_TAKES_OVER = 8;
constexpr std::int32_t ACTION_FILL = 10;
constexpr std::int32_t ACTION_ENTER = 11;
/// One per offered style rather than one action carrying an index, because a hit is (action, seat)
/// and the seat is already spoken for.
constexpr std::int32_t ACTION_STYLE_FIRST = 12;

/// The three styles a host can put in a seat, in the order they are drawn. `BotPolicy` has six;
/// the other two are the scripted match's (a bot that never moves at all, and one that never
/// plays) and neither is an opponent anybody would choose.
constexpr std::array<BotPolicy, 3> OFFERED_STYLES = {BotPolicy::Turtle, BotPolicy::ExpandNear, BotPolicy::Raider};

/// The twelve empires, in the order the generator hands out player indices. The same list the match
/// uses, so a host can tell somebody which empire they are before anybody has connected.
constexpr std::array<const char*, SeatsPage::SEAT_COUNT> EMPIRE_NAMES = {"HALVORSEN", "SORNE", "OKONKWO", "TAMSIN", "VARGA", "IDRIS"};

/// No `0`/`O` and no `1`/`I`. These are read off one screen and typed into another, usually after
/// a trip through a chat window, and the two pairs people confuse are the two that cost a support
/// conversation.
constexpr std::string_view TOKEN_ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

[[nodiscard]] std::string MakeToken(Neuron::Prng& _prng)
{
  std::string token;
  for (std::int32_t index = 0; index < 9; ++index)
  {
    if (index == 4)
    {
      token.push_back('-');
      continue;
    }
    token.push_back(TOKEN_ALPHABET[_prng.Below(static_cast<std::uint32_t>(TOKEN_ALPHABET.size()))]);
  }
  return token;
}

} // namespace

std::vector<std::string> GenerateSeatTokens(std::int32_t _count)
{
  // Seeded off the clock, because a token that was the same in every match would be one anybody
  // could type without being told it. ADR-029 is clear this is not security -- it is the difference
  // between a seat you were given and a seat you guessed.
  const auto now = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  Neuron::Prng prng{now ^ 0x9E37'79B9'7F4A'7C15ULL};

  std::vector<std::string> tokens;
  tokens.reserve(static_cast<std::size_t>(_count));
  for (std::int32_t index = 0; index < _count; ++index)
  {
    tokens.push_back(MakeToken(prng));
  }
  return tokens;
}

SeatsPage::SeatsPage(std::vector<std::string> _tokens)
{
  for (std::int32_t index = 0; index < SEAT_COUNT; ++index)
  {
    Seat& seat = m_seats[static_cast<std::size_t>(index)];
    seat.token = index < static_cast<std::int32_t>(_tokens.size()) ? _tokens[static_cast<std::size_t>(index)] : std::string{};
    seat.name = EMPIRE_NAMES[static_cast<std::size_t>(index)];

    // Six to begin with, the smallest playable match (`MINIMUM_PLAYERS`), so the screen is usable
    // before the host has decided anything.
    seat.kind = Kind::Human;
  }
  m_seatCount = SEAT_COUNT;

  // A fixed view of the sky, for `JoinPage`'s reason: nothing on this screen is worth looking away
  // from, and a background that moved on its own would be the only thing on it that did.
  m_camera.SetViewport(0.0F, 0.0F, SCREEN_WIDTH, SCREEN_HEIGHT);
  m_camera.SetTarget({0.0F, 0.0F, 0.0F});
  m_camera.SetDistance(700.0F);
  m_camera.SetOrientation(0.6F, 0.35F);
}

void SeatsPage::SetConnected(const std::vector<bool>& _connected)
{
  for (std::int32_t index = 0; index < SEAT_COUNT; ++index)
  {
    m_connected[static_cast<std::size_t>(index)] =
      index < static_cast<std::int32_t>(_connected.size()) && _connected[static_cast<std::size_t>(index)];
  }
}

bool SeatsPage::EveryoneIsHere() const
{
  for (std::int32_t index = 0; index < m_seatCount; ++index)
  {
    if (!SeatIsReady(index))
    {
      return false;
    }
  }
  return true;
}

bool SeatsPage::SeatIsReady(std::int32_t _seat) const
{
  const Seat& seat = m_seats[static_cast<std::size_t>(_seat)];
  return seat.kind == Kind::Bot || m_connected[static_cast<std::size_t>(_seat)] || seat.ifWaiting == IfWaiting::BotTakesOver;
}

std::vector<std::optional<BotPolicy>> SeatsPage::Roster() const
{
  std::vector<std::optional<BotPolicy>> roster;
  roster.reserve(static_cast<std::size_t>(m_seatCount));
  for (std::int32_t index = 0; index < m_seatCount; ++index)
  {
    const Seat& seat = m_seats[static_cast<std::size_t>(index)];
    roster.push_back(seat.kind == Kind::Bot ? std::optional<BotPolicy>{seat.policy} : std::optional<BotPolicy>{});
  }
  return roster;
}

std::int32_t SeatsPage::PlayerIndexOf(std::int32_t _seat) const
{
  // The seat IS the player. The lobby handed out token *n* for player *n* before this screen
  // existed, so nothing here may renumber anybody.
  return _seat < m_seatCount ? _seat : -1;
}

std::int32_t SeatsPage::PlayingCount() const
{
  return m_seatCount;
}

std::vector<std::string> SeatsPage::PlayingTokens() const
{
  std::vector<std::string> tokens;
  tokens.reserve(static_cast<std::size_t>(m_seatCount));
  for (std::int32_t index = 0; index < m_seatCount; ++index)
  {
    tokens.push_back(m_seats[static_cast<std::size_t>(index)].token);
  }
  return tokens;
}

std::int32_t SeatsPage::HostSeat() const
{
  return PlayerIndexOf(m_hostSeat);
}

bool SeatsPage::BotTakesOverSeat(std::int32_t _seat) const
{
  if (_seat < 0 || _seat >= SEAT_COUNT)
  {
    return false;
  }
  const Seat& seat = m_seats[static_cast<std::size_t>(_seat)];
  return seat.kind == Kind::Human && seat.ifWaiting == IfWaiting::BotTakesOver;
}

std::optional<SeatsPage::Entry> SeatsPage::TakeEnterRequest() noexcept
{
  std::optional<Entry> asked = m_entry;
  m_entry.reset();
  return asked;
}

std::string SeatsPage::TakeCopyRequest()
{
  std::string taken;
  taken.swap(m_copyRequest);
  return taken;
}

std::int32_t SeatsPage::FillWaitingSeatsWithBots()
{
  // Every seat still waiting, except the host's and anybody already on a token. This is the
  // one-tap version of the thing a host actually wants at the end of an evening, and the thing a
  // practice match needs done to every seat at once.
  std::int32_t filled = 0;
  for (std::int32_t index = 0; index < m_seatCount; ++index)
  {
    if (index == m_hostSeat || m_connected[static_cast<std::size_t>(index)])
    {
      continue;
    }
    if (m_seats[static_cast<std::size_t>(index)].kind == Kind::Human)
    {
      m_seats[static_cast<std::size_t>(index)].kind = Kind::Bot;
      ++filled;
    }
  }
  return filled;
}

void SeatsPage::AddHit(float _x, float _y, float _width, float _height, std::int32_t _action, std::int32_t _seat)
{
  m_hits.push_back(Hit{_x, _y, _width, _height, _action, _seat});
}

void SeatsPage::HandleKey(Neuron::KeyboardInput::Key _key)
{
  if (_key == Neuron::KeyboardInput::Key::Enter && PlayingCount() >= static_cast<std::int32_t>(MINIMUM_PLAYERS) && EveryoneIsHere())
  {
    m_entry = Entry::Match;
  }
}

bool SeatsPage::HandleTap(float _xPixels, float _yPixels)
{
  for (auto hit = m_hits.rbegin(); hit != m_hits.rend(); ++hit)
  {
    const bool inside = _xPixels >= hit->x && _xPixels < hit->x + hit->width && _yPixels >= hit->y && _yPixels < hit->y + hit->height;
    if (!inside)
    {
      continue;
    }

    Seat& seat = m_seats[static_cast<std::size_t>(hit->seat >= 0 ? hit->seat : m_selected)];
    switch (hit->action)
    {
    case ACTION_SELECT:
      m_selected = hit->seat;
      return true;

    case ACTION_HUMAN:
      // The plain human seat, which is also the one that waits: the three-way's left segment is
      // `Kind::Human` and `GoesCustodian` together (ADR-066), so picking it takes back a takeover
      // the host had already agreed to.
      m_selected = hit->seat;
      seat.kind = Kind::Human;
      seat.ifWaiting = IfWaiting::GoesCustodian;
      m_refusal.clear();
      return true;

    case ACTION_BOT:
      m_selected = hit->seat;
      if (hit->seat == m_hostSeat)
      {
        // The host is sitting in it. Letting them hand their own seat to a bot would leave the
        // process that owns the match with nothing to draw.
        m_refusal = "That is your seat. Take another one first.";
        return true;
      }
      if (m_connected[static_cast<std::size_t>(hit->seat)])
      {
        // Somebody is already on the token. The seat is theirs until they disconnect.
        m_refusal = std::format("{} has already connected to that seat.", seat.name);
        return true;
      }
      seat.kind = Kind::Bot;
      m_refusal.clear();
      return true;

    case ACTION_COPY:
      m_copyRequest = seat.token;
      return true;

    case ACTION_NEW_TOKEN:
    {
      const auto now = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
      Neuron::Prng prng{now};
      seat.token = MakeToken(prng);
      return true;
    }

    case ACTION_BOT_TAKES_OVER:
      m_selected = hit->seat;
      if (hit->seat == m_hostSeat)
      {
        // The host is sitting in it and cannot be the player who did not turn up.
        m_refusal = "That is your seat. You are already here.";
        return true;
      }
      seat.kind = Kind::Human;
      seat.ifWaiting = IfWaiting::BotTakesOver;
      m_refusal.clear();
      return true;

    case ACTION_FILL:
      m_refusal = FillWaitingSeatsWithBots() == 0 ? std::string{"Every seat already has somebody in it."} : std::string{};
      return true;

    case ACTION_PRACTICE:
      // **It does not wait for anybody, and that is the point** (ADR-051). A practice match is one
      // person against five bots, so every other seat becomes one here rather than the host being
      // asked to fill them first and then enter. A player already connected keeps their seat --
      // the host can still practise with a friend watching, and taking somebody's seat out from
      // under them is the one thing FILL has always refused to do.
      (void)FillWaitingSeatsWithBots();
      m_refusal.clear();
      m_entry = Entry::Practice;
      return true;

    case ACTION_ENTER:
      if (PlayingCount() < static_cast<std::int32_t>(MINIMUM_PLAYERS))
      {
        m_refusal = std::format("A match needs at least {} seats.", MINIMUM_PLAYERS);
        return true;
      }
      if (!EveryoneIsHere())
      {
        // The wait is the point. A match that began without somebody would spend its first ticks
        // putting them into custody for missing a game they were still being invited to.
        m_refusal = "Every seat needs a player connected, a bot, or BOT TAKES OVER.";
        return true;
      }

      // **Entering is what cashes in BOT TAKES OVER.** Up to this moment the host can still change
      // their mind and wait; past it the seat is a bot and the roster the match is built from says
      // so, which is what makes a reloaded store play the same game (ADR-037).
      for (std::int32_t index = 0; index < m_seatCount; ++index)
      {
        Seat& waiting = m_seats[static_cast<std::size_t>(index)];
        if (waiting.kind == Kind::Human && !m_connected[static_cast<std::size_t>(index)])
        {
          waiting.kind = Kind::Bot;
        }
      }
      m_entry = Entry::Match;
      return true;

    default:
      if (hit->action >= ACTION_STYLE_FIRST && hit->action < ACTION_STYLE_FIRST + static_cast<std::int32_t>(OFFERED_STYLES.size()))
      {
        seat.policy = OFFERED_STYLES[static_cast<std::size_t>(hit->action - ACTION_STYLE_FIRST)];
        seat.kind = Kind::Bot;
        m_refusal.clear();
      }
      return true;
    }
  }
  return false;
}

void SeatsPage::DrawWorld(ShapeRenderer& _shapes, FontRenderer& _text)
{
  (void)_text;
  m_hits.clear();
  _shapes.FillRect(0.0F, 0.0F, SCREEN_WIDTH, SCREEN_HEIGHT, APP_BACKGROUND);
  m_sky.Draw(_shapes, m_camera, STAR);
}

void SeatsPage::DrawSeatCard(ShapeRenderer& _shapes, FontRenderer& _text, std::int32_t _index, float _x, float _y, float _width,
                             float _height)
{
  const Seat& seat = m_seats[static_cast<std::size_t>(_index)];
  const bool selected = _index == m_selected;
  const bool here = m_connected[static_cast<std::size_t>(_index)];
  const bool mine = m_hostSeat == _index;

  _shapes.FillRect(_x, _y, _width, _height, CARD_FILL);
  _shapes.StrokeRect(_x, _y, _width, _height, selected ? BLUE : CARD_BORDER);
  AddHit(_x, _y, _width, _height, ACTION_SELECT, _index);

  // The swatch is the empire's colour, and this is the only place a player's colour appears before
  // the map does (ADR-027).
  const std::int32_t headerY = static_cast<std::int32_t>(_y) + 12;
  _shapes.FillRect(_x + 12.0F, static_cast<float>(headerY), 8.0F, 8.0F, OwnerColor(_index, mine ? _index : -1));
  _text.DrawText(static_cast<std::int32_t>(_x) + 26, headerY, std::format("SEAT {:02}", _index + 1), TEXT_PRIMARY);

  const std::string right = mine ? std::string{"YOU"} : seat.name;
  const auto rightWidth = static_cast<float>(FontRenderer::MeasurePixels(right));
  _text.DrawText(static_cast<std::int32_t>(_x + _width - rightWidth) - 12, headerY, right, mine ? BLUE : TEXT_MUTED);

  // ---- The token, which is the whole reason this card exists ------------------------------------
  std::int32_t lineY = headerY + LINE_HEIGHT + 12;
  _text.DrawText(static_cast<std::int32_t>(_x) + 12, lineY, "TOKEN", TEXT_MUTED);
  lineY += LINE_HEIGHT + 2;
  _text.DrawText(static_cast<std::int32_t>(_x) + 12, lineY, seat.token, TEXT_PRIMARY);
  lineY += LINE_HEIGHT + 8;

  std::string status;
  Color statusColor = AMBER;
  if (seat.kind == Kind::Bot)
  {
    status = std::format("BOT - {}", Describe(seat.policy));
    statusColor = NEUTRAL_DIM;
  }
  else if (mine)
  {
    status = "CONNECTED - YOU";
    statusColor = BLUE;
  }
  else if (here)
  {
    status = "CONNECTED";
    statusColor = BLUE;
  }
  else if (seat.ifWaiting == IfWaiting::BotTakesOver)
  {
    status = "BOT WILL TAKE OVER";
  }
  else
  {
    status = "WAITING FOR PLAYER";
  }
  _text.DrawText(static_cast<std::int32_t>(_x) + 12, lineY, status, statusColor);

  // ---- HUMAN | BOT AT T1 | BOT -------------------------------------------------------------------
  //
  // **One control for one question** (ADR-066). Who plays this seat has three answers -- a person, a
  // person a bot takes over from at the first lock, or a bot now -- and they used to be asked twice,
  // once on the card and once in the detail panel, in words that did not obviously belong to the
  // same question.
  //
  // The segments are sized to their labels rather than cut into equal thirds: `BOT AT T1` is nine
  // glyphs and a third of a 212-pixel card is seven.
  const float toggleY = _y + _height - CARD_TOGGLE_HEIGHT - CARD_TOGGLE_MARGIN;
  const float room = _width - 24.0F;
  const std::array<const char*, 3> labels = {"HUMAN", "BOT AT T1", "BOT"};
  const std::array<std::int32_t, 3> actions = {ACTION_HUMAN, ACTION_BOT_TAKES_OVER, ACTION_BOT};

  float measured = 0.0F;
  for (const char* label : labels)
  {
    measured += static_cast<float>(FontRenderer::MeasurePixels(label)) + 12.0F;
  }
  const float gap = std::max(2.0F, (room - measured) / static_cast<float>(labels.size() - 1));

  const bool waits = seat.kind == Kind::Human && seat.ifWaiting == IfWaiting::BotTakesOver;
  const std::array<bool, 3> lit = {seat.kind == Kind::Human && !waits, waits, seat.kind == Kind::Bot};

  // A seat somebody is already sitting on cannot be handed to a bot, and neither can the host's --
  // and the host is never the player who did not turn up, so the middle is theirs to skip too.
  const std::array<bool, 3> possible = {true, !mine, !here && !mine};

  float toggleX = _x + 12.0F;
  for (std::size_t slot = 0; slot < labels.size(); ++slot)
  {
    const auto labelWidth = static_cast<float>(FontRenderer::MeasurePixels(labels[slot]));
    const float segmentWidth = labelWidth + 12.0F;

    if (lit[slot])
    {
      _shapes.FillRect(toggleX, toggleY, segmentWidth, CARD_TOGGLE_HEIGHT, BLUE);
    }
    else
    {
      _shapes.StrokeRect(toggleX, toggleY, segmentWidth, CARD_TOGGLE_HEIGHT, possible[slot] ? OUTLINE : DIVIDER);
    }

    _text.DrawText(static_cast<std::int32_t>(toggleX + (segmentWidth - labelWidth) * 0.5F), CenterTextY(toggleY, CARD_TOGGLE_HEIGHT),
                   labels[slot], lit[slot] ? APP_BACKGROUND : (possible[slot] ? TEXT_PRIMARY : NEUTRAL_DIM));
    AddHit(toggleX, toggleY, segmentWidth, CARD_TOGGLE_HEIGHT, actions[slot], _index);
    toggleX += segmentWidth + gap;
  }
}

void SeatsPage::DrawDetail(ShapeRenderer& _shapes, FontRenderer& _text)
{
  const float contentX = PANEL_X;
  const float contentRight = PANEL_X + PANEL_WIDTH;
  const auto detailWidth = static_cast<std::uint32_t>(contentRight - contentX);

  // The column rule, halfway across the gutter. The console already painted the background; a
  // second fill here would only put an opaque rectangle over the card fill it sits in.
  _shapes.FillRect(PANEL_X - CONSOLE_PADDING * 0.5F, PANEL_TOP, 1.0F, PANEL_HEIGHT, DIVIDER);

  const Seat& seat = m_seats[static_cast<std::size_t>(m_selected)];

  std::int32_t y = static_cast<std::int32_t>(PANEL_TOP);
  _text.DrawText(static_cast<std::int32_t>(contentX), y, std::format("SEAT {:02} - {}", m_selected + 1, seat.name), TEXT_PRIMARY);
  y += LINE_HEIGHT + 8;

  for (const std::string& line :
       FontRenderer::WrapToWidth("The token is the seat: whoever enters it plays this empire. Send it to the person playing.", detailWidth))
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), y, line, TEXT_DETAIL, Face::SansRegular);
    y += LINE_HEIGHT;
  }
  y += 10;

  // ---- The token, and the two things you do with it ---------------------------------------------
  _shapes.StrokeRect(contentX, static_cast<float>(y), contentRight - contentX, 24.0F, CARD_BORDER);
  _text.DrawText(static_cast<std::int32_t>(contentX) + 8, CenterTextY(static_cast<float>(y), 24.0F), seat.token, TEXT_PRIMARY);

  const auto copyWidth = static_cast<float>(FontRenderer::MeasurePixels("COPY"));
  _text.DrawText(static_cast<std::int32_t>(contentRight - copyWidth) - 8, CenterTextY(static_cast<float>(y), 24.0F), "COPY", BLUE);
  AddHit(contentRight - copyWidth - 16.0F, static_cast<float>(y), copyWidth + 16.0F, 24.0F, ACTION_COPY, m_selected);
  y += 32;

  const float halfWidth = (contentRight - contentX - 8.0F) * 0.5F;
  _shapes.StrokeRect(contentX, static_cast<float>(y), halfWidth, 22.0F, OUTLINE);
  _text.DrawText(static_cast<std::int32_t>(contentX) + 10, CenterTextY(static_cast<float>(y), 22.0F), "NEW TOKEN", TEXT_PRIMARY);
  AddHit(contentX, static_cast<float>(y), halfWidth, 22.0F, ACTION_NEW_TOKEN, m_selected);

  // **`TAKE SEAT` was here and it lied.** It moved `m_hostSeat`, which is what decides whose seat
  // is protected from the BOT toggle and which seats FILL WAITING skips -- while the host's actual
  // seat is fixed by the token their client presented before this screen opened, and nothing here
  // can change that. A host who took another seat could then hand their own empire to a bot and
  // enter a match somebody else was playing for them. Removed rather than repaired: making it real
  // means reconnecting with a different token, which is the composition root's to do (ADR-041).
  y += 28;

  for (const std::string& line : FontRenderer::WrapToWidth(m_hostSeat == m_selected ? "This is your seat: you logged in with this token."
                                                                                    : "Not yours. You hold the seat you logged in with.",
                                                           detailWidth))
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), y, line, m_hostSeat == m_selected ? BLUE : NEUTRAL_DIM, Face::SansRegular);
    y += LINE_HEIGHT;
  }
  y += 14;

  _shapes.FillRect(contentX, static_cast<float>(y), PANEL_WIDTH, 1.0F, DIVIDER);
  y += 10;

  // ---- How a bot plays ---------------------------------------------------------------------------
  //
  // Shown instead of the waiting rule rather than beside it, because a bot is never waiting. The
  // panel says one thing about the selected seat and which thing depends on who is in it.
  if (seat.kind == Kind::Bot)
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), y, "HOW IT PLAYS", TEXT_MUTED);
    y += LINE_HEIGHT + 6;

    for (std::size_t slot = 0; slot < OFFERED_STYLES.size(); ++slot)
    {
      const bool chosen = seat.policy == OFFERED_STYLES[slot];
      _shapes.StrokeRect(contentX, static_cast<float>(y), contentRight - contentX, 20.0F, chosen ? BLUE : DIVIDER);
      _text.DrawText(static_cast<std::int32_t>(contentX) + 8, CenterTextY(static_cast<float>(y), 20.0F), Describe(OFFERED_STYLES[slot]),
                     chosen ? BLUE : TEXT_PRIMARY);
      AddHit(contentX, static_cast<float>(y), contentRight - contentX, 20.0F, ACTION_STYLE_FIRST + static_cast<std::int32_t>(slot),
             m_selected);
      y += 26;
    }

    y += 4;
    for (const std::string& line :
         FontRenderer::WrapToWidth("A bot sees exactly what a player in this seat would see, and nothing more.", detailWidth))
    {
      _text.DrawText(static_cast<std::int32_t>(contentX), y, line, NEUTRAL_DIM, Face::SansRegular);
      y += LINE_HEIGHT;
    }

    return;
  }

  // ---- What the card's three-way means for this seat ---------------------------------------------
  //
  // **The panel says what the setting DOES and does not offer a second way to change it** (ADR-066).
  // `IF STILL WAITING AT T1 LOCK`, with `BOT TAKES OVER | SEAT GOES CUSTODIAN` under it, asked the
  // same question the card asks and answered it in different words, so a host reading both had two
  // controls and one setting.
  const bool takesOver = seat.ifWaiting == IfWaiting::BotTakesOver;

  for (const std::string& line :
       FontRenderer::WrapToWidth(takesOver ? "BOT AT T1: this seat is ready to start without its player, and entering makes it a bot."
                                           : "HUMAN: entering waits for this player. Set the card to BOT AT T1 to start without them.",
                                 detailWidth))
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), y, line, NEUTRAL_DIM, Face::SansRegular);
    y += LINE_HEIGHT;
  }
}

/// The practice offer, and the answer to the question a six-hour tick asks a beginner.
///
/// **ADR-051.** It sits under the grid rather than in the footer beside `ENTER MATCH` because it is
/// not a variant of entering -- it needs no seats filled, waits for nobody, and is the right answer
/// for exactly one person, which a button in a row of lobby controls would not say.
void SeatsPage::DrawPractice(ShapeRenderer& _shapes, FontRenderer& _text)
{
  _shapes.StrokeRect(PRACTICE_X, PRACTICE_Y, PRACTICE_WIDTH, PRACTICE_HEIGHT, CARD_BORDER);

  // The label and the button share the top row, so the two lines under them run the full width of
  // the box. Under the prose the button had to be cut into it, and both read as crowded.
  const auto buttonWidth = static_cast<float>(FontRenderer::MeasurePixels("PRACTICE MATCH ›")) + 24.0F;
  const float buttonX = PRACTICE_X + PRACTICE_WIDTH - 12.0F - buttonWidth;
  const float buttonY = PRACTICE_Y + 10.0F;

  _text.DrawText(static_cast<std::int32_t>(PRACTICE_X) + 12, CenterTextY(buttonY, 24.0F), "FIRST MATCH?", AMBER);

  _shapes.StrokeRect(buttonX, buttonY, buttonWidth, 24.0F, AMBER);
  _text.DrawText(static_cast<std::int32_t>(buttonX) + 12, CenterTextY(buttonY, 24.0F), "PRACTICE MATCH ›", AMBER);
  AddHit(buttonX, buttonY, buttonWidth, 24.0F, ACTION_PRACTICE, -1);

  // The comparison a beginner is actually making, in the two numbers that differ. Everything else
  // about the match is the same one, which is this sentence's whole job (`PracticeRules`).
  const std::array<const char*, 2> lines = {"The same rules and the same galaxy, on a tick every two minutes instead of six hours.",
                                            "Five bots, thirty ticks, about an hour to play. Nobody else has to turn up."};
  std::int32_t lineY = static_cast<std::int32_t>(PRACTICE_Y) + 44;
  for (const char* line : lines)
  {
    _text.DrawText(static_cast<std::int32_t>(PRACTICE_X) + 12, lineY, line, TEXT_DETAIL, Face::SansRegular);
    lineY += LINE_HEIGHT + 2;
  }
}

void SeatsPage::DrawFooter(ShapeRenderer& _shapes, FontRenderer& _text)
{
  const float footerY = FOOTER_Y;
  _shapes.FillRect(CONSOLE_X, footerY, CONSOLE_WIDTH, 1.0F, CARD_BORDER);

  const std::int32_t playing = PlayingCount();
  const bool enough = playing >= static_cast<std::int32_t>(MINIMUM_PLAYERS);

  const bool everyone = EveryoneIsHere();

  // ---- Who is still missing ----------------------------------------------------------------------
  //
  // Named rather than counted. "Waiting for 3" tells the host to wait; "waiting for SORNE, TAMSIN"
  // tells them who to go and ask.
  std::vector<std::string> missing;
  std::int32_t bots = 0;
  for (std::int32_t index = 0; index < m_seatCount; ++index)
  {
    if (m_seats[static_cast<std::size_t>(index)].kind == Kind::Bot)
    {
      ++bots;
    }
    else if (!SeatIsReady(index))
    {
      missing.push_back(m_seats[static_cast<std::size_t>(index)].name);
    }
  }

  std::string summary;
  if (!enough)
  {
    summary = std::format("{} SEATS - A MATCH NEEDS AT LEAST {}", playing, MINIMUM_PLAYERS);
  }
  else if (!missing.empty())
  {
    summary = std::format("WAITING FOR {}", missing.front());
    for (std::size_t index = 1; index < missing.size() && index < 3; ++index)
    {
      summary += ", " + missing[index];
    }
    if (missing.size() > 3)
    {
      summary += std::format(", +{}", missing.size() - 3);
    }
    summary += std::format(" · {} OF {} HERE", playing - static_cast<std::int32_t>(missing.size()), playing);
  }
  else
  {
    summary = bots == 0 ? std::format("ALL {} SEATS CONNECTED - YOU ARE SEAT {:02}", playing, m_hostSeat + 1)
                        : std::format("{} SEATS READY ({} BOT) - YOU ARE SEAT {:02}", playing, bots, m_hostSeat + 1);
  }

  // The refusal is the answer to a button in this row, so it is said in this row. In the detail
  // panel it answered beside the wrong question, and it was the one thing that could overrun the
  // panel's height.
  const bool refused = !m_refusal.empty();
  _text.DrawText(static_cast<std::int32_t>(CONSOLE_X + CONSOLE_PADDING), CenterTextY(footerY, FOOTER_HEIGHT), refused ? m_refusal : summary,
                 refused ? AMBER : (!enough ? RED : (everyone ? BLUE : AMBER)), refused ? Face::SansRegular : Face::MonoRegular);

  // ---- ENTER MATCH --------------------------------------------------------------------------------
  const auto enterWidth = static_cast<float>(FontRenderer::MeasurePixels("ENTER MATCH ›")) + 24.0F;
  const float enterX = CONSOLE_X + CONSOLE_WIDTH - CONSOLE_PADDING - enterWidth;
  if (enough && everyone)
  {
    _shapes.FillRect(enterX, footerY + 10.0F, enterWidth, 24.0F, BLUE);
    _text.DrawText(static_cast<std::int32_t>(enterX) + 12, CenterTextY(footerY, FOOTER_HEIGHT), "ENTER MATCH ›", APP_BACKGROUND);
    AddHit(enterX, footerY + 10.0F, enterWidth, 24.0F, ACTION_ENTER, -1);
  }
  else
  {
    _shapes.StrokeRect(enterX, footerY + 10.0F, enterWidth, 24.0F, DIVIDER);
    _text.DrawText(static_cast<std::int32_t>(enterX) + 12, CenterTextY(footerY, FOOTER_HEIGHT), "ENTER MATCH ›", NEUTRAL_DIM);
  }

  const auto fillWidth = static_cast<float>(FontRenderer::MeasurePixels("FILL WAITING WITH BOTS")) + 24.0F;
  const float fillX = enterX - 12.0F - fillWidth;
  _shapes.StrokeRect(fillX, footerY + 10.0F, fillWidth, 24.0F, OUTLINE);
  _text.DrawText(static_cast<std::int32_t>(fillX) + 12, CenterTextY(footerY, FOOTER_HEIGHT), "FILL WAITING WITH BOTS", TEXT_PRIMARY);
  AddHit(fillX, footerY + 10.0F, fillWidth, 24.0F, ACTION_FILL, -1);
}

void SeatsPage::DrawInterface(ShapeRenderer& _shapes, FontRenderer& _text)
{
  // ---- The name, at 2x, and what this screen is -----------------------------------------------
  //
  // The same title block screen 03 opens with, so that a host who has just come off the join screen
  // sees the lobby arrive under the same two lines rather than under a bar that replaced them.
  _text.DrawText(static_cast<std::int32_t>(CONSOLE_X), static_cast<std::int32_t>(TITLE_Y), "LOCKSTEP", TEXT_PRIMARY, Face::MonoMedium,
                 FontRenderer::COUNTDOWN_SCALE);
  _text.DrawText(static_cast<std::int32_t>(CONSOLE_X), static_cast<std::int32_t>(SUBTITLE_Y), "SEATS - BEFORE THE MATCH STARTS",
                 TEXT_MUTED);

  // Seats, then the people among them. Counting seats as humans was right up until a seat could be
  // a bot, at which point a six-seat match with five bots announced itself as "1 SEATS".
  std::int32_t humans = 0;
  std::int32_t here = 0;
  for (std::int32_t index = 0; index < m_seatCount; ++index)
  {
    if (m_seats[static_cast<std::size_t>(index)].kind != Kind::Human)
    {
      continue;
    }
    ++humans;
    here += m_connected[static_cast<std::size_t>(index)] ? 1 : 0;
  }
  const std::string census = std::format("{} SEATS · {} OF {} CONNECTED", m_seatCount, here, humans);
  const auto censusWidth = static_cast<float>(FontRenderer::MeasurePixels(census));
  _text.DrawText(static_cast<std::int32_t>(CONSOLE_X + CONSOLE_WIDTH - censusWidth), static_cast<std::int32_t>(SUBTITLE_Y), census,
                 TEXT_MUTED);

  // ---- The console ---------------------------------------------------------------------------
  //
  // INK FIRST, then the card's own 4% white, exactly as `JoinPage` does it and for the same reason:
  // 4% white over stars is stars, and what is behind a card has to stop being readable.
  _shapes.FillRect(CONSOLE_X, CONSOLE_Y, CONSOLE_WIDTH, CONSOLE_HEIGHT, APP_BACKGROUND);
  _shapes.FillRect(CONSOLE_X, CONSOLE_Y, CONSOLE_WIDTH, CONSOLE_HEIGHT, CARD_FILL);
  _shapes.StrokeRect(CONSOLE_X, CONSOLE_Y, CONSOLE_WIDTH, CONSOLE_HEIGHT, CARD_BORDER);

  // ---- The grid ------------------------------------------------------------------------------
  for (std::int32_t index = 0; index < SEAT_COUNT; ++index)
  {
    // Row and column as integers, converted afterwards. The division is integer ON PURPOSE -- it is
    // which row the seat is on -- and a `/` under a `static_cast<float>` reads like a float division
    // somebody got wrong.
    const std::int32_t column = index % GRID_COLUMNS;
    const std::int32_t row = index / GRID_COLUMNS;

    const float cardX = GRID_X + static_cast<float>(column) * (CARD_WIDTH + CARD_GAP);
    const float cardY = GRID_TOP + static_cast<float>(row) * (CARD_HEIGHT + CARD_GAP);
    DrawSeatCard(_shapes, _text, index, cardX, cardY, CARD_WIDTH, CARD_HEIGHT);
  }

  DrawPractice(_shapes, _text);
  DrawDetail(_shapes, _text);
  DrawFooter(_shapes, _text);
}

} // namespace Lockstep
