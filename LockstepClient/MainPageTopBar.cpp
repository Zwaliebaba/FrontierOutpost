// MainPageTopBar.cpp -- the top bar: the census, the purse and what the queue has taken of it, the
// countdown (ADR-087, ADR-091).
//
// It says what it knows and hides what is not finished: a figure that does not fit is dropped whole
// rather than clipped, which is the rule every bar on this screen follows (SCREENS.md 01).

#include "pch.h"
#include "MainPage.h"

#include "DesignTokens.h"

namespace Lockstep
{

namespace
{

using Neuron::Color;
using Neuron::Face;
using Neuron::FontRenderer;
using Neuron::ShapeRenderer;

/// What a tap does, which the rows composed in here have to name (ADR-113). The page's own enum,
/// aliased rather than qualified thirty times.
using Action = MainPage::Action;

/// 1284 -> "1,284". The reference groups thousands and the score is the number a player checks
/// first, so it is grouped here rather than left as a run of digits.
[[nodiscard]] std::string FormatScore(std::uint32_t _score)
{
  std::string digits = std::to_string(_score);
  for (std::size_t at = digits.size(); at > 3;)
  {
    at -= 3;
    digits.insert(at, ",");
  }
  return digits;
}

} // namespace

void MainPage::DrawTopBar(ShapeRenderer& _shapes, FontRenderer& _text)
{
  const std::int32_t centered = CenterTextY(0.0F, Frame::TOP_BAR_HEIGHT);
  _shapes.FillRect(0.0F, 0.0F, Frame::SCREEN_WIDTH, Frame::TOP_BAR_HEIGHT, Ink::APP_BACKGROUND);
  _shapes.FillRect(0.0F, Frame::TOP_BAR_HEIGHT - 1.0F, Frame::SCREEN_WIDTH, 1.0F, Ink::CARD_BORDER);

  // ---- The right-hand block goes first, and it decides how much room the left one gets ----------
  //
  // The left half is a sentence that grows -- the match id, the day, the player and system counts,
  // and an end time when there is one -- and the right half is a fixed set of facts laid out from
  // the edge inwards. Drawn in the obvious order, the sentence ran under the countdown: `ENDS 22
  // SEP 18:00Z` and `T47 LOCKS` printed on top of each other, which is what a fixed 1280 costs when
  // one side is authored and the other is data. So the right side is measured first and the left is
  // trimmed to fit in front of it.
  // The right group is laid out right to left, because it is anchored to the frame edge and its
  // widest member -- the leader's name -- is the one that changes.
  float cursor = Frame::SCREEN_WIDTH - 16.0F;

  // **`REPLAY` is behind `--dev` until screen 07 is wired** (ADR-091). Its own sheet is titled
  // `REPLAY TICK 7 - NOT YET WIRED`, which is a control teaching a player that the buttons on this
  // screen may do nothing -- the exact lesson ADR-053 and ADR-077 were spent unteaching. It stays on
  // the bar for whoever is building it.
  if (m_developerControls)
  {
    const std::string replayLabel = std::format("REPLAY T{}", m_state.match.tick);
    const float replayWidth = 10.0F + 7.0F + 6.0F + static_cast<float>(FontRenderer::MeasurePixels(replayLabel)) + 10.0F;
    const float replayX = cursor - replayWidth;
    _shapes.StrokeRect(replayX, 13.0F, replayWidth, 22.0F, Ink::OUTLINE);
    // The one glyph the font does not have and does not need: the replay triangle is geometry, as
    // it is in the reference (README "Assets").
    _shapes.FillTriangle(replayX + 10.0F, 19.0F, replayX + 17.0F, 24.0F, replayX + 10.0F, 29.0F, Ink::TEXT_PRIMARY);
    _text.DrawText(static_cast<std::int32_t>(replayX + 23.0F), centered, replayLabel, Ink::TEXT_PRIMARY);
    // An isolated chip with the bar's own margin around it, so the HIT is the bar and the chip stays
    // 22 (ADR-100). The bar is exactly the floor tall, which is where the number came from.
    AddHit(replayX, 0.0F, replayWidth, Frame::TOP_BAR_HEIGHT, Action::OpenReplay, 0);
    cursor = replayX - 14.0F;

    _shapes.FillRect(cursor, 13.0F, 1.0F, 22.0F, Ink::CARD_BORDER);
    cursor -= 15.0F;
  }

  // **Only when the leader is somebody else** (ADR-056). "Public score, the leader is always
  // visible" is the anti-snowball, and it is about knowing who is ahead of you -- so when that is
  // you, the line says your own score back to you next to the chip that already says `1ST / 6`,
  // and `LDR YOU 0` is three words for a fact the bar states twice over.
  const bool someoneElseLeads = m_state.player.placement != 1 && !m_state.player.leader.name.empty();
  if (someoneElseLeads)
  {
    const std::string leaderLine = std::format("LDR {} {}", m_state.player.leader.name, FormatScore(m_state.player.leader.score));
    DrawRight(_text, cursor, centered, leaderLine, Ink::TEXT_MUTED);
    cursor -= static_cast<float>(FontRenderer::MeasurePixels(leaderLine)) + 8.0F;
  }

  // **The chip says whether the place MOVED, not only what it is** (ADR-091). Placement is the
  // anti-snowball's whole instrument -- the one-pager makes the score public so a player can tell
  // they are falling behind -- and a number that is the same ink at 1st and at 6th says only where
  // you are, never that you are sliding.
  //
  // Blue when you lead, amber when you have dropped since the last digest this client drew, and the
  // ordinary outline otherwise. `m_placementDrawn` is session memory: a client that joins mid-match
  // has no previous place, and the chip is simply not amber until it has drawn one.
  const bool leading = m_state.player.placement == 1;
  const bool slipped = m_placementDrawn != 0 && m_state.player.placement > m_placementDrawn;
  const Color chipInk = leading ? Ink::BLUE : (slipped ? Ink::AMBER : Ink::OUTLINE);
  const Color chipText = leading ? Ink::BLUE : (slipped ? Ink::AMBER : Ink::TEXT_PRIMARY);

  const std::string placement = std::format("{} / {}", FormatPlacement(m_state.player.placement), m_state.player.playerCount);
  const float chipWidth = static_cast<float>(FontRenderer::MeasurePixels(placement)) + 14.0F;
  _shapes.StrokeRect(cursor - chipWidth, 14.0F, chipWidth, 20.0F, chipInk);
  _text.DrawText(static_cast<std::int32_t>(cursor - chipWidth + 7.0F), centered, placement, chipText);
  cursor -= chipWidth + 8.0F;

  const std::string score = FormatScore(m_state.player.score);
  DrawRight(_text, cursor, centered, score, Ink::TEXT_PRIMARY);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(score)) + 8.0F;

