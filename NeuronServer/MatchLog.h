#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Neuron
{

/// The instrumentation log: one line per event, timestamped UTC, appended.
///
/// **ADR-030, and the second sanctioned exception to R13.** The test plan opens with
/// *"Instrumentation (all phases): timestamped events for login, session start/end, order edit,
/// order lock... The login curve is the primary instrument; surveys are secondary."* A Phase 0 that
/// cannot draw that curve answers nothing, and a debug trace only a debugger sees is not a curve.
///
/// Append-only and flushed per write. A crash mid-match must not lose the record of the match, and
/// at a few lines a tick the cost of flushing is not measurable against the cost of a Phase 0 run
/// nobody can analyse.
///
/// **It is plain text, one line per event, and that is deliberate.** Its reader is a person with a
/// spreadsheet, not a program. A structured format nobody has asked for would be a format guessed
/// at, and the guess would be wrong in a way discovered after the matches were played.
class MatchLog
{
public:
  /// Opens for appending. A log that cannot be opened is reported once and then quietly ignored --
  /// losing instrumentation is bad, and stopping a live match because a disk is full is worse.
  explicit MatchLog(std::string _path);

  [[nodiscard]] bool Open() const noexcept
  {
    return m_open;
  }
  [[nodiscard]] const std::string& Path() const noexcept
  {
    return m_path;
  }

  /// Writes one line, prefixed with the UTC timestamp.
  ///
  /// **This is the one place in the tree that reads a wall clock on purpose.** The simulation is
  /// forbidden one (R16) and the schedule is handed an instant (ADR-026); a log needs the real time
  /// or its lines cannot be lined up against six people's memories of a weekend.
  void Write(const std::string& _line);

  void Write(const std::vector<std::string>& _lines);

private:
  /// UTF-8, as every path in this tree is; the wide copy is what the CRT is handed, converted once.
  std::string m_path;
  std::wstring m_widePath;
  bool m_open = false;
  bool m_complained = false;
};

} // namespace Neuron
