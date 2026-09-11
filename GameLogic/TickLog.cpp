// TickLog.cpp -- names for the things the resolver logged.

#include "pch.h"
#include "TickLog.h"

namespace Lockstep
{

const char* Describe(Phase _phase) noexcept
{
  switch (_phase)
  {
  case Phase::Lock:
    return "lock";
  case Phase::Production:
    return "production";
  case Phase::Movement:
    return "movement";
  case Phase::Combat:
    return "combat";
  case Phase::Claims:
    return "claims";
  case Phase::Digest:
    return "digest";
  default:
    return "unknown";
  }
}

const char* Describe(DigestKind _kind) noexcept
{
  switch (_kind)
  {
  case DigestKind::Contact:
    return "contact";
  case DigestKind::ProposalReceived:
    return "proposal received";
  case DigestKind::ProposalAnswered:
    return "proposal answered";
  case DigestKind::ProposalWithdrawn:
    return "proposal withdrawn";
  case DigestKind::ProposalIgnored:
    return "proposal ignored";
  case DigestKind::ProposalVoided:
    return "proposal voided";
  case DigestKind::OrderRejected:
    return "order rejected";
  case DigestKind::SystemClaimed:
    return "system claimed";
  case DigestKind::SystemLost:
    return "system lost";
  case DigestKind::SiegeBegun:
    return "siege begun";
  case DigestKind::Battle:
    return "battle";
  case DigestKind::LaneOpened:
    return "lane opened";
  case DigestKind::LaneCanceled:
    return "lane canceled";
  case DigestKind::AgreementOpened:
    return "agreement opened";
  case DigestKind::AgreementBreached:
    return "agreement breached";
  case DigestKind::Economy:
    return "economy";
  case DigestKind::Region:
    return "region";
  case DigestKind::Custodian:
    return "custodian";
  case DigestKind::MatchEnded:
    return "match ended";
  default:
    return "unknown";
  }
}

std::uint32_t TickLog::Dodges() const
{
  std::uint32_t dodged = 0;
  for (const Interception& interception : interceptions)
  {
    dodged += interception.dodged ? 1U : 0U;
  }
  return dodged;
}

const PhaseRecord* TickLog::Find(Phase _phase) const
{
  for (const PhaseRecord& record : phases)
  {
    if (record.phase == _phase)
    {
      return &record;
    }
  }
  return nullptr;
}

std::vector<std::string> TickLog::AllLines() const
{
  std::vector<std::string> lines;
  for (const PhaseRecord& record : phases)
  {
    for (const std::string& line : record.lines)
    {
      lines.push_back(line);
    }
  }
  return lines;
}

} // namespace Lockstep