  DrawRight(_text, cursor, centered, "SCORE", Ink::TEXT_MUTED);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels("SCORE")) + 14.0F;

  _shapes.FillRect(cursor, 13.0F, 1.0F, 22.0F, Ink::CARD_BORDER);
  cursor -= 15.0F;

  // The purse, beside the score and in the same weight (ADR-053). It is the number every build
  // on the screen is priced against, and it belongs where the eye already goes for the score
  // rather than inside a sentence on the production card.
  //
  // **And what this tick has already committed, right after it** (ADR-087). `46 CR` beside a sheet
  // refusing a 30 CR build is the contradiction a player actually hits: both numbers are right, and
  // the 20 between them was only ever visible in the locks rail on the other side of the screen.
  // `46 CR −20` carries the whole arithmetic in the place the bigger number is read.
  //
  // Drawn right to left like everything else on this bar, so the committed amount is composed first
  // and sits outermost -- it is the qualifier, and the purse is what it qualifies.
  const std::uint32_t committed = m_state.orders.QueuedBuildCost();
  if (committed > 0)
  {
    const std::string spent = std::format("−{}", committed);
    DrawRight(_text, cursor, centered, spent, Ink::BLUE);
    cursor -= static_cast<float>(FontRenderer::MeasurePixels(spent)) + 6.0F;
  }

  const std::string credits = std::format("{} CR", m_state.player.credits);
  DrawRight(_text, cursor, centered, credits, Ink::TEXT_PRIMARY);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(credits)) + 14.0F;

  _shapes.FillRect(cursor, 13.0F, 1.0F, 22.0F, Ink::CARD_BORDER);
  cursor -= 15.0F;

  // The countdown is the one thing on the bar in the 16px cut, and it is amber because amber is the
  // warning colour: this is the deadline every order on the rail is racing (README "Frame").
  const std::string countdown = m_state.match.finished ? std::string{"--:--:--"} : FormatCountdown(m_state.match.secondsToLock);
  const std::int32_t bigY = CenterTextY(0.0F, Frame::TOP_BAR_HEIGHT, Face::MonoDisplay);

  // **Amber is the deadline colour, and at zero there is no deadline left to warn about** (screen
  // 06). A countdown that stayed amber on 00:00:00 read as "hurry" to a player who could no longer
  // do anything, which is the opposite of what the number means once it has run out.
  const bool atLock = m_state.orders.locked && !m_state.match.finished;
  DrawRight(_text, cursor, bigY, countdown, atLock ? Ink::NEUTRAL_DIM : Ink::AMBER, Face::MonoDisplay);
  cursor -= static_cast<float>(FontRenderer::MeasurePixels(countdown, Face::MonoDisplay)) + 8.0F;

  const std::string lockLabel = m_state.match.finished ? std::string{"MATCH ENDED"}
                                : atLock               ? std::format("T{} LOCKED", m_state.OrdersTick())
                                                       : std::format("T{} LOCKS", m_state.OrdersTick());
  DrawRight(_text, cursor, centered, lockLabel, Ink::TEXT_MUTED);

  // ---- The left half, trimmed to what is left --------------------------------------------------
  const float titleWidth = static_cast<float>(FontRenderer::MeasurePixels("LOCKSTEP"));
  const float lineX = 16.0F + titleWidth + 10.0F;
  const float room = cursor - 14.0F - lineX;

  _text.DrawText(16, centered, "LOCKSTEP", Ink::TEXT_PRIMARY, Face::MonoMedium);

  // "DAY 12/21" rather than "DAY 12 / 21", and the countdown and replay labels use T-notation: at
  // 8px the reference's spelled-out bar is 63px wider than the frame (ADR-014).
  //
  // `M0419 - D12/21 - 12 PLAYERS - 61 SYSTEMS` (SCREENS.md 01). The census moved up here from the
  // map pane, where it was a caption on a picture; on the top bar it sits with the other facts
  // about the match that do not change from tick to tick.
  //
  // Dropped a clause at a time rather than clipped mid-word: every version below is a true and
  // readable line, and the end time goes before the census because a player who wants the end date
  // can read it off the day counter. The end time is also dropped rather than left dangling when
  // the state has none -- a match generated without a server has no schedule to report
  // (GeneratedMatch.h), and "- ENDS" followed by nothing reads as a truncation bug.
  const std::string census = std::format("{} PLAYERS · {} SYSTEMS", m_state.player.playerCount, m_state.totalSystems);
  // **No `M<id>`** (ADR-091). The snapshot carries no match id, so `SnapshotView` was filling it
  // with the zero-padded TICK -- a four-digit number beside `D3/21` and `T9 LOCKS` that names
  // neither the match nor the tick, and changes every tick while looking like an identifier.
  const std::string stem = std::format("D{}/{}", m_state.match.day, m_state.match.totalDays);

  std::vector<std::string> candidates;
  if (!m_state.match.endsAt.empty())
  {
    candidates.push_back(std::format("{} · {} · ENDS {}", stem, census, m_state.match.endsAt));
  }
  candidates.push_back(std::format("{} · {}", stem, census));
  candidates.push_back(stem);

  for (const std::string& candidate : candidates)
  {
    if (static_cast<float>(FontRenderer::MeasurePixels(candidate)) <= room || &candidate == &candidates.back())
    {
      _text.DrawText(static_cast<std::int32_t>(lineX), centered, candidate, Ink::TEXT_MUTED);

      // A disconnected client says so, in the one place a player is already looking. Everything
      // else on this screen is the last thing the server said, and without this there is no way to
      // tell that from the current thing the server is saying.
      if (!m_state.connected)
      {
        const float offlineX = lineX + static_cast<float>(FontRenderer::MeasurePixels(candidate)) + 12.0F;
        if (offlineX + static_cast<float>(FontRenderer::MeasurePixels("RECONNECTING")) < cursor - 14.0F)
        {
          _text.DrawText(static_cast<std::int32_t>(offlineX), centered, "RECONNECTING", Ink::RED);
        }
      }
      break;
    }
  }
}

} // namespace Lockstep
