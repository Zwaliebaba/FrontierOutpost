#include "pch.h"
#include "CppUnitTest.h"

#include "NeuronCore.h"

#include "Id.h"
#include "Prng.h"
#include "Turns16.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

// The generator's sequence is a wire format in all but name.
//
// Every galaxy any match is ever played on comes out of these numbers, so changing them changes
// every galaxy that has ever been generated -- which is why the values below are pinned by value
// and why this suite failing is the point rather than a nuisance (ADR-018).
//
// The three vectors are the published splitmix64 outputs for their seeds, so this is a test against
// the algorithm as specified rather than against whatever this file happens to compute.
TEST_CLASS(PrngTests)
{
public:
  TEST_METHOD(TheSequenceIsSplitmix64)
  {
    struct Vector
    {
      std::uint64_t seed;
      std::array<std::uint64_t, 4> expected;
    };

    constexpr std::array<Vector, 3> VECTORS = {{
      {0x0000000000000000ULL, {0xE220A8397B1DCDAFULL, 0x6E789E6AA1B965F4ULL, 0x06C45D188009454FULL, 0xF88BB8A8724C81ECULL}},
      {0x0000000000000001ULL, {0x910A2DEC89025CC1ULL, 0xBEEB8DA1658EEC67ULL, 0xF893A2EEFB32555EULL, 0x71C18690EE42C90BULL}},
      {0x0123456789ABCDEFULL, {0x157A3807A48FAA9DULL, 0xD573529B34A1D093ULL, 0x2F90B72E996DCCBEULL, 0xA2D419334C4667ECULL}},
    }};

    for (const Vector& vector : VECTORS)
    {
      Neuron::Prng prng{vector.seed};
      for (std::size_t draw = 0; draw < vector.expected.size(); ++draw)
      {
        Assert::AreEqual(vector.expected[draw], prng.Next(),
                         (std::wstring(L"draw ") + std::to_wstring(draw) + L" of seed " + std::to_wstring(vector.seed)).c_str());
      }
    }
  }

  TEST_METHOD(TheSameSeedGivesTheSameSequence)
  {
    Neuron::Prng first{0xFEEDFACEULL};
    Neuron::Prng second{0xFEEDFACEULL};
    for (std::int32_t draw = 0; draw < 64; ++draw)
    {
      Assert::AreEqual(first.Next(), second.Next());
    }
  }

  TEST_METHOD(BoundedDrawsStayInRange)
  {
    Neuron::Prng prng{7};
    for (std::uint32_t bound = 1; bound <= 17; ++bound)
    {
      for (std::int32_t draw = 0; draw < 200; ++draw)
      {
        Assert::IsTrue(prng.Below(bound) < bound, L"Below must be strictly below its bound");
      }
    }
  }

  TEST_METHOD(ABoundOfOneOrZeroIsAlwaysZero)
  {
    Neuron::Prng prng{1};
    Assert::AreEqual(0u, prng.Below(0));
    Assert::AreEqual(0u, prng.Below(1));
  }

  // Modulo alone would bias toward the low end by an amount far too small to see and quite large
  // enough to make a generated galaxy consistently favour one direction. This does not prove the
  // rejection is right -- no sample does -- but it would catch a bound so skewed that a whole
  // outcome had gone missing.
  TEST_METHOD(BoundedDrawsCoverEveryOutcome)
  {
    constexpr std::uint32_t BOUND = 6;
    constexpr std::int32_t DRAWS = 60000;

    std::array<std::int32_t, BOUND> counts = {};
    Neuron::Prng prng{0x5EEDULL};
    for (std::int32_t draw = 0; draw < DRAWS; ++draw)
    {
      counts[prng.Below(BOUND)]++;
    }

    // Expected 10,000 each. A 20% window is loose enough never to flake on a fixed sequence and
    // tight enough to catch a real skew.
    for (std::size_t outcome = 0; outcome < counts.size(); ++outcome)
    {
      Assert::IsTrue(
        counts[outcome] > 8000 && counts[outcome] < 12000,
        (std::wstring(L"outcome ") + std::to_wstring(outcome) + L" came up " + std::to_wstring(counts[outcome]) + L" times in 60000")
          .c_str());
    }
  }

  TEST_METHOD(BetweenIsInclusiveAtBothEnds)
  {
    Neuron::Prng prng{99};
    bool sawLow = false;
    bool sawHigh = false;

    for (std::int32_t draw = 0; draw < 500; ++draw)
    {
      const std::int32_t value = prng.Between(2, 4);
      Assert::IsTrue(value >= 2 && value <= 4);
      sawLow = sawLow || value == 2;
      sawHigh = sawHigh || value == 4;
    }

    Assert::IsTrue(sawLow && sawHigh, L"two to four means 2, 3 or 4 -- both ends included");
    Assert::AreEqual(5, prng.Between(5, 5), L"an empty range is its own answer");
    Assert::AreEqual(9, prng.Between(9, 3), L"and an inverted one does not wrap");
  }
};

