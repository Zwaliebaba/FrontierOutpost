// SeatsPage.cpp -- who is playing, and what they have to type.

#include "pch.h"
#include "SeatsPage.h"

#include "MatchRules.h"
#include "Prng.h"

#include <chrono>
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
constexpr float TOP_BAR_HEIGHT = 44.0F;
constexpr float FOOTER_HEIGHT = 44.0F;

/// The grid and the detail panel. The panel is the same 260 the locks rail is, so the two screens
/// share an edge and the eye does not have to relearn where the right-hand column starts.
constexpr float PANEL_WIDTH = 260.0F;
constexpr float GRID_X = 12.0F;
constexpr float GRID_TOP = TOP_BAR_HEIGHT + 12.0F;
constexpr float GRID_WIDTH = SCREEN_WIDTH - PANEL_WIDTH - 2.0F * GRID_X;
constexpr std::int32_t GRID_COLUMNS = 3;
constexpr float CARD_GAP = 8.0F;
constexpr float CARD_WIDTH = (GRID_WIDTH - CARD_GAP * (GRID_COLUMNS - 1)) / GRID_COLUMNS;
constexpr float CARD_HEIGHT = 132.0F;

constexpr std::int32_t LINE_HEIGHT = 12;

constexpr Color APP_BACKGROUND = {11, 14, 20, 255};
constexpr Color CARD_FILL = {255, 255, 255, 10};
constexpr Color CARD_BORDER = {255, 255, 255, 26};
constexpr Color OUTLINE = {255, 255, 255, 51};
constexpr Color DIVIDER = {255, 255, 255, 18};
constexpr Color TEXT_PRIMARY = {240, 243, 247, 255};
constexpr Color TEXT_MUTED = {214, 220, 228, 140};
constexpr Color TEXT_DETAIL = {214, 220, 228, 153};
constexpr Color NEUTRAL_DIM = {214, 220, 228, 115};
constexpr Color BLUE = {94, 196, 255, 255};
constexpr Color AMBER = {255, 196, 87, 255};
constexpr Color RED = {255, 110, 96, 255};

constexpr std::int32_t ACTION_SELECT = 1;
constexpr std::int32_t ACTION_HUMAN = 3;
constexpr std::int32_t ACTION_BOT = 4;
constexpr std::int32_t ACTION_COPY = 5;
constexpr std::int32_t ACTION_NEW_TOKEN = 6;
constexpr std::int32_t ACTION_BOT_TAKES_OVER = 8;
constexpr std::int32_t ACTION_GOES_CUSTODIAN = 9;
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

