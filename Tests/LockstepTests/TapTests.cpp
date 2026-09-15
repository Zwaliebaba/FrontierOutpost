// TapTests.cpp -- the screens around the main page's three panes, tapped: the connection dialog, the
// seats, the top bar, and the whole board with the link down.
//
// The harness is `Headless.h` and the rest of the main page is one suite per surface beside this one:
// `DigestTapTests`, `SignalSheetTapTests`, `PlaceSheetTapTests`, `RailTapTests`, `MapTapTests`.

#include "pch.h"
#include "CppUnitTest.h"

#include "Headless.h"

#include "ConnectionDialog.h"
#include "MainPage.h"
#include "SeatsPage.h"
#include "SnapshotView.h"

#include "BotPolicy.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

TEST_CLASS(ConnectionDialogTapTests)
{
public:
  TEST_METHOD(ARefusedTokenOffersAWayBackToTheField)
  {
    // ADR-038's EDIT TOKEN, which is the whole point of the refusal dialog: the commonest refusal
    // is a typo, and the screen behind this one has the field with the typo in it.
    Lockstep::ConnectionDialog::Facts facts;
    facts.reason = Neuron::RefusalReason::UnknownToken;
    facts.canGoBack = true;

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::EditToken ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Refused, facts, Lockstep::ConnectionDialog::Action::EditToken),
                   L"EDIT TOKEN cannot be pressed");
  }

  TEST_METHOD(ASeatInUseOffersARetry)
  {
    // The refusal that stops being true on its own: the seat frees when the other link drops.
    Lockstep::ConnectionDialog::Facts facts;
    facts.reason = Neuron::RefusalReason::AlreadyConnected;
    facts.canGoBack = true;

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Retry ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Refused, facts, Lockstep::ConnectionDialog::Action::Retry),
                   L"RETRY cannot be pressed");
  }

  TEST_METHOD(ALostConnectionOffersARetryAndAQuit)
  {
    Lockstep::ConnectionDialog::Facts facts;
    facts.lockCountdown = "00:14:09";

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Retry ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Lost, facts, Lockstep::ConnectionDialog::Action::Retry),
                   L"RETRY NOW cannot be pressed");
    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Quit ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Lost, facts, Lockstep::ConnectionDialog::Action::Quit),
                   L"QUIT cannot be pressed");
  }

  TEST_METHOD(AFinishedMatchOffersTheLastDigest)
  {
    Lockstep::ConnectionDialog::Facts facts;
    facts.standings = {Lockstep::ConnectionDialog::Facts::Standing{.text = "1ST  YOU        85", .isYou = true}};

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::ViewLastDigest ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Finished, facts, Lockstep::ConnectionDialog::Action::ViewLastDigest),
                   L"VIEW LAST DIGEST cannot be pressed");
  }

  TEST_METHOD(AConnectingDialogCanBeCanceled)
  {
    Lockstep::ConnectionDialog::Facts facts;
    facts.server = "127.0.0.1:7341";

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Cancel ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Connecting, facts, Lockstep::ConnectionDialog::Action::Cancel),
                   L"CANCEL cannot be pressed");
  }

  TEST_METHOD(WithNothingBehindItThereIsNoWayBackOnlyOut)
  {
    // `canGoBack` false is the match loop, where the join screen is a whole match ago. A BACK there
    // would be a button that returns to a screen this process left twenty minutes ago.
    Lockstep::ConnectionDialog::Facts facts;
    facts.reason = Neuron::RefusalReason::UnknownToken;
    facts.canGoBack = false;

    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Back !=
                     PressSomething(Lockstep::ConnectionDialog::Kind::Refused, facts, Lockstep::ConnectionDialog::Action::Back),
                   L"BACK was offered with nothing behind it");
    Assert::IsTrue(Lockstep::ConnectionDialog::Action::Quit ==
                     PressSomething(Lockstep::ConnectionDialog::Kind::Refused, facts, Lockstep::ConnectionDialog::Action::Quit),
                   L"QUIT cannot be pressed");
  }

  TEST_METHOD(AModalSwallowsEveryTapItIsOver)
  {
    // A tap reaching the board behind a REFUSED dialog would be an order edit against a connection
    // that is not there, and the player would have no way to tell which of their taps counted.
    Headless renderers;
    Lockstep::ConnectionDialog dialog;
    dialog.Update(Lockstep::ConnectionDialog::Kind::Refused, Lockstep::ConnectionDialog::Facts{}, 0.0);
    DrawDialog(dialog, renderers);

    Assert::IsTrue(dialog.Modal());

    // The four corners, which are as far from the card as this screen goes.
    Assert::IsTrue(dialog.HandleTap(1.0F, 1.0F), L"a tap in the corner fell through the scrim");
    Assert::IsTrue(dialog.HandleTap(static_cast<float>(SCREEN_WIDTH) - 1.0F, static_cast<float>(SCREEN_HEIGHT) - 1.0F));
    Assert::IsTrue(dialog.TakeAction() == Lockstep::ConnectionDialog::Action::None, L"the scrim pressed a button");
  }

  TEST_METHOD(ALostLinkIsABannerAndNotAModal)
  {
    // **The dialog told the player nothing they tapped would be sent, and then stopped them doing
    // the things that do not need sending** (ADR-085): reading the digest that arrived before the
    // drop, looking at the map, opening a sheet. A banner says the same sentence in 44 pixels.
    Headless renderers;
    Lockstep::ConnectionDialog dialog;
    dialog.Update(Lockstep::ConnectionDialog::Kind::Lost, Lockstep::ConnectionDialog::Facts{}, 0.0);
    DrawDialog(dialog, renderers);

    Assert::IsTrue(dialog.Visible(), L"a dropped link draws nothing at all");
    Assert::IsFalse(dialog.Modal(), L"a dropped link is still a modal");

    // The board below the band is reachable: the four corners of the map pane fall through.
    Assert::IsFalse(dialog.HandleTap(700.0F, 400.0F), L"the banner swallowed a tap on the map");
    Assert::IsFalse(dialog.HandleTap(1.0F, static_cast<float>(SCREEN_HEIGHT) - 1.0F), L"the banner swallowed a tap on the digest");
    Assert::IsTrue(dialog.TakeAction() == Lockstep::ConnectionDialog::Action::None);
  }

  TEST_METHOD(TheBannerStillOffersRetryAndQuit)
  {
    Assert::IsTrue(PressSomething(Lockstep::ConnectionDialog::Kind::Lost, Lockstep::ConnectionDialog::Facts{},
                                  Lockstep::ConnectionDialog::Action::Retry) == Lockstep::ConnectionDialog::Action::Retry,
                   L"the banner offers no way to retry now");
    Assert::IsTrue(PressSomething(Lockstep::ConnectionDialog::Kind::Lost, Lockstep::ConnectionDialog::Facts{},
                                  Lockstep::ConnectionDialog::Action::Quit) == Lockstep::ConnectionDialog::Action::Quit,
                   L"the banner offers no way out");
  }

  TEST_METHOD(AHiddenDialogSwallowsNothing)
  {
    // The other half: when there is nothing wrong the dialog must be completely out of the way.
    Headless renderers;
    Lockstep::ConnectionDialog dialog;
    dialog.Update(Lockstep::ConnectionDialog::Kind::None, Lockstep::ConnectionDialog::Facts{}, 0.0);
    DrawDialog(dialog, renderers);

    Assert::IsFalse(dialog.Visible());
    Assert::IsFalse(dialog.HandleTap(640.0F, 360.0F), L"an invisible dialog swallowed a tap");
  }
};