// A typed index is worth nothing if it is not actually distinct, and worth a great deal if it is.
TEST_CLASS(IdTests)
{
public:
  struct AlphaTag;
  struct BetaTag;
  using Alpha = Neuron::Id<AlphaTag>;
  using Beta = Neuron::Id<BetaTag>;

  // Asserted twice on purpose. The `static_assert` is the real claim -- an id costs nothing at
  // runtime and can be used where a constant is required -- and the runtime assert is what makes
  // the suite report the case rather than silently compiling it away.
  TEST_METHOD(ADefaultIdIsInvalid)
  {
    static_assert(!Alpha{}.IsValid());
    static_assert(Alpha{}.Index() == Alpha::NONE);

    Assert::IsFalse(Alpha{}.IsValid());
    Assert::AreEqual(Alpha::NONE, Alpha{}.Index());
  }

  TEST_METHOD(AnIdRemembersItsIndex)
  {
    static_assert(Alpha{2}.IsValid());
    static_assert(Alpha{2}.Index() == 2);
    static_assert(Alpha{2}.AsSize() == 2);

    Assert::IsTrue(Alpha{2}.IsValid());
    Assert::AreEqual(2, Alpha{2}.Index());
    Assert::AreEqual(static_cast<size_t>(2), Alpha{2}.AsSize());
  }

  TEST_METHOD(IdsCompareAndOrder)
  {
    Assert::IsTrue(Alpha{1} == Alpha{1});
    Assert::IsTrue(Alpha{1} != Alpha{2});
    Assert::IsTrue(Alpha{1} < Alpha{2}, L"ordering is part of the contract: ADR-018 sorts by id");
  }

  // The whole reason the tag exists. Two ids of different tags must not be interchangeable, and a
  // raw integer must not become one by accident.
  TEST_METHOD(DifferentTagsAreDifferentTypes)
  {
    Assert::IsFalse((std::is_same_v<Alpha, Beta>));
    Assert::IsFalse((std::is_convertible_v<Alpha, Beta>));
    Assert::IsFalse((std::is_convertible_v<int, Alpha>), L"construction from an integer is explicit");
    Assert::IsFalse((std::is_convertible_v<Alpha, int>), L"and an id does not decay back to one");
  }
};