[[nodiscard]] std::int32_t CenterTextY(float _y, float _height)
{
  return static_cast<std::int32_t>(_y + (_height - static_cast<float>(FontRenderer::GLYPH_HEIGHT_TEXELS)) * 0.5F);
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

bool SeatsPage::TakeEnterRequest() noexcept
{
  const bool asked = m_enterRequested;
  m_enterRequested = false;
  return asked;
}

std::string SeatsPage::TakeCopyRequest()
{
  std::string taken;
  taken.swap(m_copyRequest);
  return taken;
}

void SeatsPage::AddHit(float _x, float _y, float _width, float _height, std::int32_t _action, std::int32_t _seat)
{
  m_hits.push_back(Hit{_x, _y, _width, _height, _action, _seat});
}

void SeatsPage::HandleKey(Neuron::KeyboardInput::Key _key)
{
  if (_key == Neuron::KeyboardInput::Key::Enter && PlayingCount() >= static_cast<std::int32_t>(MINIMUM_PLAYERS) && EveryoneIsHere())
  {
    m_enterRequested = true;
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
      m_selected = hit->seat;
      seat.kind = Kind::Human;
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
      seat.ifWaiting = IfWaiting::BotTakesOver;
      m_refusal.clear();
      return true;

    case ACTION_GOES_CUSTODIAN:
      seat.ifWaiting = IfWaiting::GoesCustodian;
      return true;

    case ACTION_FILL:
    {
      // Every seat still waiting, except the host's and anybody already on a token. This is the
      // one-tap version of the thing a host actually wants at the end of an evening.
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
      m_refusal = filled == 0 ? std::string{"Every seat already has somebody in it."} : std::string{};
      return true;
    }

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
      m_enterRequested = true;
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

  // ---- HUMAN | BOT -------------------------------------------------------------------------------
  const float toggleY = _y + _height - 28.0F;
  const float toggleWidth = (_width - 24.0F) / 2.0F;
  const std::array<const char*, 2> labels = {"HUMAN", "BOT"};
  const std::array<std::int32_t, 2> actions = {ACTION_HUMAN, ACTION_BOT};
  const std::array<Kind, 2> kinds = {Kind::Human, Kind::Bot};

  for (std::size_t slot = 0; slot < labels.size(); ++slot)
  {
    const float toggleX = _x + 12.0F + static_cast<float>(slot) * toggleWidth;
    const bool on = seat.kind == kinds[slot];

    // A seat somebody is already sitting on cannot be handed to a bot, and neither can the host's.
    const bool possible = kinds[slot] != Kind::Bot || (!here && !mine);

    if (on)
    {
      _shapes.FillRect(toggleX, toggleY, toggleWidth - 3.0F, 18.0F, BLUE);
    }
    else
    {
      _shapes.StrokeRect(toggleX, toggleY, toggleWidth - 3.0F, 18.0F, possible ? OUTLINE : DIVIDER);
    }

    const auto labelWidth = static_cast<float>(FontRenderer::MeasurePixels(labels[slot]));
    _text.DrawText(static_cast<std::int32_t>(toggleX + (toggleWidth - 3.0F - labelWidth) * 0.5F), CenterTextY(toggleY, 18.0F), labels[slot],
                   on ? APP_BACKGROUND : (possible ? TEXT_PRIMARY : NEUTRAL_DIM));
    AddHit(toggleX, toggleY, toggleWidth - 3.0F, 18.0F, actions[slot], _index);
  }
}

void SeatsPage::DrawDetail(ShapeRenderer& _shapes, FontRenderer& _text)
{
  const float panelX = SCREEN_WIDTH - PANEL_WIDTH;
  const float contentX = panelX + 14.0F;
  const float contentRight = SCREEN_WIDTH - 14.0F;
  const std::size_t columns = FontRenderer::FitCharacters(static_cast<std::uint32_t>(contentRight - contentX));

  _shapes.FillRect(panelX, TOP_BAR_HEIGHT, PANEL_WIDTH, SCREEN_HEIGHT - TOP_BAR_HEIGHT - FOOTER_HEIGHT, APP_BACKGROUND);
  _shapes.FillRect(panelX, TOP_BAR_HEIGHT, 1.0F, SCREEN_HEIGHT - TOP_BAR_HEIGHT - FOOTER_HEIGHT, CARD_BORDER);

  const Seat& seat = m_seats[static_cast<std::size_t>(m_selected)];

  std::int32_t y = static_cast<std::int32_t>(TOP_BAR_HEIGHT) + 14;
  _text.DrawText(static_cast<std::int32_t>(contentX), y, std::format("SEAT {:02} - {}", m_selected + 1, seat.name), TEXT_PRIMARY);
  y += LINE_HEIGHT + 8;

  for (const std::string& line :
       FontRenderer::Wrap("The token is the seat: whoever enters it plays this empire. Send it to the person playing.", columns))
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), y, line, TEXT_DETAIL);
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
  _text.DrawText(static_cast<std::int32_t>(contentX), y,
                 m_hostSeat == m_selected ? "This is your seat: you logged in with this token."
                                          : "Not yours. You hold the seat you logged in with.",
                 m_hostSeat == m_selected ? BLUE : NEUTRAL_DIM);
  y += LINE_HEIGHT + 14;

  _shapes.FillRect(panelX + 1.0F, static_cast<float>(y), PANEL_WIDTH - 1.0F, 1.0F, DIVIDER);
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
         FontRenderer::Wrap("A bot sees exactly what a player in this seat would see, and nothing more.", columns))
    {
      _text.DrawText(static_cast<std::int32_t>(contentX), y, line, NEUTRAL_DIM);
      y += LINE_HEIGHT;
    }

    if (!m_refusal.empty())
    {
      y += 10;
      for (const std::string& line : FontRenderer::Wrap(m_refusal, columns))
      {
        _text.DrawText(static_cast<std::int32_t>(contentX), y, line, AMBER);
        y += LINE_HEIGHT;
      }
    }
    return;
  }

  // ---- If still waiting at the lock --------------------------------------------------------------
  _text.DrawText(static_cast<std::int32_t>(contentX), y, "IF STILL WAITING AT T1 LOCK", TEXT_MUTED);
  y += LINE_HEIGHT + 6;

  const bool takesOver = seat.ifWaiting == IfWaiting::BotTakesOver;

  _shapes.StrokeRect(contentX, static_cast<float>(y), halfWidth, 30.0F, takesOver ? BLUE : DIVIDER);
  _text.DrawText(static_cast<std::int32_t>(contentX) + 6, static_cast<std::int32_t>(y) + 5, "BOT TAKES", takesOver ? BLUE : NEUTRAL_DIM);
  _text.DrawText(static_cast<std::int32_t>(contentX) + 6, static_cast<std::int32_t>(y) + 17, "OVER", takesOver ? BLUE : NEUTRAL_DIM);
  AddHit(contentX, static_cast<float>(y), halfWidth, 30.0F, ACTION_BOT_TAKES_OVER, m_selected);

  _shapes.StrokeRect(contentX + halfWidth + 8.0F, static_cast<float>(y), halfWidth, 30.0F, takesOver ? DIVIDER : BLUE);
  _text.DrawText(static_cast<std::int32_t>(contentX + halfWidth) + 14, static_cast<std::int32_t>(y) + 5, "SEAT GOES",
                 takesOver ? TEXT_MUTED : BLUE);
  _text.DrawText(static_cast<std::int32_t>(contentX + halfWidth) + 14, static_cast<std::int32_t>(y) + 17, "CUSTODIAN",
                 takesOver ? TEXT_MUTED : BLUE);
  AddHit(contentX + halfWidth + 8.0F, static_cast<float>(y), halfWidth, 30.0F, ACTION_GOES_CUSTODIAN, m_selected);
  y += 40;

  for (const std::string& line :
       FontRenderer::Wrap(takesOver ? "This seat is ready to start without its player: entering makes it a bot."
                                    : "Entering waits for this player. Switch to BOT TAKES OVER to start without them.",
                          columns))
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), y, line, NEUTRAL_DIM);
    y += LINE_HEIGHT;
  }

  // Whatever was last refused, said where the thing that refused it is.
  if (!m_refusal.empty())
  {
    y += 10;
    for (const std::string& line : FontRenderer::Wrap(m_refusal, columns))
    {
      _text.DrawText(static_cast<std::int32_t>(contentX), y, line, AMBER);
      y += LINE_HEIGHT;
    }
  }
}

