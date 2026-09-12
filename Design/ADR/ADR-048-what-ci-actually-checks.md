# ADR-048 — What CI actually checks

**Status:** Accepted

**Date:** 2026-09-12
**Decided by:** Owner, on the codebase review's §4 items 6 and 9, 2026-09-12.

**Amends:** the 2026-09-09 owner decision that CI does not build Release. It still does not, except
for one suite, and the reason that changed is below.

---

## Context

Two gaps in what the gate was actually asserting.

**The fuzz tests asserted the absence of a crash and nothing else.** `NeuronCoreTests` and
`NeuronServerTests` feed the decoders truncated frames, a length field a peer chose, a message only
a server sends, and twelve clients sending pure noise. One of them says so in as many words:
*"Surviving is the assertion. What is being tested is the absence of a crash and of a read past the
end, neither of which a return value can express."* The first half was checked. **The second half was
not checked by anything** — a read a few bytes past a heap buffer is precisely the bug that passes.

**Release was never built by CI**, which was right when the two configurations differed only in
optimisation and there was nothing to disagree about. Since the scripted match's final hash was
pinned to a value computed under clang, there is.

## Decision

**Address Sanitizer on the two suites that are fed hostile input**, and not on the rest of the tree.
The game and the client are not fed hostile input, and the cost is real. Incremental linking goes
off with it, because ASan does not support it.

**Container annotations are off**, which a mixed build requires: ASan's annotations need every
object in the link to agree, and `NeuronCore.lib` is built without them because the executable and
the other three suites link it too. That gives up overflow detection *inside* a container's spare
capacity and keeps the check that matters here — a read past the allocation itself.

**A `Release|x64` job builds `GameLogicTests` alone and runs it.** It needs two libraries rather than
nine projects and runs in parallel with the Debug job, so the wall clock is unchanged. The rest of
the tree is still covered by the static alignment check in `CheckProjectFiles.py`, which is what
stands in for the build nobody runs.

## Verified

**ASan was proved live rather than assumed.** A deliberate read sixty-four bytes past a four-byte
allocation, run once and deleted: `AddressSanitizer: heap-buffer-overflow ... WireTests.cpp:254`. A
passing suite says nothing about whether a sanitizer is on, and `clang_rt.asan_dynamic-x86_64.dll`
appearing beside the output is evidence rather than proof.

All 434 tests pass with it on.

## Consequences

**The Debug gate is the sanitized gate.** There is no separate ASan run to remember to do; the fuzz
tests mean what their comments say on every push.

**A curiosity worth writing down**, because the next person to write a test in this tree will hit
it: `windows.h` defines `small` as `char`. A local named `small` produces *"'std::vector<uint8_t>'
followed by 'char' is illegal"*, which points at the line after the one that is wrong.

## Open questions

**No UndefinedBehaviorSanitizer**, because MSVC has none. The clang shim build the codebase review
used to verify `GameLogic` on Linux is where that would go, and it is outside the tree.
