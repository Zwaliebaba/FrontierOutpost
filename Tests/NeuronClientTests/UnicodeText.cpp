// Tests/NeuronClientTests/UnicodeText.cpp
//
// UTF-8 at NeuronClient's API and UTF-16 at Windows' convert both ways without loss, a character
// outside the Basic Multilingual Plane included (Design/ADR/ADR-005).
#include "pch.h"

#include "Unicode.h"

#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

// "Grusse" with its umlaut and sharp s, the CJK "world", and a grinning face from outside the
// Basic Multilingual Plane, in escapes, so that the test does not depend on how the file is read.
constexpr std::string_view UTF8 = "Gr\xC3\xBC\xC3\x9F"
                                  "e \xE4\xB8\x96\xE7\x95\x8C \xF0\x9F\x98\x80";
constexpr std::wstring_view UTF16 = L"Gr\u00FC\u00DFe \u4E16\u754C \U0001F600";

} // namespace

TEST_CLASS(UnicodeText)
{
public:
  TEST_METHOD(ConvertsUtf8ToUtf16)
  {
    Assert::IsTrue(Neuron::Utf8ToUtf16(UTF8) == UTF16, L"the UTF-16 differs");
  }

  TEST_METHOD(ConvertsUtf16ToUtf8)
  {
    Assert::IsTrue(Neuron::Utf16ToUtf8(UTF16) == UTF8, L"the UTF-8 differs");
  }

  TEST_METHOD(ReplacesAnInvalidSequence)
  {
    Assert::IsTrue(Neuron::Utf8ToUtf16("a\xFF"
                                       "b") == L"a\uFFFDb",
                   L"an invalid byte is not U+FFFD");
  }

  TEST_METHOD(KeepsEmptyTextEmpty)
  {
    Assert::IsTrue(Neuron::Utf8ToUtf16("").empty(), L"empty UTF-8 became text");
    Assert::IsTrue(Neuron::Utf16ToUtf8(L"").empty(), L"empty UTF-16 became text");
  }
};

} // namespace NeuronClientTests