void SeatsPage::DrawFooter(ShapeRenderer& _shapes, FontRenderer& _text)
{
  const float footerY = SCREEN_HEIGHT - FOOTER_HEIGHT;
  _shapes.FillRect(0.0F, footerY, SCREEN_WIDTH, FOOTER_HEIGHT, APP_BACKGROUND);
  _shapes.FillRect(0.0F, footerY, SCREEN_WIDTH, 1.0F, CARD_BORDER);

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
    summary += std::format(" - {} OF {} HERE", playing - static_cast<std::int32_t>(missing.size()), playing);
  }
  else
  {
    summary = bots == 0 ? std::format("ALL {} SEATS CONNECTED - YOU ARE SEAT {:02}", playing, m_hostSeat + 1)
                        : std::format("{} SEATS READY ({} BOT) - YOU ARE SEAT {:02}", playing, bots, m_hostSeat + 1);
  }
  _text.DrawText(16, CenterTextY(footerY, FOOTER_HEIGHT), summary, !enough ? RED : (everyone ? BLUE : AMBER));

  // ---- ENTER MATCH --------------------------------------------------------------------------------
  const auto enterWidth = static_cast<float>(FontRenderer::MeasurePixels("ENTER MATCH >")) + 24.0F;
  const float enterX = SCREEN_WIDTH - 16.0F - enterWidth;
  if (enough && everyone)
  {
    _shapes.FillRect(enterX, footerY + 10.0F, enterWidth, 24.0F, BLUE);
    _text.DrawText(static_cast<std::int32_t>(enterX) + 12, CenterTextY(footerY, FOOTER_HEIGHT), "ENTER MATCH >", APP_BACKGROUND);
    AddHit(enterX, footerY + 10.0F, enterWidth, 24.0F, ACTION_ENTER, -1);
  }
  else
  {
    _shapes.StrokeRect(enterX, footerY + 10.0F, enterWidth, 24.0F, DIVIDER);
    _text.DrawText(static_cast<std::int32_t>(enterX) + 12, CenterTextY(footerY, FOOTER_HEIGHT), "ENTER MATCH >", NEUTRAL_DIM);
  }

  const auto fillWidth = static_cast<float>(FontRenderer::MeasurePixels("FILL WAITING WITH BOTS")) + 24.0F;
  const float fillX = enterX - 12.0F - fillWidth;
  _shapes.StrokeRect(fillX, footerY + 10.0F, fillWidth, 24.0F, OUTLINE);
  _text.DrawText(static_cast<std::int32_t>(fillX) + 12, CenterTextY(footerY, FOOTER_HEIGHT), "FILL WAITING WITH BOTS", TEXT_PRIMARY);
  AddHit(fillX, footerY + 10.0F, fillWidth, 24.0F, ACTION_FILL, -1);
}

void SeatsPage::DrawInterface(ShapeRenderer& _shapes, FontRenderer& _text)
{
  // ---- The top bar ---------------------------------------------------------------------------
  _shapes.FillRect(0.0F, 0.0F, SCREEN_WIDTH, TOP_BAR_HEIGHT, APP_BACKGROUND);
  _shapes.FillRect(0.0F, TOP_BAR_HEIGHT - 1.0F, SCREEN_WIDTH, 1.0F, CARD_BORDER);

  const std::int32_t centered = CenterTextY(0.0F, TOP_BAR_HEIGHT);
  _text.DrawText(16, centered, "LOCKSTEP", TEXT_PRIMARY);
  _text.DrawText(16 + static_cast<std::int32_t>(FontRenderer::MeasurePixels("LOCKSTEP")) + 12, centered, "SEATS - BEFORE THE MATCH STARTS",
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
  const std::string census = std::format("{} SEATS - {} OF {} CONNECTED", m_seatCount, here, humans);
  const auto censusWidth = static_cast<float>(FontRenderer::MeasurePixels(census));
  _text.DrawText(static_cast<std::int32_t>(SCREEN_WIDTH - censusWidth) - 16, centered, census, TEXT_MUTED);

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

  DrawDetail(_shapes, _text);
  DrawFooter(_shapes, _text);
}

} // namespace Lockstep
