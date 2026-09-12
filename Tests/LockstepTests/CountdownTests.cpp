// CountdownTests.cpp -- the clock in the top bar, and the lock it is counting down to.
//
// `FormatCountdown` is six lines and it had a bug in it for the whole life of the screen: it
// truncated, so `00:00:00` was on the top bar for the entire last second while the rail still said
// UNLOCKED and still took edits. The screen announced that the deadline had passed and then went on
// accepting orders, which is precisely the confusion screen 06 exists to remove (ADR-039).
//
// It was found by photographing the running client. That is a poor way to find an arithmetic bug in
// a pure function, and this file is the answer to it.

#include "pch.h"
#include "CppUnitTest.h"

#include "MainPage.h"

#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

TEST_CLASS(CountdownTests)
{
public:
  TEST_METHOD(ZeroOnTheClockMeansNoTimeLeft)
  {
    // **The regression.** Anything above zero has to read as at least one second, because the rail
    // is still editable for all of it. `00:00:00` and LOCKED are meant to be the same event.
    Assert::AreEqual(std::string{"00:00:00"}, Lockstep::MainPage::FormatCountdown(0.0));
    Assert::AreEqual(std::string{"00:00:01"}, Lockstep::MainPage::FormatCountdown(0.001), L"a fraction of a second read as none");
    Assert::AreEqual(std::string{"00:00:01"}, Lockstep::MainPage::FormatCountdown(0.4), L"most of a second read as none");
    Assert::AreEqual(std::string{"00:00:01"}, Lockstep::MainPage::FormatCountdown(0.999));
    Assert::AreEqual(std::string{"00:00:01"}, Lockstep::MainPage::FormatCountdown(1.0));
    Assert::AreEqual(std::string{"00:00:02"}, Lockstep::MainPage::FormatCountdown(1.2));
  }

  TEST_METHOD(TheFieldsCarryAndThePaddingHolds)
  {
    Assert::AreEqual(std::string{"00:01:00"}, Lockstep::MainPage::FormatCountdown(60.0));
    Assert::AreEqual(std::string{"00:59:59"}, Lockstep::MainPage::FormatCountdown(3599.0));
    Assert::AreEqual(std::string{"01:00:00"}, Lockstep::MainPage::FormatCountdown(3600.0));
    Assert::AreEqual(std::string{"02:14:09"}, Lockstep::MainPage::FormatCountdown(8049.0), L"the reference sheet's own countdown");

    // Six hours is the authored tick interval, so this is the longest thing the bar ever shows.
    Assert::AreEqual(std::string{"06:00:00"}, Lockstep::MainPage::FormatCountdown(21600.0));
  }

  TEST_METHOD(APastDeadlineIsZeroRatherThanNegative)
  {
    // The countdown is driven by a server-sent number and decremented locally by frame time, so it
    // can go past zero between a lock and the state that follows it. A negative here would format
    // as an hour count with a minus in it on a bar that has no room for one.
    Assert::AreEqual(std::string{"00:00:00"}, Lockstep::MainPage::FormatCountdown(-0.5));
    Assert::AreEqual(std::string{"00:00:00"}, Lockstep::MainPage::FormatCountdown(-9000.0));
  }
};

TEST_CLASS(LockTests)
{
public:
  TEST_METHOD(TheOrdersLockWhenTheCountdownRunsOut)
  {
    // Screen 06's whole mechanism, and the client's entire share of the tick discipline: the
    // server decides what resolves, and this decides when the player stops being able to change
    // what it resolves.
    Lockstep::MatchState state;
    state.match.secondsToLock = 2.0;

    Lockstep::MainPage page;
    page.Create(state);
    Assert::IsFalse(page.State().orders.locked, L"locked before the clock ran out");

    page.Update(1.5);
    Assert::IsFalse(page.State().orders.locked, L"locked with half a second still on the clock");
    Assert::AreEqual(std::string{"00:00:01"}, Lockstep::MainPage::FormatCountdown(page.State().match.secondsToLock),
                     L"half a second left did not read as one");

    page.Update(1.0);
    Assert::IsTrue(page.State().orders.locked, L"the clock ran out and the orders did not lock");
    Assert::AreEqual(0.0, page.State().match.secondsToLock, 0.0001, L"the countdown ran past zero");
    Assert::AreEqual(std::string{"00:00:00"}, Lockstep::MainPage::FormatCountdown(page.State().match.secondsToLock));
  }

  TEST_METHOD(ALockedCountdownStopsMoving)
  {
    // Once locked the client is waiting for the server, not counting toward anything. A countdown
    // that kept running would be counting toward a deadline that has already passed.
    Lockstep::MatchState state;
    state.match.secondsToLock = 0.5;

    Lockstep::MainPage page;
    page.Create(state);
    page.Update(1.0);
    Assert::IsTrue(page.State().orders.locked);

    page.Update(30.0);
    Assert::AreEqual(0.0, page.State().match.secondsToLock, 0.0001, L"a locked countdown kept counting");
    Assert::IsTrue(page.State().orders.locked, L"a locked page unlocked itself");
  }

  TEST_METHOD(AFinishedMatchIsLockedFromTheStart)
  {
    // The match is over; there is no next tick to order for. `ViewOf` folds this in so that every
    // control is inert without the screen having to ask twice.
    Lockstep::MatchState state;
    state.match.secondsToLock = 3600.0;
    state.match.finished = true;
    state.orders.locked = true;

    Lockstep::MainPage page;
    page.Create(state);
    Assert::IsTrue(page.State().orders.locked);

    page.Update(1.0);
    Assert::AreEqual(3600.0, page.State().match.secondsToLock, 0.0001, L"a finished match counted down to something");
  }

  TEST_METHOD(TheOrdersTickIsTheOneAfterTheDigest)
  {
    // The digest is the tick that resolved; what the player is editing is the one after it. Getting
    // this backwards would put screen 06's notice a tick out in both of its sentences.
    Lockstep::MatchState state;
    state.match.tick = 46;

    Lockstep::MainPage page;
    page.Create(state);
    Assert::AreEqual(47U, page.State().OrdersTick());
  }
};

} // namespace LockstepTests