TEST_CLASS(SeatsPageTapTests)
{
public:
  TEST_METHOD(ASeatCanBeHandedToABot)
  {
    // ADR-037's BOT toggle, drawn and refused until that ADR and never pressed since.
    Lockstep::SeatsPage page{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};

    Headless renderers;
    const bool filled =
      SweepFor(page, renderers, DrawSeats, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
               [&page]
               {
                 const std::vector<std::optional<Lockstep::BotPolicy>> roster = page.Roster();
                 return std::ranges::any_of(roster, [](const std::optional<Lockstep::BotPolicy>& _seat) { return _seat.has_value(); });
               });
    Assert::IsTrue(filled, L"no seat on the seats screen can be given to a bot");
  }

  TEST_METHOD(TheHostsOwnSeatCannotBeGivenAway)
  {
    // The host is the process that owns the match. A bot in their seat would leave it with nothing
    // to draw. Seat one is the host's by default, and the whole sweep must not manage to flip it.
    Lockstep::SeatsPage page{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};

    Headless renderers;
    (void)SweepFor(page, renderers, DrawSeats, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, [] { return false; });

    // Every control on the screen, pressed, in every order the sweep happens to find. The host's
    // own seat has to come out of that still theirs.
    const std::vector<std::optional<Lockstep::BotPolicy>> roster = page.Roster();
    Assert::IsTrue(page.HostSeat() >= 0 && page.HostSeat() < static_cast<std::int32_t>(roster.size()));
    Assert::IsFalse(roster[static_cast<std::size_t>(page.HostSeat())].has_value(), L"the host gave their own seat to a bot");
  }

  TEST_METHOD(TheTokenRowStillWorksWhereTakeSeatUsedTo)
  {
    // `NEW TOKEN` shared a row with `TAKE SEAT`. Removing one of a pair of half-width buttons is
    // exactly the edit that leaves the other one drawn and unhittable, and nothing would say so.
    Lockstep::SeatsPage page{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};

    Headless renderers;

    // Any seat's token, not seat one's: the sweep selects a card before it reaches the panel, and
    // NEW TOKEN reissues whichever seat is selected.
    const std::vector<std::string> before = page.PlayingTokens();
    const bool reissued =
      SweepFor(page, renderers, DrawSeats, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, [&page, &before] { return page.PlayingTokens() != before; });
    Assert::IsTrue(reissued, L"no control on the seats screen reissues a token");
  }

  TEST_METHOD(AWaitingSeatCanBeMarkedWithoutBecomingABot)
  {
    // ADR-066's middle segment. `BOT AT T1` is the answer between "wait for them" and "play it
    // yourself", and the claim it has to make is that the seat is READY while still a person's --
    // which is what makes ENTER MATCH reachable when a friend does not show, without handing their
    // empire to a machine before they have had the chance to arrive.
    Lockstep::SeatsPage page{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};
    page.SetConnected(std::vector<bool>{true, false, false, false, false, false});

    Headless renderers;
    const bool marked = SweepFor(page, renderers, DrawSeats, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page]
                                 {
                                   for (std::int32_t seat = 0; seat < Lockstep::SeatsPage::SEAT_COUNT; ++seat)
                                   {
                                     if (page.BotTakesOverSeat(seat) && !page.Roster()[static_cast<std::size_t>(seat)].has_value())
                                     {
                                       return true;
                                     }
                                   }
                                   return false;
                                 });
    Assert::IsTrue(marked, L"no control marks a waiting seat for a bot without making it one now");
  }

  TEST_METHOD(EveryoneIsHereOnceTheSeatsAreBots)
  {
    // The rule that makes ENTER MATCH reachable when a friend does not show: a bot seat is never
    // waiting for anybody.
    Lockstep::SeatsPage page{Lockstep::GenerateSeatTokens(Lockstep::SeatsPage::SEAT_COUNT)};
    page.SetConnected(std::vector<bool>{true, false, false, false, false, false});
    Assert::IsFalse(page.EveryoneIsHere(), L"five empty human seats counted as ready");

    Headless renderers;
    (void)SweepFor(page, renderers, DrawSeats, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, [&page] { return page.EveryoneIsHere(); });
    Assert::IsTrue(page.EveryoneIsHere(), L"a screen full of bots is still waiting for somebody");
  }
};

