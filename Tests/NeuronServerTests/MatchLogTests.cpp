// MatchLogTests.cpp -- the instrumentation log, and the events that reach it.
//
// ADR-030. These tests exist because the log is the second sanctioned exception to R13 and because
// the bug it already had was invisible: a path relative to the working directory wrote nothing at
// all, silently, for a whole test run. A log that fails quietly is the worst kind, so what is
// asserted here is mostly that lines actually land in a file somebody can open.

#include "pch.h"
#include "CppUnitTest.h"

#include "MatchLog.h"
#include "Session.h"
#include "TickSchedule.h"

#include "CountingSimulation.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

namespace
{

constexpr std::uint32_t ONE_HOUR_INTERVAL = 60 * 60;
constexpr Neuron::Instant ONE_HOUR = ONE_HOUR_INTERVAL;

/// A path in the temp directory that nothing else is using. A copy of the one in
/// `NeuronServerTests.cpp` rather than a shared helper, because two callers in two files is where
/// AGENTS.md R2 says a helper starts earning its keep and these two want different extensions.
[[nodiscard]] std::string TemporaryLogPath(const char* _name)
{
  char buffer[512] = {};
  std::size_t length = 0;
  if (getenv_s(&length, buffer, sizeof(buffer), "TEMP") != 0 || length == 0)
  {
    return std::string("frontier-") + _name + ".log";
  }
  return std::string(buffer) + "\\frontier-" + _name + ".log";
}

/// Every line of a file, in order. Empty if there is no file, which is the case a test about a log
/// that wrote nothing has to be able to tell from a log that wrote a blank line.
[[nodiscard]] std::vector<std::string> LinesOf(const std::string& _path)
{
  std::vector<std::string> lines;
  std::ifstream file{_path, std::ios::binary};
  std::string line;
  while (std::getline(file, line))
  {
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    lines.push_back(line);
  }
  return lines;
}

void Erase(const std::string& _path)
{
  (void)std::remove(_path.c_str());
}

/// "2026-09-11T14:03:27Z" -- twenty characters, ending in Z, with the separators where a person
/// reading six people's logs side by side needs them.
[[nodiscard]] bool LooksLikeAUtcStamp(const std::string& _line)
{
  return _line.size() > 20 && _line[4] == '-' && _line[7] == '-' && _line[10] == 'T' && _line[13] == ':' && _line[16] == ':' &&
         _line[19] == 'Z' && _line[20] == ' ';
}

} // namespace

TEST_CLASS(MatchLogTests)
{
public:
  TEST_METHOD(EveryLineIsStampedInUtcAndKeptInOrder)
  {
    const std::string path = TemporaryLogPath("stamped");
    Erase(path);

    {
      Neuron::MatchLog log{path};
      Assert::IsTrue(log.Open(), L"a log in the temp directory must open");
      log.Write("first");
      log.Write("second");
    }

    const std::vector<std::string> lines = LinesOf(path);
    Assert::AreEqual(std::size_t{2}, lines.size());
    Assert::IsTrue(LooksLikeAUtcStamp(lines[0]), L"a timestamp a spreadsheet can sort");
    Assert::IsTrue(LooksLikeAUtcStamp(lines[1]));
    Assert::AreEqual(std::string("first"), lines[0].substr(21));
    Assert::AreEqual(std::string("second"), lines[1].substr(21));

    Erase(path);
  }

  // The reason every write reopens the file: somebody watching a forty-eight-hour run wants to read
  // the log while the match is still going, and a handle held open for three weeks is a file they
  // cannot copy.
  TEST_METHOD(LinesAreReadableWhileTheLogIsStillOpen)
  {
    const std::string path = TemporaryLogPath("mid-match");
    Erase(path);

    Neuron::MatchLog log{path};
    log.Write("match-start seed=1");

    const std::vector<std::string> midMatch = LinesOf(path);
    Assert::AreEqual(std::size_t{1}, midMatch.size(), L"flushed per line, not at the end");

    log.Write("T1 resolved");
    Assert::AreEqual(std::size_t{2}, LinesOf(path).size());

    Erase(path);
  }

  // A second match in the same directory appends. ADR-030 says so and says why the `match-start`
  // line is what separates them.
  TEST_METHOD(ASecondMatchAppendsRatherThanTruncates)
  {
    const std::string path = TemporaryLogPath("appending");
    Erase(path);

    {
      Neuron::MatchLog first{path};
      first.Write("match-start seed=1");
    }
    {
      Neuron::MatchLog second{path};
      second.Write("match-start seed=2");
    }

    const std::vector<std::string> lines = LinesOf(path);
    Assert::AreEqual(std::size_t{2}, lines.size(), L"the first match's record must survive the second");

    Erase(path);
  }

  // Losing instrumentation is bad; ending a live match because a disk is full is worse.
  TEST_METHOD(ALogThatCannotBeOpenedIsQuietRatherThanFatal)
  {
    Neuron::MatchLog log{"C:/nothing-here/and-no-directory-either/match.log"};
    Assert::IsFalse(log.Open());

    log.Write("this goes nowhere");
    log.Write(std::vector<std::string>{"and", "so", "do", "these"});
  }

  TEST_METHOD(AnEmptyPathIsNotALog)
  {
    Neuron::MatchLog log{std::string{}};
    Assert::IsFalse(log.Open());
    log.Write("nowhere");
  }

  // What the executable actually does with it: the game narrates, the session forwards, the log
  // writes. ADR-030's whole shape, in one test.
  TEST_METHOD(WhatTheGameNarratesReachesTheFile)
  {
    const std::string path = TemporaryLogPath("narrated");
    Erase(path);

    auto simulation = std::make_unique<CountingSimulation>();
    CountingSimulation& game = *simulation;
    Neuron::Session session{std::move(simulation), Neuron::TickSchedule{0, ONE_HOUR_INTERVAL}, std::string{}};

    Neuron::MatchLog log{path};
    game.events = {"T1 capital-fall player=3 to=1", "T1 dancing dodged=2 targeted=3 rear-guard=off"};
    session.Advance(ONE_HOUR);
    log.Write(session.TakeEvents());

    const std::vector<std::string> lines = LinesOf(path);
    Assert::AreEqual(std::size_t{2}, lines.size());
    Assert::AreEqual(std::string("T1 capital-fall player=3 to=1"), lines[0].substr(21));

    // Taken and cleared. A second advance that narrates nothing must not write the same two lines
    // again -- an event counted twice is a measurement that reads as a mechanic being used more
    // than it was.
    session.Advance(2 * ONE_HOUR);
    log.Write(session.TakeEvents());
    Assert::AreEqual(std::size_t{2}, LinesOf(path).size());

    Erase(path);
  }
};

} // namespace NeuronServerTests
