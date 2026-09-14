// LockstepTests.cpp -- the executable's client-side logic, under test at last.
//
// **Why this project exists.** `Lockstep` is an executable because R13 says the game ships as one
// file, and nothing could link an executable -- so `ViewOf`, `OrdersOf`, `ComposeSignals`,
// `DigestView` and `FormatCountdown` were reachable by no test at all. Two passes in a row ended
// with a throwaway harness compiled outside the repository to prove a change worked, and a
// verification that has to be rebuilt by hand is a verification nobody runs twice. The second of
// those passes found an arithmetic bug in `FormatCountdown` by PHOTOGRAPHING A RUNNING CLIENT.
//
// The four translation units are compiled a second time into this DLL rather than carved into a
// fifth library: see the comment in `LockstepTests.vcxproj`.
//
// What is here and what is not. Everything in these files that is a decision -- what a player can
// say, what they read first, when their orders lock -- is tested. The drawing is not: a
// `DrawWorld` needs a device, a swap chain and a frame, and what it produces is pixels nobody can
// assert about. Screens are still verified by photographing them (ADR-038, ADR-039), and the
// dividing line is exactly this: if it decides something it belongs here, and if it draws
// something it belongs in a screenshot.

#include "pch.h"
#include "CppUnitTest.h"

#include "MapRender.h"
#include "MatchState.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace LockstepTests
{

// The map's third axis carries data (ADR-103): a station stands as tall as its yield. The rule is
// pure and these are the three things about it that decide what a player reads off the board.
TEST_CLASS(StationGeometryTests)
{
public:
  TEST_METHOD(AnUnknownYieldStandsAtThePlainHeightRatherThanLyingDown)
  {
    // Zero is a system nobody holds, or a board the server has not priced: both stand.
    Assert::IsTrue(Lockstep::StemHeightFor(0, false) > 0.0F, L"a station lying on the ground");
    Assert::IsTrue(Lockstep::StemHeightFor(0, true) > Lockstep::StemHeightFor(0, false), L"a capital stands taller than a plain system");
    // And the base yield stands exactly where every system stood before the axis meant anything,
    // so a priced board and an unpriced one look alike until a system earns more than the base.
    Assert::AreEqual(Lockstep::StemHeightFor(0, false), Lockstep::StemHeightFor(2, false), 0.0001F);
  }

  TEST_METHOD(HeightAndFootprintRiseWithYield)
  {
    for (std::uint32_t yield = 1; yield < 40; ++yield)
    {
      Assert::IsTrue(Lockstep::StemHeightFor(yield + 1, false) > Lockstep::StemHeightFor(yield, false), L"a richer system stands lower");
      Assert::IsTrue(Lockstep::FootprintRadiusFor(yield + 1) > Lockstep::FootprintRadiusFor(yield), L"a richer system reaches less far");
    }
  }

  TEST_METHOD(ACapitalNeverStandsLowerThanItDid)
  {
    // A capital's floor is its old fixed height: a capital halved by custodian spoils is still the
    // tallest thing in its cluster, because the capital is the thing the cluster is about.
    const float floor = Lockstep::StemHeightFor(0, true);
    Assert::AreEqual(floor, Lockstep::StemHeightFor(1, true), 0.0001F, L"a poor capital dropped below its floor");
    Assert::IsTrue(Lockstep::StemHeightFor(16, true) > floor, L"a rich capital does not rise above it");
    Assert::AreEqual(Lockstep::StemHeightFor(16, false), Lockstep::StemHeightFor(16, true), 0.0001F,
                     L"above the floor a capital and a plain system are the same scale");
  }
};

TEST_CLASS(OwnerColorTests)
{
public:
  TEST_METHOD(BlueIsAlwaysYouAndNeverAnybodyElse)
  {
    // ADR-027, and the whole economy of the palette: blue is also accept and also a trade lane, so
    // a rival wearing it would make three meanings collide on one screen.
    const Neuron::Color mine = Lockstep::OwnerColor(3, 3);
    for (Lockstep::OwnerId owner = 0; owner < 12; ++owner)
    {
      if (owner == 3)
      {
        continue;
      }
      const Neuron::Color theirs = Lockstep::OwnerColor(owner, 3);
      Assert::IsFalse(theirs.red == mine.red && theirs.green == mine.green && theirs.blue == mine.blue,
                      L"a rival is wearing the viewer's blue");
    }
  }

  TEST_METHOD(ARivalLooksTheSameToEverybodyWhoIsNotThem)
  {
    // The other half of ADR-027: "you" is relative to the viewer, and everything else is not. Two
    // players comparing screenshots have to be able to talk about the orange empire.
    const Neuron::Color toOne = Lockstep::OwnerColor(5, 1);
    const Neuron::Color toTwo = Lockstep::OwnerColor(5, 2);
    Assert::IsTrue(toOne.red == toTwo.red && toOne.green == toTwo.green && toOne.blue == toTwo.blue,
                   L"the same rival is two colours to two viewers");
  }

  TEST_METHOD(EveryRivalColorIsDistinct)
  {
    // Twelve players, twelve colours, and the point of them is telling empires apart on a map.
    for (Lockstep::OwnerId first = 0; first < 12; ++first)
    {
      for (Lockstep::OwnerId second = first + 1; second < 12; ++second)
      {
        const Neuron::Color a = Lockstep::OwnerColor(first, Lockstep::NOBODY);
        const Neuron::Color b = Lockstep::OwnerColor(second, Lockstep::NOBODY);
        Assert::IsFalse(a.red == b.red && a.green == b.green && a.blue == b.blue, L"two rivals share a colour");
      }
    }
  }

  TEST_METHOD(NobodyHasAColorToo)
  {
    // Unclaimed systems are most of the map at tick zero. A lookup that fell off the end of the
    // table would be a crash on the first frame of every match.
    const Neuron::Color unclaimed = Lockstep::OwnerColor(Lockstep::NOBODY, 0);
    Assert::IsTrue(unclaimed.alpha > 0, L"unclaimed space is invisible");
  }
};

} // namespace LockstepTests
