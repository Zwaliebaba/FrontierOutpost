#pragma once

#include "BotPolicy.h"
#include "FontRenderer.h"
#include "KeyboardInput.h"
#include "MatchState.h"
#include "ShapeRenderer.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Lockstep
{

/// Tokens for a lobby's seats, one each.
///
/// **Generated before any screen exists**, because the server is opened with them: a token has to
/// be on its list before anybody can present one. `XXXX-XXXX` from an alphabet with no `0`/`O` and
/// no `1`/`I` -- these are read off one screen, pasted through a chat window and typed into
/// another, and those are the two pairs people confuse (ADR-036).
[[nodiscard]] std::vector<std::string> GenerateSeatTokens(std::int32_t _count);

/// The seats screen: who is playing, and what each of them has to type to prove it.
///
/// **It runs before the match exists** (ADR-036). The number of seats decides how the galaxy is
/// generated — one capital and one starting cluster per player — so the count has to be settled
/// before `Match::Create`, and `ENTER MATCH` is what creates it. Nothing on this screen is live
/// because there is nothing running yet to be live; the mockup's `CONNECTED` badges need a second
/// phase and ADR-036 says which one.
///
/// It is the host's screen and nobody else's. A player who joins with a token never sees it.
class SeatsPage
{
public:
  /// What a seat is for. `Bot` plays itself, from `BotPolicy` (ADR-037).
  enum class Kind : std::uint8_t
  {
    Human,
    Bot
  };

  /// What happens to a seat whose player has not turned up by the time the host wants to start.
  ///
  /// **This is what makes `ENTER MATCH` reachable when somebody does not show.** A seat set to
  /// `BotTakesOver` counts as ready even while empty, and becomes a bot the moment the host
  /// enters; a seat set to `GoesCustodian` holds the whole lobby until its player connects, which
  /// is the right default because the host usually does want to wait for a friend.
  enum class IfWaiting : std::uint8_t
  {
    BotTakesOver,
    GoesCustodian
  };

  struct Seat
  {
    Kind kind = Kind::Human;
    IfWaiting ifWaiting = IfWaiting::GoesCustodian;
    /// How this seat plays when it is a bot. Kept across a flip back to HUMAN, so a host who
    /// toggles a seat twice does not lose the style they chose.
    BotPolicy policy = BotPolicy::ExpandNear;
    /// `XXXX-XXXX`, generated. Empty seats carry one too, so that turning a seat on does not have
    /// to invent one while somebody is looking at it.
    std::string token;
    /// The empire's name, from the same list the match uses, so the host can tell friends which
    /// one they are before the match starts.
    std::string name;
  };

  /// Six seats, which is `MINIMUM_PLAYERS` and therefore every seat a match must have.
  ///
  /// **The screen shrank to six and lost a control by doing so** (owner, 2026-09-11). With twelve
  /// cards a host could choose how many were playing; with six they cannot, because `MatchRules`
  /// refuses fewer than six and this screen shows no more. So `EMPTY` went: a seat that cannot be
  /// empty needs no button saying it could be. What is left is the thing the screen is actually
  /// for -- a token per seat, and whether that seat's player has arrived.
  ///
  /// Going back to twelve is changing this number and the column count beside it. The rules have
  /// always allowed up to twelve (`MAXIMUM_PLAYERS`) and nothing here assumes six beyond layout.
  static constexpr std::int32_t SEAT_COUNT = 6;

  /// The tokens come from the caller now, because the lobby was opened with them before this
  /// screen existed: a token has to be on the server's list before anybody can present it.
  explicit SeatsPage(std::vector<std::string> _tokens);

  /// Who is on a seat, from the server. Refreshed every frame.
  void SetConnected(const std::vector<bool>& _connected);

  /// Whether every seat is ready to play. `ENTER MATCH` waits for this: a match that started
  /// without somebody would spend its first ticks putting them in custody.
  ///
  /// A seat is ready if it is a bot, or its player has connected, or it is marked `BotTakesOver` --
  /// which is the host saying they have waited long enough.
  [[nodiscard]] bool EveryoneIsHere() const;

  /// Which seats play themselves, in seat order, for `MatchSimulation`. One entry per playing seat;
  /// an empty entry is a person's.
  ///
  /// **Call it after `TakeEnterRequest`**, because entering is what turns a seat nobody came to
  /// into a bot -- before that the host can still change their mind and wait.
  [[nodiscard]] std::vector<std::optional<BotPolicy>> Roster() const;

  void DrawWorld(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);
  void DrawInterface(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  bool HandleTap(float _xPixels, float _yPixels);
  void HandleKey(Neuron::KeyboardInput::Key _key);

  /// True once, when the host has asked to start. Taken, so a held finger starts one match.
  [[nodiscard]] bool TakeEnterRequest() noexcept;

  /// How many seats the match has. Seats fill from the top, so this is also the index one past the
  /// last playing seat, and a token's position in the list is the player index it becomes.
  ///
  /// **Seats are a suffix rather than a set** (ADR-036, revised): the lobby is opened with twelve
  /// tokens before anybody has chosen anything, and the server maps token *n* to player *n*. A host
  /// who could empty a seat in the middle would renumber everybody behind it and invalidate tokens
  /// already sent. So HUMAN on seat *k* fills every seat above it and EMPTY on seat *k* empties
  /// every seat below.
  [[nodiscard]] std::int32_t SeatCount() const noexcept
  {
    return m_seatCount;
  }

  /// The tokens of the seats that are playing, in seat order. Its size is `playerCount`.
  [[nodiscard]] std::vector<std::string> PlayingTokens() const;

  [[nodiscard]] std::int32_t PlayingCount() const;

  /// Which seat the host took, as an index into `PlayingTokens`, or -1 when they have not said.
  [[nodiscard]] std::int32_t HostSeat() const;

  /// What the clipboard was last given, for the caller to put there. Taken and cleared.
  [[nodiscard]] std::string TakeCopyRequest();

private:
  struct Hit
  {
    float x;
    float y;
    float width;
    float height;
    std::int32_t action;
    std::int32_t seat;
  };

  void AddHit(float _x, float _y, float _width, float _height, std::int32_t _action, std::int32_t _seat);
  void DrawSeatCard(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, std::int32_t _index, float _x, float _y, float _width,
                    float _height);
  void DrawDetail(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);
  void DrawFooter(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  /// Whether this seat will not hold the match up: a bot, somebody connected, or a seat the host
  /// has already said they will not wait for.
  [[nodiscard]] bool SeatIsReady(std::int32_t _seat) const;

  /// The player index this seat will have, or -1 when it is empty. Seats close up: a player index
  /// is a position in the generator's output, not a label the host chose (ADR-036).
  [[nodiscard]] std::int32_t PlayerIndexOf(std::int32_t _seat) const;

  std::array<Seat, SEAT_COUNT> m_seats;
  std::array<bool, SEAT_COUNT> m_connected = {};
  std::int32_t m_seatCount = 6;
  std::int32_t m_selected = 0;
  std::int32_t m_hostSeat = 0;

  bool m_enterRequested = false;
  std::string m_copyRequest;
  std::string m_refusal;

  std::vector<Hit> m_hits;
};

} // namespace Lockstep
