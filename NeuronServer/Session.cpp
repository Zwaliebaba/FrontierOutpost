// Session.cpp -- one match, running, and the two things that decide when.
//
// There is very little here, and that is the design working. The session routes bytes it cannot
// read to a simulation it cannot see inside, on a schedule it is told about by a caller that owns
// the clock. Everything it is allowed to know is on an interface.

#include "pch.h"
#include "Session.h"

namespace Neuron
{

Session::Session(std::unique_ptr<Simulation> _simulation, TickSchedule _schedule, std::string _storePath, std::vector<std::string> _tokens)
  : m_simulation(std::move(_simulation)),
    m_schedule(_schedule),
    m_storePath(std::move(_storePath))
{
  ASSERT_TEXT(m_simulation != nullptr, L"A session without a simulation has nothing to run.");
  m_contents.configuration = m_simulation->Configuration();
  m_contents.hash = m_simulation->Hash();
  m_contents.startedAt = m_schedule.StartedAt();
  m_contents.intervalSeconds = m_schedule.IntervalSeconds();
  m_contents.finished = m_simulation->IsFinished();
  m_contents.tokens = std::move(_tokens);
}

bool Session::Reload(Simulation& _simulation, const MatchStore::Contents& _contents)
{
  for (const std::vector<PlayerTurn>& tick : _contents.ticks)
  {
    for (std::size_t player = 0; player < tick.size(); ++player)
    {
      const PlayerTurn& turn = tick[player];
      const auto index = static_cast<std::int32_t>(player);

      if (turn.present)
      {
        _simulation.MarkPresent(index);
      }
      if (!turn.orders.empty())
      {
        _simulation.Submit(index, turn.orders);
      }
    }
    _simulation.Resolve();
  }

  // The check ADR-024 rests on. A store replayed into a different state than it was written from
  // means the rules have moved under a live match, and the only safe thing to do is refuse.
  return _simulation.Hash() == _contents.hash;
}

std::unique_ptr<Session> Session::Resume(std::unique_ptr<Simulation> _simulation, const MatchStore::Contents& _contents,
                                         std::string _storePath)
{
  ASSERT_TEXT(_simulation != nullptr, L"Resuming into no simulation resumes nothing.");
  if (!Reload(*_simulation, _contents))
  {
    return nullptr;
  }

  auto session = std::make_unique<Session>(std::move(_simulation), TickSchedule{_contents.startedAt, _contents.intervalSeconds},
                                           std::move(_storePath), _contents.tokens);

  // The stored turns are kept, so that the next persist writes the whole history and not a match
  // that appears to have begun at the restart. The hash is the replayed one, which Reload has just
  // shown to be the stored one.
  session->m_contents = _contents;
  return session;
}

void Session::MarkPresent(std::int32_t _player)
{
  m_simulation->MarkPresent(_player);
}

bool Session::Submit(std::int32_t _player, std::span<const std::uint8_t> _orders)
{
  if (_player < 0 || _player >= m_simulation->PlayerCount())
  {
    return false;
  }

  // Submitting is not being present. The two are marked separately because they are separate facts
  // and only one of them is what the custodian rule counts -- a client that posts orders from a
  // retry queue has not logged in.
  m_simulation->Submit(_player, _orders);
  return true;
}

std::uint32_t Session::Advance(Instant _now)
{
  std::uint32_t resolved = 0;

  while (m_schedule.LocksOwed(m_simulation->Tick(), _now) > 0)
  {
    if (m_simulation->IsFinished())
    {
      // The match is over. The schedule keeps producing locks because it is arithmetic and does not
      // know that; the session is the thing that knows, which is where ADR-023 left the question.
      break;
    }

    m_simulation->Resolve();
    RememberDigests();
    m_contents.ticks.push_back(m_simulation->LockedTurn());
    m_contents.hash = m_simulation->Hash();
    m_contents.finished = m_simulation->IsFinished();
    ++resolved;
    ++m_resolvedHere;

    // Persisted after every lock rather than at the end. A tick that resolved and was not written
    // is a tick that replays differently, and the whole store is a few kilobytes.
    Persist();
  }

  return resolved;
}

std::int64_t Session::SecondsUntilNextLock(Instant _now) const
{
  return m_schedule.SecondsUntilLockOf(m_simulation->Tick(), _now);
}

std::vector<std::string> Session::TakeEvents()
{
  return m_simulation->TakeEvents();
}

std::vector<std::uint8_t> Session::SnapshotFor(std::int32_t _player) const
{
  return m_simulation->SnapshotFor(_player);
}

std::vector<std::uint8_t> Session::DigestFor(std::int32_t _player) const
{
  return m_simulation->DigestFor(_player);
}

std::vector<Protocol::TickDigest> Session::DigestsFor(std::int32_t _player) const
{
  if (_player < 0 || static_cast<std::size_t>(_player) >= m_digests.size())
  {
    return {};
  }
  return m_digests[static_cast<std::size_t>(_player)];
}

void Session::RememberDigests()
{
  const std::size_t players = static_cast<std::size_t>(m_simulation->PlayerCount());
  m_digests.resize(players);

  const std::uint32_t tick = m_simulation->Tick();
  for (std::size_t player = 0; player < players; ++player)
  {
    std::vector<Protocol::TickDigest>& kept = m_digests[player];
    kept.push_back(Protocol::TickDigest{.tick = tick, .bytes = m_simulation->DigestFor(static_cast<std::int32_t>(player))});

    // Oldest first, so the front is what falls off. A deque would save the shift and cost a
    // container nobody else in this tree uses, for eight elements four times a day.
    if (kept.size() > DIGEST_HISTORY)
    {
      kept.erase(kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(kept.size() - DIGEST_HISTORY));
    }
  }
}

void Session::Persist()
{
  if (m_storePath.empty())
  {
    return;
  }

  // A failed write does not stop the match. Six people are mid-tick and a full disk is not a reason
  // to end their weekend; the session records that it could not write and whoever is watching the
  // server can see it.
  m_persisted = MatchStore::Save(m_storePath, m_contents);
}

} // namespace Neuron