// The top bar says what it knows and hides what is not finished (ADR-091).
TEST_CLASS(TopBarTests)
{
public:
  TEST_METHOD(ReplayIsOffTheBarUnlessDeveloperControlsAreOn)
  {
    // Its sheet is a stub, and a control whose own title said `NOT YET WIRED` teaches a player that
    // the buttons on this screen may do nothing -- which is what ADR-053 and ADR-077 were spent
    // unteaching.
    const auto simulation = PlayedMatch(2);
    Lockstep::MainPage shipped;
    shipped.Create(ViewOfSeatZero(*simulation));

    Headless renderers;
    const bool found = SweepFor(shipped, renderers, DrawPage, 0, 0, SCREEN_WIDTH, TOP_BAR,
                                [&shipped] { return shipped.OpenPanel() == Lockstep::MainPage::Panel::Replay; });
    Assert::IsFalse(found, L"a shipped build put REPLAY on the bar");

    Lockstep::MainPage dev;
    dev.Create(ViewOfSeatZero(*simulation));
    dev.SetDeveloperControls(true);
    const bool reachable = SweepFor(dev, renderers, DrawPage, 0, 0, SCREEN_WIDTH, TOP_BAR,
                                    [&dev] { return dev.OpenPanel() == Lockstep::MainPage::Panel::Replay; });
    Assert::IsTrue(reachable, L"--dev did not put REPLAY back");
  }

  TEST_METHOD(TheCensusNamesNoMatchId)
  {
    // `M0007` was the zero-padded TICK, which names neither the match nor the tick and changes every
    // tick while looking like an identifier.
    const auto simulation = PlayedMatch(7);
    const Lockstep::MatchState state = ViewOfSeatZero(*simulation);

    // The id the view model carries is still the tick; what changed is that the bar stops printing
    // it. Asserted through the state rather than the pixels, because the draw is a capture.
    Assert::AreEqual(state.match.tick, static_cast<std::uint32_t>(std::stoul(state.match.id)),
                     L"the match id stopped being the tick, so the bar may be able to show one after all");
  }
};

