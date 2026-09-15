// MainPageDigest.cpp -- the digest rail: cards laid out and then drawn, each with its buttons.
//
// **The digest is the order surface** (ADR-034): every event carries what can be done about it, and
// since ADR-112 each of those buttons is a link to the place the order is given at. The column pages
// by cards rather than scrolling (ADR-080), and it fades to 55% while the map is taking a move
// (ADR-114) -- every ink here goes through `Faded`.

#include "pch.h"
#include "MainPage.h"

#include "Controls.h"
#include "DesignTokens.h"
#include "DigestView.h"
#include "MainPageParts.h"

#include <algorithm>
#include <utility>

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

/// A name in the rail's voice. Labels and headers are uppercase (DESIGN-GUIDELINES "Font"), and
/// the font has no case of its own to fall back on.
[[nodiscard]] Color EventColor(EventKind _kind) noexcept
{
  switch (_kind)
  {
  case EventKind::Contact:
    return Ink::AMBER;
  case EventKind::Proposal:
    return Ink::BLUE;
  case EventKind::Loss:
    return Ink::RED;
  case EventKind::Region:
    return Ink::PURPLE;
  case EventKind::Custodian:
  case EventKind::Economy:
  case EventKind::Ignored:
  default:
    return {214, 220, 228, 89};
  }
}

} // namespace

MainPage::CardLayout MainPage::LayoutCard(const DigestCard& _card, std::uint32_t _widthPixels) const
{
  CardLayout layout;

  // **An actor card is collapsed unless it is the open one** (ADR-061). It is the only card whose
  // lines are a LIST -- one per event the rival produced -- so it is the only one whose body can be
  // dropped without dropping a fact the card is the only record of: the title still names the
  // rival, the stamp still counts them, and opening it is one tap away.
  layout.collapsible = _card.actor != NOBODY;
  const bool collapsed = layout.collapsible && _card.actor != m_expandedActor;

  if (!collapsed)
  {
    for (const std::string& detail : _card.lines)
    {
      for (std::string& line : FontRenderer::WrapToWidth(detail, _widthPixels))
      {
        layout.details.push_back(std::move(line));
      }
    }
  }

  layout.hasVerdict = !_card.verdict.empty();
  if (layout.hasVerdict)
  {
    // The verdict box is inset two characters from the card's text column. It was two COLUMNS
    // until 2026-09-13; two advances is the same inset while the font is fixed-pitch, and the
    // one that still means "two characters" when it is not (ADR-073).
    layout.verdictDetail = FontRenderer::WrapToWidth(_card.verdictDetail, _widthPixels - 2U * FontRenderer::AdvancePixels());
  }
  layout.hasActions = !_card.actions.empty();

  // The same arithmetic the draw below walks, in one expression: the title block, a line per detail,
  // the verdict box, the action row, and the gap to the next card's divider.
  const auto lines = static_cast<float>(LINE_HEIGHT);
  layout.height = 11.0F + static_cast<float>(TITLE_LINE_HEIGHT) + 2.0F + static_cast<float>(layout.details.size()) * lines + 4.0F;
  if (layout.hasVerdict)
  {
    layout.height += 4.0F + (1.0F + static_cast<float>(layout.verdictDetail.size())) * lines + 6.0F;
  }
  // **The action row is a BUTTON tall plus its two gaps** (ADR-100, ADR-111). It reserved one
  // `LINE_HEIGHT` while a button was 18 and centred on that line's baseline, which was near enough
  // to true to go unnoticed; at 44 the button reached a whole line above its row and painted over
  // the detail line there. A row that reserves less than it draws is the defect `LINE_HEIGHT` was
  // introduced for, one control further on. The gaps are reserved rather than borrowed from the
  // line above, because they are what the grown 44-pixel target reaches into.
  if (layout.hasActions)
  {
    layout.height += BUTTON_GAP + BUTTON_HEIGHT + BUTTON_GAP;
  }
  return layout;
}

/// One button's whole ink at the share the digest wears while the map is taking a move (ADR-114).
///
/// **A button's ink is a bundle rather than one colour**, so it is faded a field at a time on its
/// way to `DrawButton`. Every band, rule and label on this column went through `Faded` and the
/// buttons did not, which left the column deaf and looking live.
MainPage::ControlInk MainPage::FadedInk(ControlInk _ink) const
{
  _ink.border = Faded(_ink.border);
  _ink.fill = Faded(_ink.fill);
  _ink.label = Faded(_ink.label);
  _ink.number = Faded(_ink.number);
  _ink.segmentRule = Faded(_ink.segmentRule);
  _ink.segmentFill = Faded(_ink.segmentFill);
  return _ink;
}

