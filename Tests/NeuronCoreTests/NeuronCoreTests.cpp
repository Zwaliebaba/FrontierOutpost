#include "pch.h"
#include "CppUnitTest.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

// The wizard's empty TestMethod1 is gone. This suite is a placeholder, but not an empty one:
// vstest reports "no tests found" as a pass, so a suite with nothing in it would let a broken
// NeuronCore build through CI unnoticed. The assertion below is the one thing worth asserting
// before there is any NeuronCore code -- that this really is the only platform we build
// (AGENTS.md 3). Delete this class when the first real NeuronCore test lands.
TEST_CLASS(SuiteSmoke)
{
public:
  TEST_METHOD(BuiltForX64)
  {
    Assert::AreEqual(static_cast<size_t>(8), sizeof(void*), L"x64 is the only supported platform.");
  }
};

} // namespace NeuronCoreTests