// The top bar carries the purse AND what this tick has committed of it (ADR-087).
TEST_CLASS(CommittedPurseTests)
{
public:
  TEST_METHOD(NothingQueuedCommitsNothing)
  {
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    Assert::AreEqual(0u, page.State().orders.QueuedBuildCost(), L"a fresh match has something queued already");
  }

  TEST_METHOD(TheCommittedAmountIsWhatTheSheetPricesAgainst)
  {
    // One number, read two ways: the bar subtracts it and the sheet says so in a sentence. They are
    // the same call, so they cannot disagree (ADR-078, ADR-087).
    const auto simulation = PlayedMatch(0);
    Lockstep::MatchState state = ViewOfSeatZero(*simulation);
    Assert::IsFalse(state.orders.builds.empty());

    state.player.credits = 46;
    state.orders.builds.front().cost = 20;
    state.orders.queuedBuilds.push_back(0);

    Lockstep::MainPage page;
    page.Create(std::move(state));

    Assert::AreEqual(20u, page.State().orders.QueuedBuildCost(), L"the bar would subtract the wrong number");
    Assert::IsTrue(page.PurseSentence().find("26") != std::string::npos, L"and the sheet would not agree with it");
  }
};

TEST_CLASS(OfflineBoardTapTests)
{
public:
  TEST_METHOD(NoTapGivesAnOrderWhileTheLinkIsDown)
  {
    // The same claim `ALockedRailQueuesNothing` makes about the lock, about the other reason an
    // order cannot go: a client that cannot send must not let one be composed either, or the player
    // is editing a list that will never leave.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    page.SetOffline(true);
    Assert::IsFalse(page.OrdersEditable(), L"a page with no link still says orders can be given");

    Headless renderers;
    const bool ordered =
      SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
               [&page]
               {
                 const Lockstep::OrderSet orders = Lockstep::OrdersOf(page.State());
                 return !orders.builds.empty() || !orders.fleetOrders.empty() || !orders.proposals.empty() || orders.concede;
               });
    Assert::IsFalse(ordered, L"a tap composed an order on a client that cannot send one");
  }

  TEST_METHOD(TheBoardIsStillReadableWhileTheLinkIsDown)
  {
    // The half the modal took away. Focusing, opening a sheet and reading the digest reach no
    // socket, so none of them is a thing to stop.
    const auto simulation = PlayedMatch(4);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));
    page.SetOffline(true);

    Headless renderers;
    const bool opened = SweepFor(page, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                 [&page] { return page.OpenPanel() != Lockstep::MainPage::Panel::None; });
    Assert::IsTrue(opened, L"a dropped link left nothing on the board to open");

    Lockstep::MainPage focusing;
    focusing.Create(ViewOfSeatZero(*simulation));
    focusing.SetOffline(true);
    const bool focused = SweepFor(focusing, renderers, DrawPage, 0, TOP_BAR, SCREEN_WIDTH, SCREEN_HEIGHT,
                                  [&focusing] { return focusing.FocusedSystem() != Lockstep::EventRefs::NONE; });
    Assert::IsTrue(focused, L"a dropped link left nothing on the board to focus");
  }

  TEST_METHOD(ComingBackRestoresTheControls)
  {
    // It is a state and not a one-way door: the reconnect loop under the banner is expected to win.
    const auto simulation = PlayedMatch(0);
    Lockstep::MainPage page;
    page.Create(ViewOfSeatZero(*simulation));

    page.SetOffline(true);
    Assert::IsFalse(page.OrdersEditable());
    page.SetOffline(false);
    Assert::IsTrue(page.OrdersEditable(), L"the controls did not come back with the link");
  }
};

} // namespace LockstepTests
