#pragma once

#include "FontRenderer.h"
#include "Protocol.h"
#include "ShapeRenderer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Lockstep
{

/// Screens 04 and 05: what the connection is doing, said over the screen it is doing it to.
///
/// **This is the half of the client that was a `MessageBoxA` and an `EXIT_FAILURE`.** Every state a
/// connection can be in that is not "playing" had no drawing at all: a refusal after the join screen
/// handed off put up a Win32 message box in a game that draws its own everything and then killed the
/// process; a dropped link showed nothing while the reconnect loop ran underneath; and a client
/// welcomed by a lobby that has not started its match sat looking at the reference fixture -- a
/// FAKE MATCH with invented empires -- for as long as the host took to arrange the seats.
///
/// **One component for six states rather than six dialogs.** They differ in a title, a tone, a
/// paragraph and two buttons, and nothing else; SCREENS.md 05 draws them as one component for the
/// same reason. What varies is passed in as `Facts` every frame, so the dialog is never stale --
/// the countdown in a CONNECTION LOST dialog is live, which is the point of screen 04.
///
/// It draws over whatever is already on the screen and owns a scrim. The caller flushes both
/// renderers before calling `Draw`, exactly as `MainPage` flushes between its own two layers.
class ConnectionDialog
{
public:
  enum class Kind : std::uint8_t
  {
    /// Nothing is wrong and nothing is drawn.
    None,
    /// The socket is open and the `Hello` is sent; no `Welcome` yet.
    Connecting,
    /// Welcomed by a LOBBY. There is no match to draw, and there may not be for some minutes.
    Waiting,
    /// The server said no. `Facts::reason` says which no.
    Refused,
    /// The link dropped. The reconnect loop is running underneath this (screen 04).
    Lost,
    /// The match is over. Not a refusal -- see the note on `Facts::reason`.
    Finished
  };

  /// What a button press means. The caller decides what to do about it, because what `Back` means
  /// depends on whether there is a join screen behind this dialog.
  enum class Action : std::uint8_t
  {
    None,
    /// Stop trying and return to the fields.
    Cancel,
    Back,
    /// `Back`, with the caret put in the token field. The commonest refusal is a typo.
    EditToken,
    Retry,
    Quit,
    /// Dismiss and go back to the match screen, which still holds the last digest.
    ViewLastDigest
  };

  /// Everything the words need that the dialog cannot know.
  struct Facts
  {
    /// Connecting and Lost: which server.
    std::string server;

    /// Refused: which refusal.
    ///
    /// **`MatchFinished` never arrives here**, because no server sends it: the end of a match is
    /// found in the snapshot (`MatchHeader::finished`) and reaches this dialog as `Kind::Finished`.
    /// The enumerator is on the wire and unused, which is worth knowing before somebody wires a
    /// refusal path to it and finds the dialog already covered.
    Neuron::RefusalReason reason = Neuron::RefusalReason::None;

    /// Waiting: which seat this client holds. -1 when it does not know.
    std::int32_t seat = -1;

    /// Lost: how many times this connection has already come back, and how long until the next try.
    std::uint32_t reconnects = 0;
    double secondsToNextAttempt = 0.0;

    /// Lost: the lock runs whether or not you are back, so it is the most important line in the
    /// dialog. Empty when this client has never seen a tick.
    std::string lockCountdown;

    /// Finished: the standings line, already formatted by whoever has the state.
    std::string standings;

    /// Whether there is a screen behind this one worth going back to. The join screen sets it; the
    /// match loop does not, and gets `QUIT` where `BACK` would be.
    bool canGoBack = false;
  };

  /// Set every frame from the connection. Cheap, and it is what makes screen 04's countdown live.
  void Update(Kind _kind, const Facts& _facts, double _elapsedSeconds);

  [[nodiscard]] Kind Showing() const noexcept
  {
    return m_kind;
  }
  [[nodiscard]] bool Visible() const noexcept
  {
    return m_kind != Kind::None;
  }

  void Draw(Neuron::ShapeRenderer& _shapes, Neuron::FontRenderer& _text);

  /// True when the tap hit the dialog at all -- including the scrim, which swallows it. A dialog
  /// that let taps through to the map behind it would let a player edit orders they cannot send.
  bool HandleTap(float _xPixels, float _yPixels);

  /// Taken, so a held finger presses a button once.
  [[nodiscard]] Action TakeAction() noexcept;

private:
  struct Button
  {
    std::string label;
    Action action = Action::None;
    bool filled = false;
  };

  struct Hit
  {
    float x;
    float y;
    float width;
    float height;
    Action action;
  };

  /// How a dialog is dressed.
  ///
  /// **The border and the title are two colours, not one.** They were one until a neutral dialog
  /// was looked at: the neutral tone is white at 26 alpha, which is right for a hairline border and
  /// leaves a title all but invisible. A red or amber tone is used for both, because those are
  /// readable and the colour is half of what they say.
  struct Look
  {
    Neuron::Color border;
    Neuron::Color title;
  };

  /// The title, the look, the paragraph and the buttons, for whatever `m_kind` and `m_facts` say.
  /// Built in one place so that a state cannot end up with a red border and a reassuring sentence.
  void Compose(std::string& _outTitle, Look& _outLook, std::vector<std::string>& _outBody, std::vector<Button>& _outButtons) const;

  Kind m_kind = Kind::None;
  Facts m_facts;

  /// How long this dialog has been up. The CONNECTING dialog counts it out loud, because a wait
  /// with a number on it is a wait somebody will sit through.
  double m_shownSeconds = 0.0;

  Action m_action = Action::None;
  std::vector<Hit> m_hits;
};

} // namespace Lockstep
