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
constexpr std::int32_t GRID_COLUMNS = 4;
constexpr float CARD_GAP = 8.0F;
constexpr float CARD_WIDTH = (GRID_WIDTH - CARD_GAP * (GRID_COLUMNS - 1)) / GRID_COLUMNS;
constexpr float CARD_HEIGHT = 104.0F;

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
constexpr std::int32_t ACTION_EMPTY = 2;
constexpr std::int32_t ACTION_HUMAN = 3;
constexpr std::int32_t ACTION_BOT = 4;
constexpr std::int32_t ACTION_COPY = 5;
constexpr std::int32_t ACTION_NEW_TOKEN = 6;
constexpr std::int32_t ACTION_TAKE_SEAT = 7;
constexpr std::int32_t ACTION_BOT_TAKES_OVER = 8;
constexpr std::int32_t ACTION_GOES_CUSTODIAN = 9;
constexpr std::int32_t ACTION_FILL = 10;
constexpr std::int32_t ACTION_ENTER = 11;

/// The twelve empires, in the order the generator hands out player indices. The same list the match
/// uses, so a host can tell somebody which empire they are before anybody has connected.
constexpr std::array<const char*, SeatsPage::SEAT_COUNT> EMPIRE_NAMES = {"HALVORSEN", "SORNE", "OKONKWO", "TAMSIN", "VARGA", "IDRIS",
                                                                         "DUNMORE",   "NARTH", "VESK",    "ORUNE",  "PELL",  "KEPLER"};

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

