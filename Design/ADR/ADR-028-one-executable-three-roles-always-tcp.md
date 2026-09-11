# ADR-028 — One executable, three roles, and the client always talks TCP

**Status:** Accepted

**Date:** 2026-09-11
**Decided by:** **Owner decision, 2026-09-11**, on the shape (one executable, embedded server, TCP throughout). The protocol details are the build session's, recorded here with it.
**Supersedes:** — (succeeds ADR-006, which is Deprecated)

---

## Context

`4X-02` Step 3 needs six people on six machines. Two questions had to be answered together, because
the answer to each changes the other.

**TCP or UDP.** ADR-006 chose in-process queues at 20 Hz for a real-time ship demo and is
Deprecated. This game sends **four messages a day per player**.

**One executable or two.** `4X-02`'s draft left it open — *"a `--serve` flag or a second executable"*
— and noted that a second executable changes AGENTS.md §2's "nine projects" and that R13 applies to
the client.

R13 is the constraint that makes the second question sharp. It says the executable ships alone, and
ADR-024 amended it to let *a server* write one match store. With one binary in two roles, that
wording is ambiguous in a way somebody would eventually resolve the wrong way.

## Options considered

### Transport

**UDP.** Loses on this traffic, and not marginally:

- **A lost order is a lost turn**, discovered six hours later when the digest shows the player did
  nothing. Delivery is non-negotiable, so acknowledgements and retransmission would have to be
  built — and having built them, TCP would have been written again, worse.
- **Ordering matters because `Submit` replaces.** That is what makes "editable until the lock"
  work. A stale order set arriving after a newer one would silently overwrite a real turn.
- **A snapshot does not fit in a datagram.** Staying under ~1200 bytes to avoid IP fragmentation
  means chunking and reassembling every snapshot: more protocol, more to get wrong.
- **The connection is the presence signal.** `MarkPresent` asks whether the server saw a player
  between locks, and a TCP connection answers that for free. Presence feeds the custodian rule, so
  a worse answer makes custodians of people who were sitting there.

UDP's advantages — no head-of-line blocking, no handshake — matter when you send many small updates
a second and a dropped packet beats a late one. At four messages a day a handshake is free and
there is no "late": the tick is six hours wide.

**TCP.** Reliable, ordered, streamed. Everything above, for nothing.

### Process shape

**Two executables**, a server and a client. Cleanest separation and it changes the repository map,
splits R13 cleanly, and leaves the local path — the one every developer runs — bypassing the
network entirely.

**One executable, direct calls locally, sockets remotely.** What Step 2 built. It has two code
paths where the one that matters is the one nobody runs until six people are waiting.

**One executable, three roles, TCP in all of them.**

## Decision

**TCP, and one executable with three roles.**

| Role | How | What it does |
|---|---|---|
| host and play | default | Server on a background thread, then a client that connects to `127.0.0.1` |
| join | `--join <host[:port]>` | Client only |
| dedicated | `--serve [port]` | Server only, no window — also the headless runner |

**THE CLIENT TALKS TCP EVEN WHEN THE SERVER IS ON THE NEXT THREAD.** That is the decision that
earns its keep. There is no local shortcut, so the host's own client is a socket client like
everybody else and **every single launch exercises the transport**. The alternative buys a few
microseconds and costs the only regular testing the network would ever get.

**Threading is one sentence: the session lives on the server thread and nothing else touches it.**
The client shares no state with the server, only a socket. No lock anywhere near the simulation,
resolution stays single-threaded, determinism is untouched.

**The wire is a 32-bit little-endian length and then that many bytes.** Nothing else in the
envelope: the kind is the payload's first byte, TCP already checksums, and a version belongs to the
payload. `FrameStream` does the reassembly, because TCP is a stream and *"a send of 300 bytes can
arrive as 40 and 260"* is the thing people reimplement wrong.

**Six message kinds** — Hello, Welcome, Refused, Orders, State, Ping — and the list is short on
purpose. A protocol with room to grow is a protocol somebody grows.

**Every length off the wire is checked before it is believed**, at three layers: the frame length
against a one-megabyte cap, each blob against both a cap and what is actually left in the message,
and every decoder checks `AtEnd` so trailing bytes are as refused as missing ones. `ByteReader`
fills a short read with **zeros**, so a decoder that trusted a length would turn a truncated message
into a plausible wrong one.

**Nothing is accepted before a Hello**, and a client sending a server-only message is dropped.

**R13's amendment is re-worded from a binary to a role.** A process *acting as the server* writes a
match store; a process that is only a client never does. That is the honest distinction and it is
the one Step 2 tiptoed around by passing an empty store path.

## Consequences

**The transport is exercised constantly**, which is the whole point, and the loopback tests in
`NeuronServerTests` are real socket tests rather than fakes — they catch a listener that does not
listen and a non-blocking read that does not mean what this tree thinks it means.

**A thundering herd at every lock.** Every client wants to talk at the same instant. At twelve
players this is nothing; it is the first thing that would bite if this ever grew, and it is written
down here so the next person does not have to discover it.

**If the host closes their window, the match stops.** The store means a restart resumes it, but the
other five are disconnected until somebody restarts. For Phase 0 that is acceptable *if it is said
out loud*, which is what this paragraph is for.

**No encryption, and that is deliberate rather than forgotten.** Tokens cross the wire in the clear.
Phase 0 is six people who know each other on a known host. Schannel is in the Windows SDK so R14
would not forbid TLS later; pretending it was in scope now would be security theatre.

**`--serve` is also the headless runner.** A match resolving on a schedule with nobody watching,
which is what a Phase 0 dry run wants.

## What this changes elsewhere

`NeuronCore` gains `FrameStream`, `Protocol` and `Socket` — the wire protocol was always listed as
its business (AGENTS.md §2) and was the one part of the row that did not exist. `NeuronServer` gains
`MatchServer`. `Lockstep` gains `MatchConnection` and `HostedServer`, and its entry point
picks a role. AGENTS.md R13's amendment changes wording in the same commit. Nine projects, still.

## Open questions

**Reconnection.** A client that loses its connection stops. It should retry and be handed the
current snapshot — the server already sends one on Hello, so the missing half is the client's.

**What a reconnecting player is owed beyond the current snapshot.** The digests they missed,
probably: the one-pager's premise is that you open the app and read what changed, and somebody away
for three ticks has three digests waiting. Only the latest is kept today.

**A slow-loris connection.** `FrameStream::Pending()` exposes how much a peer is sitting on without
completing a frame, and nothing acts on it.
