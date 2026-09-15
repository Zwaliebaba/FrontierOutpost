// MainPage.cpp -- the ops console: what the page is, and what it keeps between frames.
//
// **One class, one translation unit per pane** (AGENTS.md R7). The page draws itself every frame
// and keeps almost no state, so its seams are the panes it draws: the top bar, the map, the digest,
// the locks rail, the sheet and the move mode are each a `MainPage<Pane>.cpp`; the taps, drags,
// notches and keys are `MainPageInput.cpp`; what every one of them shares is `MainPageParts.h`, and
// the control vocabulary they draw with is `Controls.h` (ADR-111). This unit holds the rest: creation,
// the sentences the panes borrow, the clock, and the frame's dispatch.
//
// Design/UI/SCREENS.md 01 is the spec and every number here comes from it or from
// DESIGN-GUIDELINES.md; the ADRs cited beside each rule are where the spec was settled.

#include "pch.h"
#include "MainPage.h"

#include "DesignTokens.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace Lockstep
{

namespace
{

using Neuron::FontRenderer;
using Neuron::ShapeRenderer;

} // namespace

std::string MainPage::FormatPlacement(std::uint32_t _placement)
{
  const std::uint32_t lastTwo = _placement % 100;
  const char* suffix = "TH";
  if (lastTwo < 11 || lastTwo > 13)
  {
    switch (_placement % 10)
    {
    case 1:
      suffix = "ST";
      break;
    case 2:
      suffix = "ND";
      break;
    case 3:
      suffix = "RD";
      break;
    default:
      break;
    }
  }
  return std::to_string(_placement) + suffix;
}

void MainPage::Create(MatchState _state)
{
  // What the player had open, so that a state arriving does not shut it (ADR-065). A sheet is where
  // somebody is in the middle of deciding something, and the tick landing under them is not a
  // reason to take it away -- it is the reason they opened it.
  const Panel wasOpen = m_panel;
  const std::int32_t wasSubject = m_panelSubject;
  const std::int32_t wasSubjectId = m_panelSubjectId;
  const std::int32_t wasMoving = m_moveMode.has_value() ? m_moveMode->fleetId : EventRefs::NONE;

  // Where this player stood on the digest being replaced, so the chip can say a place was lost
  // (ADR-091). Zero on the first state, which is no placement and so never a slip.
  m_placementDrawn = m_state.player.placement;

  m_state = std::move(_state);
  m_panel = Panel::None;
  m_panelSubject = EventRefs::NONE;
  m_panelSubjectId = EventRefs::NONE;
  m_sheetScroll = 0;
  m_sheetDragPixels = 0.0F;
  m_moveMode.reset();
  m_focusedSystem = EventRefs::NONE;

  // The arming is an index into the signal list, and the list is recomposed with the state. Kept,
  // it would be a row armed that nobody armed.
  m_armedConcede = EventRefs::NONE;

  // A digest is replaced wholesale every tick, so nothing about how the last one was being READ
  // survives it: page three is nowhere in the new one, and the rival whose card was open may have
  // no card at all (ADR-061).
  m_digestTop = 0;
  m_cardsOnScreen = 1;
  m_digestDragPixels = 0.0F;
  m_expandedActor = NOBODY;

  MeasureContent();
  ReopenPanel(wasOpen, wasSubjectId, wasSubject);
  ReopenMove(wasMoving);
}

std::string MainPage::PurseSentence() const
{
  const std::uint32_t spent = m_state.orders.QueuedBuildCost();
  if (spent == 0)
  {
    return {};
  }
  const std::uint32_t left = spent <= m_state.player.credits ? m_state.player.credits - spent : 0;
  return std::format("Priced against the {} credits left after the {} already queued, not the {} in hand.", left, spent,
                     m_state.player.credits);
}

std::string MainPage::RisingSentence(std::string_view _systemName, std::uint32_t _ticksIn, std::uint32_t _ticks)
{
  if (_ticks == 0)
  {
    return {};
  }

  // Words below ten and digits from ten up, which is the ordinary rule for prose and the reason
  // this is not `std::to_string` (DESIGN-GUIDELINES "Copy": a sentence is a sentence). A level takes
  // one to four ticks today (ADR-069), so the table is the whole of the range and then some.
  const auto spelled = [](std::uint32_t _count)
  {
    constexpr std::array<const char*, 10> WORDS = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine"};
    return _count < WORDS.size() ? std::string{WORDS[_count]} : std::to_string(_count);
  };

  const std::uint32_t in = _ticksIn > _ticks ? _ticks : _ticksIn;
  std::string counted = spelled(in);
  counted[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(counted[0])));
  return std::format("{} cannot take another order until this lands. {} of {} ticks {} in.", _systemName, counted, spelled(_ticks),
                     in == 1 ? "is" : "are");
}