SeatsPage::SeatsPage()
{
  // Seeded off the clock, because a token that was the same in every match would be one anybody
  // could type without being told it. ADR-029 is clear that this is not security -- it is the
  // difference between a seat you were given and a seat you guessed.
  const auto now = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  Neuron::Prng prng{now ^ 0x9E37'79B9'7F4A'7C15ULL};

  for (std::int32_t index = 0; index < SEAT_COUNT; ++index)
  {
    m_seats[static_cast<std::size_t>(index)].token = MakeToken(prng);
    m_seats[static_cast<std::size_t>(index)].name = EMPIRE_NAMES[static_cast<std::size_t>(index)];

    // Six human seats to begin with, which is the smallest playable match (`MINIMUM_PLAYERS`) and
    // therefore the fewest decisions a host has to make before the screen is usable.
    m_seats[static_cast<std::size_t>(index)].kind = index < static_cast<std::int32_t>(MINIMUM_PLAYERS) ? Kind::Human : Kind::Empty;
  }
}

std::int32_t SeatsPage::PlayerIndexOf(std::int32_t _seat) const
{
  if (m_seats[static_cast<std::size_t>(_seat)].kind == Kind::Empty)
  {
    return -1;
  }

  std::int32_t player = 0;
  for (std::int32_t index = 0; index < _seat; ++index)
  {
    player += m_seats[static_cast<std::size_t>(index)].kind == Kind::Empty ? 0 : 1;
  }
  return player;
}

std::int32_t SeatsPage::PlayingCount() const
{
  std::int32_t playing = 0;
  for (const Seat& seat : m_seats)
  {
    playing += seat.kind == Kind::Empty ? 0 : 1;
  }
  return playing;
}

std::vector<std::string> SeatsPage::PlayingTokens() const
{
  std::vector<std::string> tokens;
  for (const Seat& seat : m_seats)
  {
    if (seat.kind != Kind::Empty)
    {
      tokens.push_back(seat.token);
    }
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
  if (_key == Neuron::KeyboardInput::Key::Enter && PlayingCount() >= static_cast<std::int32_t>(MINIMUM_PLAYERS))
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

    case ACTION_EMPTY:
      m_selected = hit->seat;
      // The host's own seat cannot be emptied from under them: a match where nobody is the host is
      // a match nobody can enter.
      if (hit->seat != m_hostSeat)
      {
        seat.kind = Kind::Empty;
      }
      return true;

    case ACTION_HUMAN:
      m_selected = hit->seat;
      seat.kind = Kind::Human;
      return true;

    case ACTION_BOT:
      // Refused, and said so. ADR-036: the policies are still in the test suite, and a seat that
      // claimed BOT and then played nothing would be a worse lie than this one.
      m_selected = hit->seat;
      m_refusal = "Bots are not built yet. A seat can be HUMAN or EMPTY.";
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

    case ACTION_TAKE_SEAT:
      // Taking a seat fills it, because the host is a player and an empty seat cannot be one.
      m_hostSeat = m_selected;
      m_seats[static_cast<std::size_t>(m_selected)].kind = Kind::Human;
      return true;

    case ACTION_BOT_TAKES_OVER:
      seat.ifWaiting = IfWaiting::BotTakesOver;
      m_refusal = "Recorded. It needs bots before it can happen.";
      return true;

    case ACTION_GOES_CUSTODIAN:
      seat.ifWaiting = IfWaiting::GoesCustodian;
      return true;

    case ACTION_FILL:
      m_refusal = "Bots are not built yet, so there is nothing to fill them with.";
      return true;

    case ACTION_ENTER:
      if (PlayingCount() < static_cast<std::int32_t>(MINIMUM_PLAYERS))
      {
        m_refusal = std::format("A match needs at least {} seats.", MINIMUM_PLAYERS);
        return true;
      }
      m_enterRequested = true;
      return true;

    default:
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
  const bool empty = seat.kind == Kind::Empty;

  _shapes.FillRect(_x, _y, _width, _height, CARD_FILL);
  _shapes.StrokeRect(_x, _y, _width, _height, selected ? BLUE : CARD_BORDER);
  AddHit(_x, _y, _width, _height, ACTION_SELECT, _index);

  // The swatch is the empire's colour, and it is the only place on this screen a player's colour
  // appears before the map does (ADR-027).
  const std::int32_t player = PlayerIndexOf(_index);
  const std::int32_t headerY = static_cast<std::int32_t>(_y) + 10;
  if (!empty)
  {
    _shapes.FillRect(_x + 10.0F, static_cast<float>(headerY), 8.0F, 8.0F, OwnerColor(player, m_hostSeat == _index ? player : -1));
  }
  else
  {
    _shapes.StrokeRect(_x + 10.0F, static_cast<float>(headerY), 8.0F, 8.0F, OUTLINE);
  }

  _text.DrawText(static_cast<std::int32_t>(_x) + 24, headerY, std::format("SEAT {:02}", _index + 1), empty ? NEUTRAL_DIM : TEXT_PRIMARY);

  const std::string right = empty ? "-" : (m_hostSeat == _index ? std::string{"YOU"} : seat.name);
  const auto rightWidth = static_cast<float>(FontRenderer::MeasurePixels(right));
  _text.DrawText(static_cast<std::int32_t>(_x + _width - rightWidth) - 10, headerY, right,
                 empty ? NEUTRAL_DIM : (m_hostSeat == _index ? BLUE : TEXT_MUTED));

  // ---- The body ----------------------------------------------------------------------------------
  std::int32_t lineY = headerY + LINE_HEIGHT + 8;
  const std::size_t columns = FontRenderer::FitCharacters(static_cast<std::uint32_t>(_width - 20.0F));

  if (empty)
  {
    for (const std::string& line : FontRenderer::Wrap("Empty. Nobody plays this seat.", columns))
    {
      _text.DrawText(static_cast<std::int32_t>(_x) + 10, lineY, line, NEUTRAL_DIM);
      lineY += LINE_HEIGHT;
    }
  }
  else
  {
    _text.DrawText(static_cast<std::int32_t>(_x) + 10, lineY, "TOKEN", TEXT_MUTED);
    _text.DrawText(static_cast<std::int32_t>(_x) + 10 + static_cast<std::int32_t>(FontRenderer::MeasurePixels("TOKEN ")), lineY, seat.token,
                   TEXT_PRIMARY);
    lineY += LINE_HEIGHT;
    _text.DrawText(static_cast<std::int32_t>(_x) + 10, lineY, m_hostSeat == _index ? "THIS IS YOUR SEAT" : "WAITING FOR PLAYER",
                   m_hostSeat == _index ? BLUE : AMBER);
    lineY += LINE_HEIGHT;
  }

  // ---- EMPTY | HUMAN | BOT -----------------------------------------------------------------------
  const float toggleY = _y + _height - 26.0F;
  const float toggleWidth = (_width - 20.0F) / 3.0F;
  const std::array<const char*, 3> labels = {"EMPTY", "HUMAN", "BOT"};
  const std::array<std::int32_t, 3> actions = {ACTION_EMPTY, ACTION_HUMAN, ACTION_BOT};
  const std::array<Kind, 3> kinds = {Kind::Empty, Kind::Human, Kind::Bot};

  for (std::size_t slot = 0; slot < labels.size(); ++slot)
  {
    const float toggleX = _x + 10.0F + static_cast<float>(slot) * toggleWidth;
    const bool on = seat.kind == kinds[slot];
    const bool possible = kinds[slot] != Kind::Bot;

    if (on)
    {
      _shapes.FillRect(toggleX, toggleY, toggleWidth - 2.0F, 18.0F, BLUE);
    }
    else
    {
      _shapes.StrokeRect(toggleX, toggleY, toggleWidth - 2.0F, 18.0F, possible ? OUTLINE : DIVIDER);
    }

    const auto labelWidth = static_cast<float>(FontRenderer::MeasurePixels(labels[slot]));
    _text.DrawText(static_cast<std::int32_t>(toggleX + (toggleWidth - 2.0F - labelWidth) * 0.5F), CenterTextY(toggleY, 18.0F), labels[slot],
                   on ? APP_BACKGROUND : (possible ? TEXT_PRIMARY : NEUTRAL_DIM));
    AddHit(toggleX, toggleY, toggleWidth - 2.0F, 18.0F, actions[slot], _index);
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
  const bool empty = seat.kind == Kind::Empty;

  std::int32_t y = static_cast<std::int32_t>(TOP_BAR_HEIGHT) + 14;
  _text.DrawText(static_cast<std::int32_t>(contentX), y, std::format("SEAT {:02} - {}", m_selected + 1, empty ? "EMPTY" : seat.name),
                 TEXT_PRIMARY);
  y += LINE_HEIGHT + 8;

  const std::string_view blurb = empty ? "Nobody plays this seat. It is not in the match and its empire is not generated."
                                       : "A human seat. The token is the seat: whoever enters it plays this empire.";
  for (const std::string& line : FontRenderer::Wrap(blurb, columns))
  {
    _text.DrawText(static_cast<std::int32_t>(contentX), y, line, TEXT_DETAIL);
    y += LINE_HEIGHT;
  }
  y += 8;

  if (!empty)
  {
    // ---- The token, and the two things you do with it -------------------------------------------
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

    _shapes.StrokeRect(contentX + halfWidth + 8.0F, static_cast<float>(y), halfWidth, 22.0F, OUTLINE);
    _text.DrawText(static_cast<std::int32_t>(contentX + halfWidth) + 18, CenterTextY(static_cast<float>(y), 22.0F), "TAKE SEAT",
                   m_hostSeat == m_selected ? NEUTRAL_DIM : TEXT_PRIMARY);
    if (m_hostSeat != m_selected)
    {
      AddHit(contentX + halfWidth + 8.0F, static_cast<float>(y), halfWidth, 22.0F, ACTION_TAKE_SEAT, m_selected);
    }
    y += 34;
  }

  // ---- If still waiting at the lock -------------------------------------------------------------
  _shapes.FillRect(panelX + 1.0F, static_cast<float>(y), PANEL_WIDTH - 1.0F, 1.0F, DIVIDER);
  y += 10;
  _text.DrawText(static_cast<std::int32_t>(contentX), y, "IF STILL WAITING AT T1 LOCK", TEXT_MUTED);
  y += LINE_HEIGHT + 6;

  const float halfWidth = (contentRight - contentX - 8.0F) * 0.5F;
  const bool takesOver = seat.ifWaiting == IfWaiting::BotTakesOver;

  _shapes.StrokeRect(contentX, static_cast<float>(y), halfWidth, 30.0F, takesOver ? BLUE : DIVIDER);
  _text.DrawText(static_cast<std::int32_t>(contentX) + 6, static_cast<std::int32_t>(y) + 11, "BOT TAKES OVER",
                 takesOver ? BLUE : NEUTRAL_DIM);
  AddHit(contentX, static_cast<float>(y), halfWidth, 30.0F, ACTION_BOT_TAKES_OVER, m_selected);

  _shapes.StrokeRect(contentX + halfWidth + 8.0F, static_cast<float>(y), halfWidth, 30.0F, takesOver ? DIVIDER : BLUE);
  _text.DrawText(static_cast<std::int32_t>(contentX + halfWidth) + 14, static_cast<std::int32_t>(y) + 5, "SEAT GOES",
                 takesOver ? TEXT_MUTED : BLUE);
  _text.DrawText(static_cast<std::int32_t>(contentX + halfWidth) + 14, static_cast<std::int32_t>(y) + 17, "CUSTODIAN",
                 takesOver ? TEXT_MUTED : BLUE);
  AddHit(contentX + halfWidth + 8.0F, static_cast<float>(y), halfWidth, 30.0F, ACTION_GOES_CUSTODIAN, m_selected);
  y += 40;

  for (const std::string& line :
       FontRenderer::Wrap("Seats lock with T1. After that a seat only changes hands by absence, not from this screen.", columns))
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

  const std::string summary = enough ? std::format("{} SEATS - SEND EACH PLAYER THEIR TOKEN - YOU ARE SEAT {:02}", playing, m_hostSeat + 1)
                                     : std::format("{} SEATS - A MATCH NEEDS AT LEAST {}", playing, MINIMUM_PLAYERS);
  _text.DrawText(16, CenterTextY(footerY, FOOTER_HEIGHT), summary, enough ? TEXT_MUTED : RED);

  // ---- ENTER MATCH --------------------------------------------------------------------------------
  const auto enterWidth = static_cast<float>(FontRenderer::MeasurePixels("ENTER MATCH >")) + 24.0F;
  const float enterX = SCREEN_WIDTH - 16.0F - enterWidth;
  if (enough)
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

  // FILL EMPTY WITH BOTS is drawn and refused, for the same reason the BOT toggle is.
  const auto fillWidth = static_cast<float>(FontRenderer::MeasurePixels("FILL EMPTY WITH BOTS")) + 24.0F;
  const float fillX = enterX - 12.0F - fillWidth;
  _shapes.StrokeRect(fillX, footerY + 10.0F, fillWidth, 24.0F, DIVIDER);
  _text.DrawText(static_cast<std::int32_t>(fillX) + 12, CenterTextY(footerY, FOOTER_HEIGHT), "FILL EMPTY WITH BOTS", NEUTRAL_DIM);
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

  std::int32_t humans = 0;
  std::int32_t empties = 0;
  for (const Seat& seat : m_seats)
  {
    humans += seat.kind == Kind::Human ? 1 : 0;
    empties += seat.kind == Kind::Empty ? 1 : 0;
  }

  const std::string census = std::format("{} HUMAN - {} EMPTY", humans, empties);
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