/// The column's ground, its header line and the delta under it; returns where the cards start.
///
/// A returning player reads `SINCE YOU LOOKED` and how much of the match happened without them;
/// everyone else reads the tick and what is on the column (ADR-094).
float MainPage::DrawDigestHeader(ShapeRenderer& _shapes, FontRenderer& _text)
{
  // **Every ink on this column goes through `Faded`** (ADR-114): while the map is taking a move the
  // digest stays readable at 55% and records no hit, so the one filled control on the screen is the
  // confirm strip's `SEND` (ADR-089). A button's ink is a bundle rather than one colour, so it is
  // faded a field at a time on its way to `DrawButton`.

  // **The digest is the order surface** (ADR-034, SCREENS.md 01). Every event carries what can be
  // done about it, because the thing a player wants to do is always about something that happened,
  // and a menu somewhere else is a second place to look.
  _shapes.FillRect(0.0F, Frame::TOP_BAR_HEIGHT, Frame::DIGEST_WIDTH, Frame::SCREEN_HEIGHT - Frame::TOP_BAR_HEIGHT, Ink::APP_BACKGROUND);
  _shapes.FillRect(Frame::DIGEST_WIDTH - 1.0F, Frame::TOP_BAR_HEIGHT, 1.0F, Frame::SCREEN_HEIGHT - Frame::TOP_BAR_HEIGHT,
                   Faded(Ink::CARD_BORDER));

  const std::int32_t headerY = static_cast<std::int32_t>(Frame::TOP_BAR_HEIGHT) + 12;
  const bool returning = m_state.unreadTicks >= 2;

  if (returning)
  {
    // `SINCE YOU LOOKED - T43 > T46` and a chip. It is the first line a returning player reads and
    // it says how much of the match happened without them.
    _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), headerY,
                   std::format("SINCE YOU LOOKED · T{} → T{}", m_state.lastSeenTick, m_state.match.tick), Faded(Ink::TEXT_MUTED));

    const std::string chip = std::format("{} TICKS", m_state.unreadTicks);
    const float chipWidth = static_cast<float>(FontRenderer::MeasurePixels(chip)) + 14.0F;
    _shapes.StrokeRect(Frame::DIGEST_WIDTH - RAIL_PADDING - chipWidth, BandTopForText(headerY, 18.0F), chipWidth, 18.0F, Faded(Ink::AMBER));
    _text.DrawText(static_cast<std::int32_t>(Frame::DIGEST_WIDTH - RAIL_PADDING - chipWidth + 7.0F), headerY, chip, Faded(Ink::AMBER));
  }
  else
  {
    _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), headerY, std::format("DIGEST - TICK {}", m_state.match.tick),
                   Faded(Ink::TEXT_MUTED), Face::MonoDisplay);

    // At the lock the right-hand figure stops being a count of what is here and becomes the tick
    // that is being resolved. It is the only thing on this column that changes at zero, and it is
    // what says the digest below is about to be replaced rather than simply short.
    const bool pending = m_state.orders.locked && !m_state.match.finished;
    DrawRight(_text, Frame::DIGEST_WIDTH - RAIL_PADDING, headerY,
              pending ? std::format("T{} PENDING", m_state.OrdersTick()) : std::format("{} EVENTS", m_state.digest.size()),
              pending ? Faded(Ink::AMBER) : Faded(Ink::TEXT_MUTED));
  }

  float y = Frame::TOP_BAR_HEIGHT + 28.0F;

  // ---- The delta -----------------------------------------------------------------------------------
  const DigestDelta delta = DeltaOf(m_state);
  if (delta.Any())
  {
    // Two cells to a row, rounded up. The division is integer ON PURPOSE -- three cells is two rows
    // -- and it is done before the conversion rather than inside it, because a `/` under a
    // `static_cast<float>` reads like a float division somebody got wrong.
    const std::size_t rows = (delta.cells.size() + 1) / 2;
    const float boxHeight = static_cast<float>(rows) * static_cast<float>(LINE_HEIGHT) + 12.0F;
    _shapes.StrokeRect(RAIL_PADDING, y, Frame::DIGEST_WIDTH - 2.0F * RAIL_PADDING, boxHeight, Faded(Ink::AMBER));

    // Two columns, because four short facts in one line wrap badly at 8px and four stacked lines
    // are a list rather than a summary.
    for (std::size_t index = 0; index < delta.cells.size(); ++index)
    {
      const float cellX = RAIL_PADDING + 8.0F + static_cast<float>(index % 2) * (Frame::DIGEST_WIDTH - 2.0F * RAIL_PADDING) * 0.5F;
      const std::int32_t cellY = static_cast<std::int32_t>(y) + 6 + static_cast<std::int32_t>(index / 2) * LINE_HEIGHT;
      // **A loss is red, and what marks one is a true minus** -- three bytes of UTF-8, not the
      // ASCII hyphen this compared against until 2026-09-13. `front() == '-'` went on compiling
      // when the character changed and silently stopped finding a single negative, which is the
      // failure mode a byte comparison against text has.
      _text.DrawText(static_cast<std::int32_t>(cellX), cellY, delta.cells[index].text,
                     delta.cells[index].loss ? Faded(Ink::RED) : Faded(Ink::AMBER));
    }
    y += boxHeight + 6.0F;

    // **What the backlog could not carry** (ADR-094). The server keeps the last `DIGEST_HISTORY`
    // ticks per player and sends the whole of it on arrival (ADR-044); a player who was away longer
    // gets that window and no warning that anything fell off the front of it. The delta box's
    // counts are honest about the ticks it HAS, which is exactly what makes the gap invisible.
    if (m_state.unreadTicks > DIGEST_HISTORY_TICKS)
    {
      _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), static_cast<std::int32_t>(y), "Older ticks were not kept.",
                     Faded(Ink::NEUTRAL_DIM), Face::SansRegular);
      y += static_cast<float>(LINE_HEIGHT) + 6.0F;
    }
  }

  return y;
}

