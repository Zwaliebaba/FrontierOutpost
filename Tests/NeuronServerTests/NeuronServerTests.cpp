#include "pch.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

// NeuronServer has no behaviour left to test.
//
// It held the Session that owned the authoritative loop until 2026-09-11, when ADR-015 removed the MVP-01 vertical slice. What remains
// is the library's umbrella header and its precompiled header -- the shape of a project waiting
// for the 4X authoritative loop that will replace it.
//
// THIS PLACEHOLDER IS LOAD-BEARING, and AGENTS.md 3 says why: vstest reports "no tests found" as
// a PASS, so a suite with nothing in it is a green check mark over a library nobody exercised.
// Delete this the day the first real test lands, and not before.
TEST_CLASS(SuiteSmoke)
{
public:
  TEST_METHOD(TheSuiteRuns)
  {
    Assert::IsTrue(true, L"the suite is wired up and running");
  }
};

} // namespace NeuronServerTests
