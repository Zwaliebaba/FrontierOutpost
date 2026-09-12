# ADR-046 — The wire reaches both families

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, on the codebase review's §4 item 8, 2026-09-12.

---

## Context

`Socket::Listen` created an `AF_INET` socket and bound `INADDR_ANY`; `Socket::Connect` resolved with
`hints.ai_family = AF_INET`. The game was IPv4 only, in a year when a home connection is as likely to
hand out a v6 address as a v4 one.

## Decision

**One dual-stack listener.** The socket is `AF_INET6` with `IPV6_V6ONLY` turned off, so a v4 peer
arrives as `::ffff:a.b.c.d` and there is one accept loop rather than two. Windows defaults that
option to on, which is why it is set explicitly.

**`Connect` resolves `AF_UNSPEC`.** `getaddrinfo` returns the families in the order the system
prefers and the existing loop tries them in turn, so a host with both gets whichever answers.

**`Port()` reads the family it was given.** A `sockaddr_storage` and a check, because the port sits
at a different offset in `sockaddr_in` and `sockaddr_in6`, and a listener is now always v6 while a
socket from `Connect` is whichever the name resolved to. Reading a v6 socket through a
`sockaddr_in*` returned a number from the middle of an address.

**`host:port` is split with brackets.** `::1:7371` split on its first colon gives an empty host and a
port that does not parse, and the client then connected to nothing on the default port — silently,
because an address that does not resolve looks exactly like a server that is not running. `[::1]:7371`
is the URL form and the one somebody who has typed an IPv6 address before will reach for. An
unbracketed address with more than one colon is taken whole, as a host, because guessing a port out
of the last group of a v6 address would be worse than ignoring it.

One splitter, used by the command line and by the join screen's server field — they had two copies
with different bugs.

## Verified

One `--serve` listener; a client at `[::1]:7371` and a client at `127.0.0.1:7371`; both logged in,
as players 0 and 1, on the same socket.

## Open questions

**`getaddrinfo` is still synchronous** (ADR-043 left this too). It is instant for a literal of either
family and can block on a name server for a hostname.