/// One card: its rule, its dot, its title in the display cut, its detail, its verdict box and its
/// actions. `_yPixels` ends where the next card begins.
void MainPage::DrawDigestCard(ShapeRenderer& _shapes, FontRenderer& _text, const DigestCard& _card, const CardLayout& _layout,
                              float& _yPixels)
{
  const Color accent = Faded(EventColor(_card.kind));
  const float top = _yPixels;

  // Where this card's hits begin. The card as a whole is tappable -- reading and focusing are the
  // same gesture -- but its buttons sit inside it, and `HandleTap` reads the list BACKWARDS so
  // that the thing drawn last wins. A card-wide region appended after the buttons therefore
  // swallows every one of them, which is exactly what happened: tapping BUILD focused the event
  // instead, and the only reason it looked like it worked is that any handled tap sends the
  // order set. The card's region is inserted here instead, in front of its own buttons.
  const std::size_t cardHitsBegin = m_hits.size();

  _shapes.FillRect(0.0F, _yPixels, Frame::DIGEST_WIDTH - 1.0F, 1.0F, Faded(Ink::DIVIDER));
  std::int32_t lineY = static_cast<std::int32_t>(_yPixels) + 11;

  _shapes.FillEllipse(RAIL_PADDING + 4.0F, static_cast<float>(lineY) + 4.0F, 4.0F, 4.0F, accent);
  // The display cut (ADR-084): a card's title is what the card IS, and until there were two sizes
  // it was separated from the sentences under it by a weight step nobody could see at a glance.
  //
  // **And NOT shouted** (ADR-099). `Uppercased()` was here, over a detail line reading *7 of 10
  // lost (defending)*: a shout and a sentence about the same event. The titles are the longest
  // strings on the screen and uppercase is the least legible case for a long string, which the
  // display cut's 16px made louder rather than clearer. They are authored in sentence case in
  // `GameLogic` -- `Battle at Ulme`, `Shipyard L1 rising at Dothan` -- and this is the one place
  // that was shouting them.
  //
  // **The face is still MONO, and that is a constraint rather than a decision** (ADR-102): the
  // display cut is baked from Plex Mono only (`Font.h`, ADR-073), so a sentence-cased title at
  // this size has nowhere sans to go. ADR-074's rule is bent here and the ADR says where.
  _text.DrawText(static_cast<std::int32_t>(CARD_TEXT_LEFT), lineY, _card.title, Faded(Ink::TEXT_PRIMARY), Face::MonoDisplay);
  if (!_card.stamp.empty())
  {
    DrawRight(_text, Frame::DIGEST_WIDTH - RAIL_PADDING, lineY, _card.stamp, Faded(Ink::TEXT_MUTED));
  }

  // The title of an actor card opens and closes it. Registered here rather than after the card's
  // own region, so that it wins: the region is INSERTED at `cardHitsBegin` below, which puts
  // everything added during the card in front of it in the reverse walk `HandleTap` makes.
  if (_layout.collapsible)
  {
    AddHitUnlessMoving(0.0F, top, Frame::DIGEST_WIDTH - 1.0F, TOUCH_FLOOR, Action::ToggleActorCard, _card.actor);
  }
  lineY += TITLE_LINE_HEIGHT + 2;

  for (const std::string& line : _layout.details)
  {
    _text.DrawText(static_cast<std::int32_t>(CARD_TEXT_LEFT), lineY, line, Faded(Ink::TEXT_DETAIL), Face::SansRegular);
    lineY += LINE_HEIGHT;
  }

  // ---- The verdict box ---------------------------------------------------------------------------
  //
  // Always a verdict and never a bare `A v B` (DESIGN-GUIDELINES "Copy"), and the second line
  // always says whose ships remain -- which is why the snapshot carries both sides now.
  if (_layout.hasVerdict)
  {
    lineY += 4;
    const std::vector<std::string>& detail = _layout.verdictDetail;
    const float boxTop = static_cast<float>(lineY) - VERDICT_BOX_PADDING;
    const float boxHeight = static_cast<float>(1 + detail.size()) * static_cast<float>(LINE_HEIGHT) + 2.0F * VERDICT_BOX_PADDING;
    _shapes.StrokeRect(CARD_TEXT_LEFT, boxTop, Frame::DIGEST_WIDTH - CARD_TEXT_LEFT - RAIL_PADDING, boxHeight, Faded(Ink::AMBER));

    _text.DrawText(static_cast<std::int32_t>(CARD_TEXT_LEFT) + 6, lineY, _card.verdict, Faded(Ink::AMBER));
    lineY += LINE_HEIGHT;
    for (const std::string& line : detail)
    {
      _text.DrawText(static_cast<std::int32_t>(CARD_TEXT_LEFT) + 6, lineY, line, Faded(Ink::TEXT_DETAIL), Face::SansRegular);
      lineY += LINE_HEIGHT;
    }
    lineY += 6;
  }

  // ---- The actions -------------------------------------------------------------------------------
  if (_layout.hasActions)
  {
    DrawDigestActions(_shapes, _text, _card, lineY);
  }

  _yPixels = static_cast<float>(lineY) + 4.0F;

  // Only when there is something to focus. The card a tick-zero digest shows is synthetic -- it
  // reports that nothing has happened and carries the opening moves -- so it leads no event, and
  // a `FocusEvent` for event number -1 is an out-of-bounds read that took the whole client down.
  if (_card.leadEvent != EventRefs::NONE && !m_moveMode.has_value())
  {
    m_hits.insert(m_hits.begin() + static_cast<std::ptrdiff_t>(cardHitsBegin),
                  HitRegion{0.0F, top, Frame::DIGEST_WIDTH - 1.0F, _yPixels - top, Action::FocusEvent, _card.leadEvent});
  }
}

