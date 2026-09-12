# ADR-049 — A record is described once

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, on the codebase review's §4 item 4, 2026-09-12.

---

## Context

Three wire formats were hand-mirrored: a `Write` and a `Read` per record, the same fields listed
twice, in the same order, by hand. The rules format stopped being one of them when
`MATCH_RULES_FIELDS` became a single list driving both directions and the field count; snapshots and
orders were still two lists apiece.

**The failure mode is not a compile error.** A field added to the writer and forgotten in the reader
decodes everything after it shifted by four bytes — a valid-looking record of nonsense. And a
round-trip test cannot see it: encoding and decoding with the same wrong code agrees with itself
perfectly. This tree has the scar already, in the rules format the review found: a count guard that
compared a constant with itself, and the constant was 34 for 38 fields written.

## Decision

**`Neuron::Archive` is a reader or a writer behind one interface.** Eight primitives and a
direction: `U8`, `U32`, `U64`, `I32`, `Boolean`, `Text`, `Identity` for an `Id<Tag>`, `Enumerator`,
and `Count`. Deliberately not a serialization framework — no versioning, no schema, no reflection.

**It is the one place a count is bounded and an enumerator is range-checked.** A declared count is a
number a peer chose and a `reserve` on it is an allocation failure waiting for a malformed record;
`Enumerator` delegates to `ByteReader::ReadEnum`, which is the idiom the tree already had, rather
than repeating the check a sixth time.

**`Visit` takes a mutable record even when writing**, and `Write` casts away const to call it. That
is the price of not having the list twice, and it is the whole trade: one description that cannot
drift, against one `const_cast` in a function that provably does not write.

## The byte layout did not move, and that is pinned

**The match store holds encoded order sets** (ADR-024): a match is its seed and the orders that were
locked, and it is reloaded by replaying them. So the order format is not merely a wire format — it
is the file format of every match in progress, and a change to it silently turns every stored match
into a different one.

`OrderWireFormatTests` pins it as a literal: **the exact bytes this tree produced on 2026-09-12**,
for an order set with one of everything in it. That test was written *before* the refactor and
passed unchanged after, which is the only evidence that means anything here — a round trip would
have agreed with itself either way.

The snapshot format is wire-only, both ends ship together, and the existing round trip plus the
fuzz tests cover it.

## Consequences

**`Orders.cpp` is 165 lines from 210; `Snapshot.cpp` is 436 from 521.** The saving is real but it is
not the point: what went is the *second* list.

**`GameLogic/Snapshot.h` now includes `Archive.h`.** That is a NeuronCore header and GameLogic
already references NeuronCore, so no edge changed.

**Two formats, not three.** `MatchRules` keeps its X-macro. It has a size tripwire and a field count
the macro derives, which the archive does not give it, and rewriting a format that already has one
list to use a different one-list mechanism would be churn.

## Open questions

**`MatchStore`'s own framing is still hand-mirrored**, as is `Protocol`. Both are short, flat, and
have a fuzz test each; neither has the repeated-record shape that made snapshots and orders worth
doing. They are the next candidates if a third bug of this kind turns up.
