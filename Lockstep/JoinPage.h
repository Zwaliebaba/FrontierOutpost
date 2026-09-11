#pragma once

#include "FontRenderer.h"
#include "KeyboardInput.h"
#include "OrbitCamera.h"
#include "ShapeRenderer.h"
#include "Starfield.h"
#include "TextField.h"

#include <string>

namespace Lockstep
{

/// Screen 03: the first thing a player sees, and the only screen in this game that types.
///
/// **It exists because a token has to come from somewhere.** ADR-029 made a token a seat rather
/// than a password, and until now the only way to present one was `--join host:port --token x` on a
/// command line — which is fine for six friends on a desktop and impossible on the phone the
/// blueprint says this game is for. ADR-034 amended ADR-014's interface layer for this screen
/// alone, and `Neuron::TextField` is the whole of that amendment.
///
/// It draws in two layers like `MainPage`, for the reason in `MainPage::DrawWorld`: the interface
/// is two renderers and each is one batch, so the sky has to be flushed before the card is drawn
/// over it or the card's own text would be the only thing under the stars.
class JoinPage
{
public:
  /// What the screen is doing, which is the difference between a card you can type into and one
  /// that is waiting for an answer.
  enum class Status : std::uint8_t
  {
    /// Nothing has been tried. The fields are editable and JOIN is live.
    Ready,
    /// A `Hello` is on its way and the fields are inert.
    Connecting,
    /// The server said no. The reason is shown and the fields are editable again.
    Refused,
    /// Welcomed. The composition root takes over and shows the match.
    Joined
  };

  JoinPage();

  /// The sky. Flush both renderers after this and before `DrawInterface`.
  void DrawWorld(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);
  void DrawInterface(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  /// Returns true when the tap hit something.
  bool HandleTap(float _xPixels, float _yPixels);

  /// Characters and keys, routed to whichever field has the caret.
  void HandleTyped(std::string_view _typed);
  void HandleKey(Neuron::KeyboardInput::Key _key);

  /// Seeds the fields. The server is what the command line or the last session offered; the token
  /// is whatever the player was given, which is usually nothing.
  void Offer(std::string_view _server, std::string_view _token);

  [[nodiscard]] std::string Server() const
  {
    return m_server.Text();
  }
  [[nodiscard]] std::string Token() const
  {
    return m_token.Text();
  }

  /// True once, when the player has asked to join. Taken, so a held finger does not reconnect.
  [[nodiscard]] bool TakeJoinRequest() noexcept;

  void SetStatus(Status _status, std::string_view _detail = {});
  [[nodiscard]] Status CurrentStatus() const noexcept
  {
    return m_status;
  }

  /// What the server said in its `Welcome`, once it has. Until then the seat box is dashed and
  /// says nothing, because a seat nobody has confirmed is a guess.
  void SetSeat(std::int32_t _seat, std::int32_t _players, const Neuron::Color& _color);

  /// The countdown line under the card, when the client knows one.
  void SetMatchSummary(std::string_view _summary);

  /// Advances the caret's blink. The one thing on this screen that moves on its own.
  void Update(double _elapsedSeconds);

private:
  /// Which field the keyboard is talking to.
  enum class Focus : std::uint8_t
  {
    Server,
    Token
  };

  struct Hit
  {
    float x;
    float y;
    float width;
    float height;
    std::int32_t action;
  };

  void AddHit(float _x, float _y, float _width, float _height, std::int32_t _action);
  void DrawField(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text, float _y, std::string_view _label, std::string_view _right,
                 const Neuron::TextField& _field, bool _focused, bool _masked, std::int32_t _action);

  Neuron::Starfield m_sky;
  Neuron::OrbitCamera m_camera;

  Neuron::TextField m_server{48};
  Neuron::TextField m_token{32};
  Focus m_focus = Focus::Token;
  bool m_revealToken = false;

  Status m_status = Status::Ready;
  std::string m_detail;
  std::string m_matchSummary;

  std::int32_t m_seat = -1;
  std::int32_t m_players = 0;
  Neuron::Color m_seatColor = {94, 196, 255, 255};

  bool m_joinRequested = false;
  double m_blinkSeconds = 0.0;

  std::vector<Hit> m_hits;
};

} // namespace Lockstep