/// One card's buttons, left to right, dropping the first that does not fit the column (ADR-053).
///
/// **A button that does not fit is DROPPED rather than wrapped**, which is why the number is its own
/// cell: a state change that changed a label's width would change which buttons a card shows.
void MainPage::DrawDigestActions(ShapeRenderer& _shapes, FontRenderer& _text, const DigestCard& _card, std::int32_t& _lineYPixels)
{
  // **A gap above and below, and the hit fills both** (ADR-111). A button is 28 and a target is
  // 44, so the eight pixels either side are what the grown rectangle reaches into -- which is
  // why they are reserved here rather than left as whatever the line before happened to leave.
  _lineYPixels += static_cast<std::int32_t>(BUTTON_GAP);
  float buttonX = CARD_TEXT_LEFT;

  const auto buttonY = static_cast<float>(_lineYPixels);

  for (const EventAction& action : _card.actions)
  {
    // `committed` is "this is already in the orders this tick goes in with", which two kinds of
    // control can be and the rest cannot. It is the same state a queued tile and a queued rail
    // row wear, and it is still a target, because every order is editable until the lock.
    //
    // A build has two more states and the NUMBER SEGMENT says which (ADR-053, ADR-111): queued,
    // so the next tap is known to take it back; or beyond the purse, dashed and dim with what is
    // missing, because the lock would refuse it and a refusal a tick later is the worst way to
    // learn a price.
    Button button{.label = action.label, .number = action.number};
    bool committed = false;
    bool unaffordable = false;
    if (action.kind == EventActionKind::QueueBuild)
    {
      committed = std::ranges::find(m_state.orders.queuedBuilds, action.target) != m_state.orders.queuedBuilds.end();
      unaffordable = !committed && !m_state.CanAffordBuild(action.target);
      if (committed && action.target >= 0 && action.target < static_cast<std::int32_t>(m_state.orders.builds.size()))
      {
        button.number = std::format("−{}", m_state.orders.builds[static_cast<std::size_t>(action.target)].cost);
      }
      else if (unaffordable)
      {
        button.number = std::format("NEED {} MORE", BuildShortfall(action.target));
        button.moneyReason = true;
      }
    }

    // An answered offer says which way it was answered, in the past tense against the other
    // button's imperative, and the pair stays tappable so that changing an answer is one tap
    // (ADR-068).
    if (action.kind == EventActionKind::AcceptProposal || action.kind == EventActionKind::DeclineProposal)
    {
      const auto answered = std::ranges::find_if(m_state.orders.answers,
                                                 [&action](const ProposalAnswer& _answer) { return _answer.proposal == action.target; });
      const bool thisWay =
        answered != m_state.orders.answers.end() && answered->accepted == (action.kind == EventActionKind::AcceptProposal);
      if (thisWay)
      {
        button.label = answered->accepted ? "ACCEPTED" : "DECLINED";
        committed = true;
      }
    }

    // **A focus chip is never inert and never locked** (ADR-081): it takes the eye somewhere and
    // reaches no wire, so it goes on working while every order on the screen is frozen.
    const bool focusOnly = action.kind == EventActionKind::Focus;
    button.state = ControlState::Outlined;
    if (unaffordable)
    {
      button.state = ControlState::Inert;
    }
    else if (committed)
    {
      button.state = ControlState::Committed;
    }
    else if (!OrdersEditable() && !focusOnly)
    {
      button.state = ControlState::Locked;
    }
    else if (action.primary && OrdersEditable() && !m_moveMode.has_value())
    {
      button.state = ControlState::Primary;
    }

    // **Wide enough for a finger as well as for its label** (ADR-100, ADR-111). The box is what
    // the two cells need; the target is grown around it, because a row of buttons with gaps
    // between them is the isolated-chip case rather than the column one.
    float width = ButtonWidth(button);
    if (buttonX + width > Frame::DIGEST_WIDTH - RAIL_PADDING)
    {
      break;
    }

    const bool hovered = button.state != ControlState::Inert && button.state != ControlState::Locked && !m_moveMode.has_value() &&
                         m_pointerXPixels >= buttonX && m_pointerXPixels < buttonX + width && m_pointerYPixels >= buttonY &&
                         m_pointerYPixels < buttonY + BUTTON_HEIGHT;

    // **A committed control says what the next tap does while the finger is on it** (ADR-111),
    // so taking an order back is never a surprise. The width is remeasured, because `TAKE BACK`
    // is not the width of the label it replaces -- and it is clamped to the resting width, so a
    // button under the pointer never pushes the one beside it along.
    if (hovered && button.state == ControlState::Committed && !focusOnly)
    {
      Button flipped = button;
      flipped.label = "TAKE BACK";
      if (action.kind == EventActionKind::QueueBuild && action.target >= 0 &&
          action.target < static_cast<std::int32_t>(m_state.orders.builds.size()))
      {
        flipped.number = std::format("+{}", m_state.orders.builds[static_cast<std::size_t>(action.target)].cost);
      }
      if (ButtonWidth(flipped) <= width)
      {
        button = flipped;
      }
    }

    DrawButton(_shapes, _text, buttonX, buttonY, width, button,
               FadedInk(ControlInkFor(button.state, ControlKind::Button, hovered, button.moneyReason)));

    if ((OrdersEditable() && !unaffordable) || focusOnly)
    {
      const Frame::Box target = Frame::GrownToFloor(buttonX, buttonY, width, BUTTON_HEIGHT);
      const DigestTarget destination = TargetOf(action);
      AddHitUnlessMoving(target.x, target.y, target.width, target.height, destination.action, destination.index);
      m_hoverRegions.push_back(HoverRegion{buttonX, buttonY, width, BUTTON_HEIGHT});
    }
    buttonX += width + BUTTON_GAP;
  }
  _lineYPixels += static_cast<std::int32_t>(BUTTON_HEIGHT + BUTTON_GAP);
}

