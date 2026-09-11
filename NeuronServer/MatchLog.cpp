// MatchLog.cpp -- the instrumentation log, and the one deliberate wall clock in this tree.

#include "pch.h"
#include "MatchLog.h"

#include <chrono>
#include <cstdio>

namespace Neuron
{

namespace
{

/// "2026-09-11T14:03:27Z". UTC, because six people in four time zones reading one log is exactly
/// the case this has to survive, and a local timestamp makes that log unreadable.
[[nodiscard]] std::string NowUtc()
{
  const auto now = std::chrono::system_clock::now();
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(now));
}

} // namespace

MatchLog::MatchLog(std::string _path)
  : m_path(std::move(_path))
{
  if (m_path.empty())
  {
    return;
  }

  // Opened once to prove it can be, then closed. Each write reopens: a file handle held for three
  // weeks is a file nobody can move, copy or read while the match is running, and reading the log
  // mid-match is exactly what somebody watching a Phase 0 run wants to do.
  std::FILE* file = nullptr;
  if (fopen_s(&file, m_path.c_str(), "ab") == 0 && file != nullptr)
  {
    m_open = true;
    (void)std::fclose(file);
  }
}

void MatchLog::Write(const std::string& _line)
{
  if (!m_open)
  {
    return;
  }

  std::FILE* file = nullptr;
  if (fopen_s(&file, m_path.c_str(), "ab") != 0 || file == nullptr)
  {
    if (!m_complained)
    {
      DebugTrace("MatchLog: cannot write {}\n", m_path);
      m_complained = true;
    }
    return;
  }

  const std::string stamped = NowUtc() + " " + _line + "\n";
  (void)std::fwrite(stamped.data(), 1, stamped.size(), file);

  // Flushed and closed per line. A crash mid-match must not lose the record of the match, and at a
  // few lines a tick this costs nothing worth measuring.
  (void)std::fclose(file);
}

void MatchLog::Write(const std::vector<std::string>& _lines)
{
  for (const std::string& line : _lines)
  {
    Write(line);
  }
}

} // namespace Neuron