std::string MainPage::LockSentence() const
{
  return std::format("Resolving T{}. Controls return with the new digest. Anything you tap now is an order for T{}.", m_state.OrdersTick(),
                     m_state.OrdersTick() + 1);
}

std::string MainPage::FormatCountdown(double _seconds)
{
  // **Rounded UP, and that is the whole of it.** Truncating showed `00:00:00` for the entire last
  // second, while the rail still said UNLOCKED and still took edits -- so the screen said the
  // deadline had passed and then went on accepting orders, which is precisely the confusion screen
  // 06 exists to remove. With a ceiling, `00:00:01` means there is still a second of it and
  // `00:00:00` means there is not, so the clock reading zero and the rail reading LOCKED are the
  // same event.
  const auto total = static_cast<std::int64_t>(std::ceil(std::max(0.0, _seconds)));
  const std::int64_t hours = total / 3600;
  const std::int64_t minutes = (total % 3600) / 60;
  const std::int64_t remainder = total % 60;
  return std::format("{:02}:{:02}:{:02}", hours, minutes, remainder);
}

void MainPage::Update(double _elapsedSeconds)
{
  // Before the lock check, and deliberately: a fleet crossing a lane is crossing it while the tick
  // resolves, and a route that froze the moment the orders locked would say the opposite
  // (ADR-055).
  //
  // **`--still` holds it at zero** (ADR-114). Everything that moves on this screen is a pure
  // function of this number, so a capture taken with it stopped is always the same picture -- which
  // is what a ring that breathes and a lane whose dashes march would otherwise have taken away.
  if (!m_still)
  {
    m_animationSeconds += static_cast<float>(_elapsedSeconds);
  }

  if (m_state.orders.locked)
  {
    return;
  }

  m_state.match.secondsToLock -= _elapsedSeconds;
  if (m_state.match.secondsToLock <= 0.0)
  {
    // The lock. Everything the player has edited goes together -- fleet moves, builds and the
    // answer to the proposal -- and nothing on the rail is editable afterwards (one-pager: orders
    // are hidden until they lock). What happens NEXT is the server's: it resolves the tick and
    // sends a new digest. The client does not resolve anything, so the countdown simply stops.
    //
    // **An open sheet stays open** (ADR-065). It goes inert like everything else, and it says so in
    // its own header; closing it would take the board away from somebody mid-decision at the one
    // moment they can do nothing about it.
    m_state.match.secondsToLock = 0.0;
    m_state.orders.locked = true;
  }
}

bool MainPage::Animating() const noexcept
{
  // **Nothing moves on its own while the clock is held** (ADR-114): `--still` is for a capture, and
  // a capture of a page that asks for a frame every frame is a capture that never settles.
  if (m_still)
  {
    return false;
  }
  // The move mode's ring breathes and its lanes march, which is the second thing on this screen
  // that moves without anybody touching it (ADR-055 was the first).
  return m_moveMode.has_value() || std::ranges::any_of(m_state.fleets, [](const Fleet& _fleet)
                                                       { return _fleet.order == FleetStance::Move && _fleet.from != _fleet.to; });
}

std::vector<std::int32_t> MainPage::FleetsAtPlace(std::int32_t _system) const
{
  std::vector<std::int32_t> here;
  for (std::size_t index = 0; index < m_state.fleets.size(); ++index)
  {
    const Fleet& fleet = m_state.fleets[index];
    // Theirs, from here, and orderable. `underWay` is the one the badge does not have to ask -- a
    // fleet the server already has on a lane is not at either end of it -- and this does, because a
    // tap lands a frame after the badge was drawn and the lock can fall between them (ADR-077).
    if (fleet.owner == m_state.viewer && !fleet.underWay && fleet.from == _system)
    {
      here.push_back(static_cast<std::int32_t>(index));
    }
  }
  return here;
}

std::uint32_t MainPage::BuildShortfall(std::int32_t _index) const noexcept
{
  if (_index < 0 || _index >= static_cast<std::int32_t>(m_state.orders.builds.size()))
  {
    return 0;
  }
  const std::uint32_t needed = m_state.orders.QueuedBuildCost() + m_state.orders.builds[static_cast<std::size_t>(_index)].cost;
  return needed > m_state.player.credits ? needed - m_state.player.credits : 0;
}

void MainPage::DrawInterface(ShapeRenderer& _shapes, FontRenderer& _text)
{
  DrawTopBar(_shapes, _text);
  DrawDigestRail(_shapes, _text);
  DrawLocksRail(_shapes, _text);
  DrawPanel(_shapes, _text);
  DrawMoveMode(_shapes, _text);
}

} // namespace Lockstep