/// The band at the foot of the column: what is above, and what the worst of what is below is.
void MainPage::DrawDigestPageBand(ShapeRenderer& _shapes, FontRenderer& _text, const std::vector<DigestCard>& _cards,
                                  const std::vector<CardLayout>& _layouts, std::size_t _lastCard, float _roomPixels, bool _paged)
{
  // ---- The page band -------------------------------------------------------------------------------
  //
  // At the foot of the column, where the stack it is about ends. **It says what is hidden and what
  // the worst of it is** (ADR-080): `1 / 4 - MORE >` told a player how much column was left and
  // nothing at all about whether the battle they had not seen was in it. `< PREV` appears only once
  // there is something above, so the band never offers a direction that does nothing.
  if (_paged)
  {
    const float bandY = Frame::SCREEN_HEIGHT - DIGEST_PAGE_HEIGHT;
    const std::int32_t bandText = CenterTextY(bandY, DIGEST_PAGE_HEIGHT);
    _shapes.FillRect(0.0F, bandY, Frame::DIGEST_WIDTH - 1.0F, 1.0F, Faded(Ink::DIVIDER));

    if (m_digestTop > 0)
    {
      _text.DrawText(static_cast<std::int32_t>(RAIL_PADDING), bandText, "‹ PREV", Faded(Ink::TEXT_MUTED));
      AddHitUnlessMoving(0.0F, bandY, Frame::DIGEST_WIDTH * 0.5F, DIGEST_PAGE_HEIGHT, Action::ShowDigestPage,
                         static_cast<std::int32_t>(PreviousDigestTop(_layouts, _roomPixels)));
    }

    const std::string hidden = HiddenSummary(_cards, _lastCard);
    DrawRight(_text, Frame::DIGEST_WIDTH - RAIL_PADDING, bandText, hidden.empty() ? std::string{"END"} : hidden + " ›",
              hidden.empty() ? Faded(Ink::NEUTRAL_DIM) : Faded(Ink::TEXT_MUTED));
    if (!hidden.empty())
    {
      AddHitUnlessMoving(Frame::DIGEST_WIDTH * 0.5F, bandY, Frame::DIGEST_WIDTH * 0.5F, DIGEST_PAGE_HEIGHT, Action::ShowDigestPage,
                         static_cast<std::int32_t>(_lastCard));
    }
  }
}

