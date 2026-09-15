// MainPageInput.cpp -- the taps, drags, notches and keys, and the hit list they are answered from.
//
// **A hit is recorded beside the `FillRect` that drew the thing** (ADR-041), so every pane records
// its own targets and this unit only reads them back: a tap walks the list newest first, a drag or
// a notch asks the pane under the pointer, a key is the two the screen answers. Nothing here knows
// where anything is.

#include "pch.h"
#include "MainPage.h"

#include <algorithm>

namespace Lockstep
{

namespace
{

/// What a tap does, which the rows composed in here have to name (ADR-113). The page's own enum,
/// aliased rather than qualified thirty times.
using Action = MainPage::Action;

} // namespace

bool MainPage::HandleDrag(const Neuron::PointerInput::Drag& _drag)
{
  // The gesture belongs to whatever was under the PRESS. A drag that began on the map keeps
  // rotating it as the finger crosses onto a rail, and a drag that began on a rail never starts.
  const float mapLeft = Frame::DIGEST_WIDTH;
  const float mapRight = Frame::SCREEN_WIDTH - Frame::ORDERS_WIDTH;

  // **A drag that began on the digest scrolls it**, which is the finger's half of ADR-080: a touch
  // device has no wheel, and the page band alone made reading a long digest a sequence of taps.
  //
  // The column moves in whole cards and a finger moves in pixels, so the remainder is banked rather
  // than thrown away -- without that, a slow drag scrolls nothing at all. `DIGEST_SCROLL_PIXELS` is
  // the frame's row unit (ADR-052): the distance a finger already associates with one row of
  // anything on this screen.
  const bool startedOnDigest = _drag.originXPixels >= 0.0F && _drag.originXPixels < mapLeft && _drag.originYPixels >= Frame::TOP_BAR_HEIGHT;
  if (startedOnDigest)
  {
    constexpr float DIGEST_SCROLL_PIXELS = SHEET_ROW_HEIGHT;
    m_digestDragPixels += _drag.deltaYPixels;

    std::int32_t cards = 0;
    while (m_digestDragPixels <= -DIGEST_SCROLL_PIXELS)
    {
      m_digestDragPixels += DIGEST_SCROLL_PIXELS;
      ++cards;
    }
    while (m_digestDragPixels >= DIGEST_SCROLL_PIXELS)
    {
      m_digestDragPixels -= DIGEST_SCROLL_PIXELS;
      --cards;
    }
    (void)ScrollDigest(cards);
    return true;
  }

  const bool startedOnMap =
    _drag.originXPixels >= mapLeft && _drag.originXPixels < mapRight && _drag.originYPixels >= Frame::TOP_BAR_HEIGHT;
  if (!startedOnMap)
  {
    return false;
  }

  // **A drag that began on an open place sheet scrolls it rather than orbiting the map** (ADR-112).
  // The body moves in whole blocks and a finger moves in pixels, so the remainder is banked exactly
  // as the digest's is -- and this is the half of the gesture that matters, because a wheel is not
  // a finger and this game is for touch (ADR-098, ADR-101).
  if (m_panel == Panel::Place && m_sheetBlocks > m_sheetBlocksShown)
  {
    m_sheetDragPixels += _drag.deltaYPixels;

    std::int32_t blocks = 0;
    while (m_sheetDragPixels <= -SHEET_ROW_HEIGHT)
    {
      m_sheetDragPixels += SHEET_ROW_HEIGHT;
      ++blocks;
    }
    while (m_sheetDragPixels >= SHEET_ROW_HEIGHT)
    {
      m_sheetDragPixels -= SHEET_ROW_HEIGHT;
      --blocks;
    }
    (void)ScrollSheet(blocks);
    return true;
  }

  // Both axes now. Horizontal orbits the camera around the galaxy, vertical raises and lowers it
  // -- which is the difference the turntable could not express and the reason it did not feel
  // like a camera (ADR-017).
  m_mapView.Drag(_drag.deltaXPixels, _drag.deltaYPixels);
  return true;
}

bool MainPage::HandleZoom(std::int32_t _steps, float _xPixels, float _yPixels)
{
  if (_steps == 0)
  {
    return false;
  }

  // The pane under the pointer decides what a notch means: a list over the digest (ADR-080), a
  // camera over the map (ADR-090). One banked count, two meanings, and the pointer is what picks.
  const bool aboveTheBar = _yPixels >= Frame::TOP_BAR_HEIGHT;
  if (aboveTheBar && _xPixels >= 0.0F && _xPixels < Frame::DIGEST_WIDTH)
  {
    // A notch away from the player scrolls DOWN the column. `TakeZoomSteps` counts a notch away as
    // negative -- it was named for a camera, where away is out -- so the sign is flipped here, at
    // the one place that knows the gesture means a list rather than a distance.
    m_digestDragPixels = 0.0F;
    return ScrollDigest(-_steps);
  }

  // Over the map it means what it was banked for, sign and all: away from the player is out --
  // unless a place sheet is under the pointer, which is a list and takes the list's meaning
  // (ADR-112). The sheet is drawn over the pane, so the pane's own gesture cannot also be the
  // sheet's: a notch that zoomed the map out from under an open sheet is a notch spent on
  // something the player cannot see.
  if (aboveTheBar && _xPixels >= Frame::DIGEST_WIDTH && _xPixels < Frame::SCREEN_WIDTH - Frame::ORDERS_WIDTH)
  {
    if (m_panel == Panel::Place)
    {
      m_sheetDragPixels = 0.0F;
      return ScrollSheet(-_steps);
    }
    return m_mapView.Zoom(_steps);
  }

  // The locks rail, which had to learn this the moment its rows became 44 pixels tall (ADR-101).
  // A notch moves it by a row, so the gesture means the same thing it means on the column opposite.
  if (aboveTheBar && _xPixels >= Frame::SCREEN_WIDTH - Frame::ORDERS_WIDTH)
  {
    return ScrollRail(static_cast<float>(-_steps) * TOUCH_FLOOR);
  }

  return false;
}

bool MainPage::HandleKey(Neuron::KeyboardInput::Key _key)
{
  // **`ESC` is the way out of the move mode, and the banner says so** (ADR-114). It is the second
  // thing a key does on this screen and the first that is not about scrolling: a mode is the one
  // state here a player can be stuck in, and every platform's answer to that is this key.
  if (_key == Neuron::KeyboardInput::Key::Escape && m_moveMode.has_value())
  {
    ExitMove();
    return true;
  }

  // A screenful, measured from what the last frame actually drew rather than from a number chosen
  // here: cards are different heights and a page is however many of them fit (ADR-080).
  const auto page = static_cast<std::int32_t>(std::max<std::size_t>(m_cardsOnScreen, 1));
  if (_key == Neuron::KeyboardInput::Key::PageDown)
  {
    return ScrollDigest(page);
  }
  if (_key == Neuron::KeyboardInput::Key::PageUp)
  {
    return ScrollDigest(-page);
  }
  return false;
}

void MainPage::AddHit(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, Action _action, std::int32_t _index)
{
  m_hits.push_back(HitRegion{_xPixels, _yPixels, _widthPixels, _heightPixels, _action, _index});
}

void MainPage::AddHitUnlessMoving(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, Action _action,
                                  std::int32_t _index)
{
  // **The digest is read and not touched while the map is taking a move** (ADR-114). It fades to
  // just over half its ink and records nothing, so the one filled control on the screen is the
  // strip's `SEND` (ADR-089) and a tap meant for the map cannot land on a card behind it.
  if (m_moveMode.has_value())
  {
    return;
  }
  AddHit(_xPixels, _yPixels, _widthPixels, _heightPixels, _action, _index);
}

std::int32_t MainPage::RegionUnderPointer() const noexcept
{
  for (std::size_t index = 0; index < m_hoverRegions.size(); ++index)
  {
    const HoverRegion& region = m_hoverRegions[index];
    const bool inside = m_pointerXPixels >= region.x && m_pointerXPixels < region.x + region.width && m_pointerYPixels >= region.y &&
                        m_pointerYPixels < region.y + region.height;
    if (inside)
    {
      return static_cast<std::int32_t>(index);
    }
  }
  return EventRefs::NONE;
}

bool MainPage::SetPointer(float _xPixels, float _yPixels)
{
  m_pointerXPixels = _xPixels;
  m_pointerYPixels = _yPixels;

  // Tested against the PREVIOUS frame's rectangles, exactly as a tap is: layout and hit testing are
  // the same code, so there is only one list and it is a frame old.
  const std::int32_t under = RegionUnderPointer();
  if (under == m_hoveredRegion)
  {
    return false;
  }
  m_hoveredRegion = under;
  return true;
}

bool MainPage::HandleTap(float _xPixels, float _yPixels)
{
  // Reverse order, so the panel drawn last is hit first. Painter's order and hit order are the
  // same list read the two ways round, which is what stops a modal from being clickable-through.
  for (auto region = m_hits.rbegin(); region != m_hits.rend(); ++region)
  {
    const bool inside =
      _xPixels >= region->x && _xPixels < region->x + region->width && _yPixels >= region->y && _yPixels < region->y + region->height;
    if (!inside)
    {
      continue;
    }

    const bool editable = OrdersEditable();
    switch (region->action)
    {
    case Action::FocusEvent:
    {
      // Tapping a digest event focuses what it is about. Reading and acting are the same gesture:
      // the event says a rival is at Kepler-Reach, and the tap puts Kepler-Reach under your eye.
      //
      // Bounds-checked, because the index came from a card and a card can be synthetic. This read
      // was unguarded and a -1 crashed the client; the region that produced it is gone now, and
      // this stays so the next one cannot.
      if (region->index < 0 || region->index >= static_cast<std::int32_t>(m_state.digest.size()))
      {
        return true;
      }
      const DigestEvent& event = m_state.digest[static_cast<std::size_t>(region->index)];
      m_focusedSystem = event.refs.system;
      m_panel = Panel::None;
      return true;
    }

    case Action::FocusSystem:
    {
      // Bounds-checked against the SYSTEMS, which is the array this index names.
      if (region->index < 0 || region->index >= static_cast<std::int32_t>(m_state.graph.systems.size()))
      {
        return true;
      }
      m_focusedSystem = region->index;
      m_panel = Panel::None;
      return true;
    }
    case Action::OpenSystem:
    case Action::OpenFleetsAt:
    {
      // **A place sheet opens on a system you HOLD, and on nothing else** (ADR-058, ADR-112). You
      // cannot build on somebody else's ground -- `Match::Validate` refuses it as `NotYourSystem` --
      // and you have nothing standing on it, so a sheet over a rival's capital is a list of orders
      // that system cannot take.
      //
      // A rival's system still focuses, because the tap has to do something visible: a control
      // that silently ignores you is the defect this screen has already been bitten by twice.
      //
      // **The disc and the badge beside it land here together** (ADR-079). They are two targets
      // because they name two things -- the system, and the ships standing on it -- and the sheet
      // holds both, so the disc opens it.
      m_focusedSystem = region->index;
      m_armedConcede = EventRefs::NONE;

      const bool yours = region->index >= 0 && region->index < static_cast<std::int32_t>(m_state.graph.systems.size()) &&
                         m_state.graph.systems[static_cast<std::size_t>(region->index)].owner == m_state.viewer;

      // **The badge skips the sheet when there is one fleet under it** (ADR-114). A badge totals
      // SHIPS, so a place holding one of your fleets has exactly one thing a tap on it could mean,
      // and a sheet between the finger and the map would be a tap spent on a question with one
      // answer (ADR-079's own argument, one door further along). Several fleets is a real question
      // and the sheet is where it is asked.
      if (region->action == Action::OpenFleetsAt && yours && editable)
      {
        const std::vector<std::int32_t> here = FleetsAtPlace(region->index);
        if (here.size() == 1)
        {
          EnterMove(here.front());
          return true;
        }
      }

      OpenPlace(yours ? region->index : EventRefs::NONE);
      return true;
    }

    case Action::BeginMove:
      // The guard behind every control that leads here is `EnterMove`, by the rule the lock would
      // refuse the order by (ADR-053, ADR-077): one place rather than four that agree.
      EnterMove(region->index);
      return true;

    case Action::SendMove:
    {
      if (!editable || !m_moveMode.has_value() || m_moveMode->selected == EventRefs::NONE)
      {
        return true;
      }
      // **`from` is where the fleet stands, whether or not a move has been ordered from here
      // already.** The mode only opens on a fleet that is not under way (ADR-077), and ordering one
      // sets `from` to where it is leaving -- so re-entering the mode offers the same lanes and
      // sending again replaces the order rather than adding a second hop to it, which is what makes
      // a move editable until the lock like every other order (ADR-031).
      Fleet& fleet = m_state.fleets[static_cast<std::size_t>(m_moveMode->fleet)];
      const std::int32_t origin = fleet.from;
      fleet.to = m_moveMode->selected;
      fleet.order = FleetStance::Move;
      fleet.progress = 0.0F;
      fleet.eta = m_state.OrdersTick() + TicksTo(origin, fleet.to) - 1;
      fleet.status = std::format("ordered - ETA T{}", fleet.eta);

      // **The place sheet does not come back.** The player asked one question and it is answered;
      // reopening the sheet they came from would be the screen insisting on the last thing they
      // looked at rather than the board they just changed (ADR-114).
      ExitMove();
      return true;
    }

    case Action::CancelMove:
      ExitMove();
      return true;

    case Action::ToggleBuild:
    {
      if (!editable)
      {
        return true;
      }
      auto& queued = m_state.orders.queuedBuilds;
      const auto found = std::find(queued.begin(), queued.end(), region->index);
      if (found == queued.end())
      {
        // Refused here, by the rules the lock would refuse it by (ADR-053, ADR-069). The tiles and
        // buttons that lead here already say so and are not targets, so this is the guard behind
        // them rather than the message -- and it is the only place `queuedBuilds` grows, which is
        // what makes it the guard rather than one of several.
        //
        // `available` is the whole of "the lock would start this": the rising row and every row
        // beside it on a system that is building are all rows a queue would be refused (ADR-107).
        const bool startable = region->index >= 0 && region->index < static_cast<std::int32_t>(m_state.orders.builds.size()) &&
                               m_state.orders.builds[static_cast<std::size_t>(region->index)].available;
        if (!startable || !m_state.CanAffordBuild(region->index))
        {
          return true;
        }
        queued.push_back(region->index);
      }
      else
      {
        queued.erase(found);
      }
      return true;
    }

    case Action::OpenSignals:
      m_panel = Panel::SignalList;
      m_panelSubject = 0;
      m_panelSubjectId = EventRefs::NONE;
      m_armedConcede = EventRefs::NONE;
      return true;

    case Action::ToggleSignal:
    {
      if (!editable || region->index < 0 || region->index >= static_cast<std::int32_t>(m_state.orders.signals.size()))
      {
        return true;
      }

      auto& queued = m_state.orders.queuedSignals;
      const auto found = std::find(queued.begin(), queued.end(), region->index);
      if (found != queued.end())
      {
        // Taking one back, including a concede that was queued a moment ago. It has not resolved,
        // so it is still an edit like any other.
        queued.erase(found);
        m_armedConcede = EventRefs::NONE;
        return true;
      }

      // Conceding takes two taps on the same row. The first arms it and the row says so; anything
      // else disarms it.
      if (m_state.orders.signals[static_cast<std::size_t>(region->index)].kind == SignalKind::Concede && m_armedConcede != region->index)
      {
        m_armedConcede = region->index;
        return true;
      }

      queued.push_back(region->index);
      m_armedConcede = EventRefs::NONE;
      return true;
    }

    case Action::AcceptProposal:
    case Action::DeclineProposal:
      // Answering a proposal IS an order. It is recorded here and committed at the lock with the
      // fleet moves and the builds, which is what makes agreement move at the same speed as
      // betrayal (one-pager, decision 3).
      if (editable)
      {
        // One answer per offer (ADR-068). Answering the same one again replaces its answer rather
        // than sending two, which is what makes changing an answer before the lock one tap.
        const bool accepted = region->action == Action::AcceptProposal;
        const auto existing = std::find_if(m_state.orders.answers.begin(), m_state.orders.answers.end(),
                                           [region](const ProposalAnswer& _answer) { return _answer.proposal == region->index; });
        if (existing != m_state.orders.answers.end())
        {
          existing->accepted = accepted;
        }
        else
        {
          m_state.orders.answers.push_back(ProposalAnswer{.proposal = region->index, .accepted = accepted});
        }
      }
      return true;

    case Action::ChooseDestination:
      // **A selection and not an order** (ADR-114). The map lights the system, the strip's header
      // says what stands there, and the filled `SEND` is the one thing that commits -- so a slipped
      // finger on the map costs a second tap rather than a tick.
      if (editable && m_moveMode.has_value())
      {
        m_moveMode->selected = region->index;
      }
      return true;

    case Action::CancelFleetOrder:
    {
      // **The other half of taking an order back, and it is a different array from a build's**
      // (ADR-057, ADR-112). Putting `to` back to `from` is the whole of it: a fleet whose two ends
      // agree is standing, which is what `OnALane` reads and what every draw site tests.
      if (!editable || region->index < 0 || region->index >= static_cast<std::int32_t>(m_state.fleets.size()))
      {
        return true;
      }
      Fleet& fleet = m_state.fleets[static_cast<std::size_t>(region->index)];
      if (fleet.underWay)
      {
        return true;
      }
      fleet.to = fleet.from;
      fleet.order = FleetStance::Hold;
      fleet.progress = 0.0F;
      fleet.eta = 0;
      fleet.status.clear();
      return true;
    }

    case Action::ToggleActorCard:
      // One open at a time, so tapping a second card's title closes the first. The column has room
      // for one card's worth of lines and paging two open cards apart is not reading them.
      m_expandedActor = m_expandedActor == region->index ? NOBODY : region->index;
      return true;

    case Action::ShowDigestPage:
      m_digestTop = static_cast<std::size_t>(std::max(0, region->index));
      m_digestDragPixels = 0.0F;
      return true;

    case Action::OpenReplay:
      m_panel = Panel::Replay;
      m_panelSubject = static_cast<std::int32_t>(m_state.match.tick);
      m_panelSubjectId = EventRefs::NONE;
      return true;

    case Action::PageRail:
      // A bandful less one row, so the row a player was reading is still on the screen after the
      // tap. The digest pages the same way for the same reason (ADR-080).
      return ScrollRail(static_cast<float>(region->index) * std::max(TOUCH_FLOOR, m_railViewportPixels - TOUCH_FLOOR));

    case Action::ResetCamera:
      m_mapView.ResetView();
      return true;

    case Action::ClosePanel:
      m_panel = Panel::None;
      return true;

    case Action::None:
      // The sheet's own background. It does nothing and it is handled, which is the whole of what
      // makes the sheet a modal (ADR-112).
      return true;

    default:
      break;
    }
  }

  // **A tap that hit nothing leaves the move mode** (ADR-114), which is what the empty map, an
  // unreachable system and a rival's garrison all are while the mode is on: none of them is a
  // target, so all three arrive here and mean the same thing.
  if (m_moveMode.has_value())
  {
    ExitMove();
    return true;
  }

  return false;
}

} // namespace Lockstep