// The galaxy is laid out on a ring, and the ring is computed in integers because std::cos is not
// bit-identical between standard libraries (ADR-018). These check the approximation is good enough
// to place things with and exact enough to place them the same way twice.
TEST_CLASS(Turns16Tests)
{
public:
  TEST_METHOD(TheCardinalAnglesAreRight)
  {
    Assert::AreEqual(0, Neuron::Sine(0), L"sin 0");
    Assert::AreEqual(Neuron::TRIG_SCALE, Neuron::Sine(Neuron::QUARTER_TURN), L"sin quarter turn");
    Assert::AreEqual(0, Neuron::Sine(Neuron::HALF_TURN), L"sin half turn");
    Assert::AreEqual(-Neuron::TRIG_SCALE, Neuron::Sine(static_cast<Neuron::Turns16>(3 * Neuron::QUARTER_TURN)));

    Assert::AreEqual(Neuron::TRIG_SCALE, Neuron::Cosine(0), L"cos 0");
    Assert::AreEqual(0, Neuron::Cosine(Neuron::QUARTER_TURN), L"cos quarter turn");
    Assert::AreEqual(-Neuron::TRIG_SCALE, Neuron::Cosine(Neuron::HALF_TURN), L"cos half turn");
  }

  // Bhaskara's worst error is about 0.0016 of full scale. At the radii a galaxy uses -- a few
  // hundred units -- that is well under half a unit, which is the accuracy this needs.
  TEST_METHOD(TheApproximationIsWithinTolerance)
  {
    constexpr std::int32_t TOLERANCE = 3; // 3/1024 ~= 0.003, comfortably above the known worst case

    for (std::int32_t step = 0; step < 256; ++step)
    {
      const auto angle = static_cast<Neuron::Turns16>(step * 256);
      const double radians = 2.0 * 3.14159265358979323846 * static_cast<double>(angle) / 65536.0;

      const auto expectedSine = static_cast<std::int32_t>(std::lround(std::sin(radians) * Neuron::TRIG_SCALE));
      const std::int32_t error = std::abs(Neuron::Sine(angle) - expectedSine);
      Assert::IsTrue(error <= TOLERANCE,
                     (std::wstring(L"sine error ") + std::to_wstring(error) + L" at step " + std::to_wstring(step)).c_str());
    }
  }

  // The identity that makes a ring a ring. If this drifts, systems bunch on one side of it.
  TEST_METHOD(SineAndCosineStayOnTheUnitCircle)
  {
    for (std::int32_t step = 0; step < 128; ++step)
    {
      const auto angle = static_cast<Neuron::Turns16>(step * 512);
      const std::int64_t sine = Neuron::Sine(angle);
      const std::int64_t cosine = Neuron::Cosine(angle);
      const std::int64_t squared = sine * sine + cosine * cosine;
      constexpr std::int64_t UNIT = static_cast<std::int64_t>(Neuron::TRIG_SCALE) * Neuron::TRIG_SCALE;

      Assert::IsTrue(std::abs(squared - UNIT) < UNIT / 100, L"sin^2 + cos^2 must stay within one percent of one");
    }
  }

  TEST_METHOD(EvenSpacingWrapsExactly)
  {
    Assert::AreEqual(static_cast<int>(0), static_cast<int>(Neuron::EvenlySpaced(0, 6)));
    Assert::AreEqual(static_cast<int>(Neuron::HALF_TURN), static_cast<int>(Neuron::EvenlySpaced(3, 6)));
    Assert::AreEqual(static_cast<int>(0), static_cast<int>(Neuron::EvenlySpaced(0, 0)), L"no steps is angle zero, not a divide by zero");
  }

  // Rounding half away from zero rather than truncating, so a ring is symmetric about its centre
  // instead of drifting one unit toward it on the negative side.
  TEST_METHOD(OffsetsRoundSymmetrically)
  {
    Assert::AreEqual(100, Neuron::OffsetAlong(100, Neuron::TRIG_SCALE));
    Assert::AreEqual(-100, Neuron::OffsetAlong(100, -Neuron::TRIG_SCALE));
    Assert::AreEqual(50, Neuron::OffsetAlong(100, Neuron::TRIG_SCALE / 2));
    Assert::AreEqual(-50, Neuron::OffsetAlong(100, -Neuron::TRIG_SCALE / 2));
  }
};

} // namespace NeuronCoreTests