/// Every event, ranked and grouped, each carrying what can be done about it (ADR-034).
///
/// **This is the order surface**: the thing a player wants to do is always about something that
/// happened, so the controls are on the card rather than in a menu somewhere else. The column pages
/// by whole cards (ADR-080), and while the map is taking a move every ink on it goes through
/// `FadedInk` and it records no hit at all (ADR-114).
void MainPage::DrawDigestRail(ShapeRenderer& _shapes, FontRenderer& _text)
{
  float y = DrawDigestHeader(_shapes, _text);

  // ---- The cards, and which of them are on the screen -------------------------------------------------
  //
  // **The column scrolls, by whole cards** (ADR-080, which took the digest out of ADR-052 option C).
  // The whole stack is measured first, because a card is the unit that scrolls and the only way to
  // know where one ends is to have worked out how tall it is -- which is also what paging needed
  // (ADR-061), so the measuring loop is unchanged and only what is done with it moved.
  const std::vector<DigestCard> cards = CardsOf(m_state);
  std::vector<CardLayout> layouts;
  layouts.reserve(cards.size());
  float stackHeight = 0.0F;
  for (const DigestCard& card : cards)
  {
    layouts.push_back(LayoutCard(card, CARD_TEXT_WIDTH));
    stackHeight += layouts.back().height;
  }

  const float cardsTop = y;
  const bool paged = stackHeight > Frame::SCREEN_HEIGHT - cardsTop;
  const float room = Frame::SCREEN_HEIGHT - cardsTop - (paged ? DIGEST_PAGE_HEIGHT : 0.0F);

  // **Clamped so the last card is always reachable and never alone past the end.** A scroll
  // position is a card index and the player can push it anywhere; what stops it running off is
  // that a top with nothing under it is not a position, it is an empty column.
  const std::size_t lastTop = cards.empty() ? 0 : cards.size() - 1;
  m_digestTop = std::min(m_digestTop, lastTop);

  // What fits from here. One card always goes in even when it is taller than the column: a card
  // that fits nowhere is still better read cut off than not drawn at all.
  const std::size_t firstCard = m_digestTop;
  std::size_t lastCard = firstCard;
  float used = 0.0F;
  while (lastCard < layouts.size() && (lastCard == firstCard || used + layouts[lastCard].height <= room))
  {
    used += layouts[lastCard].height;
    ++lastCard;
  }
  m_cardsOnScreen = lastCard - firstCard;
  for (std::size_t cardIndex = firstCard; cardIndex < lastCard; ++cardIndex)
  {
    DrawDigestCard(_shapes, _text, cards[cardIndex], layouts[cardIndex], y);
  }

  DrawDigestPageBand(_shapes, _text, cards, layouts, lastCard, room, paged);
}

std::size_t MainPage::PreviousDigestTop(const std::vector<CardLayout>& _layouts, float _room) const
{
  // A screenful backwards, measured the way a screenful forwards is measured: cards are different
  // heights, so "one page" is however many of them fit and not a fixed number (ADR-061, ADR-080).
  std::size_t top = m_digestTop;
  float used = 0.0F;
  while (top > 0)
  {
    const float height = _layouts[top - 1].height;
    if (used > 0.0F && used + height > _room)
    {
      break;
    }
    used += height;
    --top;
  }
  return top;
}

MainPage::DigestTarget MainPage::TargetOf(const EventAction& _action) const noexcept
{
  switch (_action.kind)
  {
  case EventActionKind::RedirectFleet:
  {
    // **No digest control places an order any more; each of them opens the place the order is
    // about** (ADR-112). A move is given on the map from the sheet for the system the fleet is
    // standing on, so the index this screen needs is that SYSTEM and not the fleet the digest
    // named -- which is the whole reason a digest button's action and its index are chosen
    // together (ADR-057).
    const bool known = _action.target >= 0 && _action.target < static_cast<std::int32_t>(m_state.fleets.size());
    return DigestTarget{Action::OpenSystem, known ? m_state.fleets[static_cast<std::size_t>(_action.target)].from : EventRefs::NONE};
  }
  case EventActionKind::QueueBuild:
  {
    const bool known = _action.target >= 0 && _action.target < static_cast<std::int32_t>(m_state.orders.builds.size());
    return DigestTarget{Action::OpenSystem,
                        known ? PositionOfSystem(m_state, m_state.orders.builds[static_cast<std::size_t>(_action.target)].system)
                              : EventRefs::NONE};
  }
  case EventActionKind::AcceptProposal:
    return DigestTarget{Action::AcceptProposal, _action.target};
  case EventActionKind::DeclineProposal:
    return DigestTarget{Action::DeclineProposal, _action.target};
  case EventActionKind::Focus:
  default:
    // A focus chip carries the system it points at, not the event it sits on.
    return DigestTarget{Action::FocusSystem, _action.target};
  }
}

bool MainPage::ScrollDigest(std::int32_t _cards)
{
  if (_cards == 0)
  {
    return false;
  }
  const std::size_t was = m_digestTop;
  const std::int64_t wanted = static_cast<std::int64_t>(m_digestTop) + _cards;

  // Clamped at both ends here rather than only in the draw, so a wheel spun hard against the end of
  // the stack does not bank a hundred notches that have to be spun back.
  m_digestTop = wanted <= 0 ? 0 : static_cast<std::size_t>(wanted);
  return m_digestTop != was;
}

} // namespace Lockstep
