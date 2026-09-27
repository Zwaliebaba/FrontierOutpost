# LTSL interpreter: performance, simplification and the path to AGENTS.md

- **Status:** Proposed, 2026-09-27. The review is complete. Six reviewers each took a lens, and a
  verifier per lens then tried to refute every finding; where a verdict and its finding disagree,
  this plan follows the verdict. The quick wins the owner allowed landed on this branch, one commit
  each (§3). Nothing in §4 to §7 is approved. The owner approves items by ID, and each approved
  item lands as its own PR (AGENTS.md §6), with its ADR in the same commit where the item is a
  decision. The next free ADR number is ADR-018.
- **Scope:** the LTSL interpreter in `NeuronCore/`:
  - the front end: `Tokenizer.h`, `StringList.*` and `LTSL.*`;
  - the compiler and the evaluator: `Expression.*`, one `Expression_*` node per file (`Access.cpp`
    to `While.cpp`), `Environment.h`, `Script.*`, `ScriptFunction.*` and `ScriptType.h`;
  - the reflection and binding layer: `Type.*`, `Data.h`, `Function.*`, `DeclareFunction.h`,
    `Function_Generated.h` and `AutoClass*.h`, with the containers and handles under them.

  The `ScriptApi*.cpp` files in NeuronCore, NeuronClient, GameLogic and FrontierOutpost consume the
  binding layer and are in scope only as consumers. The 121 scripts in `GameData/script/` are the
  ground truth of what the language must keep doing.
- **Files:** a C++ reference gives the file name alone, which is unique in the tree (ADR-014
  decision 5); a file outside NeuronCore carries its project where that helps. Scripts are under
  `GameData/script/`. Line numbers and counts are at `2fb3cde`, the review's base, unless a
  paragraph says otherwise; §3's commits moved lines in the files they touch and deleted 919 lines
  of C++.
- **The owner's decisions for this review:**
  - **Deliverable.** A ranked report, plus quick wins landed on this branch. A quick win is small,
    low-risk and high-confidence. It needs no ADR, changes nothing a script can observe, and lands
    inside the legacy files in their own style, so they keep their marker (ADR-015).
  - **Target.** A full AGENTS.md conversion of the LTSL layer: the naming table, no Legacy marker,
    clang-tidy and clang-format clean, and `/W4 /WX /fp:precise`. Larger findings are described in
    that form.
  - **Frozen.** The LTSL language, the script API and its semantics (ADR-013 decision 8). All 121
    scripts run unchanged with identical results. A latent bug is reported with a proposed fix, and
    a fix that changes behaviour is its own decision.
  - **Evidence.** Measured on Linux with clang, and labelled as such.

  After the review the owner answered four more questions (2026-09-27):
  - **Q1.** Behaviour that is undefined today is not frozen: fix it. New values start zeroed, `Vec`
    types included; the overload chosen among several gets its argument conversions; a switch
    case's predicate is converted to Bool.
  - **Q2.** A fix that turns a crash into defined behaviour is a quick win.
  - **Q3.** Hot reload gets the stat gate (size and last write time), one entry per dependency, and
    no second reload in `ShipType.cpp`. `AUTORELOAD` stays on; turning it off in Release was not
    chosen.
  - **Q4.** `DEBUG_POINTERS` becomes Debug-only.
- **Source:** a multi-agent review on 2026-09-27, at `2fb3cde`. The lenses are named by the IDs
  their findings carry: EVAL (the evaluator's hot path), COMPILE (the compiler and the load path),
  BIND (reflection and binding), SUPPORT (containers, strings and allocators), CONFORM (the
  conversion to AGENTS.md) and SAFETY (correctness). The findings and verdicts are kept outside the
  repository; this plan carries what they established. The interpreter files are unchanged between
  `2fb3cde` and `94c4b8a`, the base of this branch.
- **Bounds:**
  - The bar is identical results: the same compiled trees and the same values, byte for byte, by
    the proof set in §1. Script-visible means type, function and field names and aliases,
    operators, implicit conversions, which overload wins, evaluation order, lvalue aliasing,
    statics, what a script prints or returns, and, until §7 question 3 is answered, how many times
    a script type is constructed.
  - Every figure is clang on Linux. Nothing was built with MSVC; §9 lists what that leaves open.

## 1. How it was measured

Nothing here was built with MSVC: the environment that wrote this has none. Instead, the
repository's own sources were compiled with clang 18 (`-std=c++2b -O2 -g`) on Linux x86-64, 4
cores, into a harness that is not part of the repository:

- all of NeuronCore (122 files) and GameLogic (133);
- the 50 NeuronClient files and 48 FrontierOutpost files that compile on Linux, every script-API
  file among them;
- trap stubs for the 115 Direct3D 12 and XAudio2 symbols the rest would have supplied, which
  nothing on the load path calls. `RendererCore.cpp` is left out, because its static initializer
  creates a graphics device.

With those, **all 121 scripts compile with no failures**: 702 functions and 232 types. The harness
runs script functions, compiles the whole corpus, times the phases of a load, dumps every compiled
function and type, and dumps the registered native surface (1,056 functions and 391 types).

Costs are compared as callgrind instruction counts (Ir), which do not vary between runs. Times are
medians of 5 or 7 runs on an idle machine. **Every figure is clang on Linux, not MSVC on
Windows.** Relative costs carry over; absolute times do not, and the file-system costs of the load
path are far higher on Windows.

### The proof set

Every commit in §3 reproduced all of the following byte for byte, or changed exactly what its
message says it changes:

- **The structural AST dump** of all 121 scripts (`ast --all`, 171,670 lines). It walks every
  compiled function and script type through the engine's own reflection, so it sees each node's
  fields, the overload each call chose, and every conversion, switch, print, `ref` and `static`,
  which the emitted-text dump (`dump --all`, kept beside it) cannot.
- **The registry** (each native's signature; each type's size, alignment, base, aliases,
  conversions and methods), and **type creation order with GUIDs** (391 types at startup, 664
  after loading), since GUIDs reach script-visible item hashes (D24).
- **A fresh-process compile of each script** against its section of `ast --all`; two scripts
  compile differently alone today (D23).
- **The Bench results**, the exact bytes of the 11 `Bench/Lang` functions; **SAFETY's corpus**, 122
  entries pinning precedence, overload choice, conversions, evaluation order, lvalue aliasing,
  statics, script types, cross-script names and which statements are dropped; **EVAL's corpus**,
  construction counts and aliasing through the evaluator.
- **Construction counts through natives**: the BIND verifier's `VBIND/Count` (70 initializer runs
  through the natives that take or return `Data`) and `VBIND/Attach` (`Object.AddScript`: 5, then
  6), with `VEVAL/Box`, `VEVAL/IntText` and `VSUPPORT/Corpus` (`Split`, `ToInt`, literals).
- **Tokenizer edge cases**: LF, CR and CRLF endings, blank and space-only lines before a body, a
  literal spanning lines, escaped quotes, tabs, no final newline.
- **34 crash and undefined-behaviour probes**, each in its own process (one only prints an
  address); a fix changes them on purpose, and the change is reviewed, not failed. And the **load
  statistics**.

Performance is checked with the Ir of the 11 benchmarks, of one `loadall`, and of
`ScriptFunction_Load` on four already-loaded scripts.

Whether the harness should live in the repository, for example as `Tools/LtslHarness/`, is a
decision for the owner (§7 question 16): it is a second build of the engine's sources, for Linux,
beside the MSBuild one.

## 2. Baseline

### Run time

| Benchmark | Loop body | Iterations | Median | Per iteration |
|---|---|---|---|---|
| IntLoop | `sum += i` | 1,000,000 | 57.6 ms | 58 ns |
| WhileLoop | `i += 1` | 1,000,000 | 56.4 ms | 56 ns |
| FloatMath | `x = x * 0.5 + 1.0` | 1,000,000 | 121.1 ms | 121 ns |
| NativeCall | `sum += (Max i 7)` | 1,000,000 | 100.2 ms | 100 ns |
| ScriptCall | `sum = (Add sum 1)`, where `Add` is a script function | 1,000,000 | 98.6 ms | 99 ns |
| MethodCall | `c.Tick 0.5`, a script method that updates two fields | 1,000,000 | 127.1 ms | 127 ns |
| VecMath | `v = v + d * 0.5` on `Vec3` | 1,000,000 | 122.5 ms | 122 ns |
| Branch | an `if` and a three-way `switch` | 1,000,000 | 180.3 ms | 180 ns |
| StringOps | `var s "Field #" + i`, then `total += s.Length` | 100,000 | 38.7 ms | 387 ns |
| ListOps | `l += i`, then a loop to `l.Size` | 100,000 | 21.3 ms | 213 ns |
| StaticVar | `static k 3`, then `sum += k` | 1,000,000 | 64.1 ms | 64 ns |

A `for` iteration makes three native calls (the test, the body and the increment), so a native call
costs about 19 ns.

**Allocation dominates.** Every temporary, and every argument that is not an lvalue, is allocated
with `Type::Allocate()`, which is `new T` (`Type.h:386`), and freed after use
(`ExpressionFunctionCall.cpp:67,80`, `ExpressionCall.cpp:54,66`, `Block.cpp:56-58`).
`DataStack.h`, a bump allocator, was used by nothing (§3 deleted it).

| Benchmark | Instructions | `malloc` and `free` | Native-call node | Other notable |
|---|---|---|---|---|
| IntLoop | 576 M | 24% | 41% | the block node 11%, the variable node 6% |
| ScriptCall | 973 M | 29% | 24% | the script-call node 14% |
| MethodCall | 1,298 M | 32% | 25% | the script-call node 10% |
| VecMath | 1,287 M | 44% | 25% | the implicit-conversion node 1.2% |
| Branch | 1,895 M | 41% | 30% | the switch node 2.6% |
| StringOps | 376 M | 23% | 7% | each `Int` to `String` conversion builds a `std::stringstream` and a locale, about 20% |

### Load

- Compiling all 121 scripts takes **53.8 ms** (median of 7), 307 M Ir. Summed over the scripts,
  reading the files takes 1.6 ms, parsing 8.5 ms and the infix rewrite (`LTSL_ApplyRewrites`)
  5.5 ms, which leaves about 38 ms for the compiler.
- A load opens each script file **twice**, for the hash read and for the parse read, and stats it
  five times: 242 opens and 605 stats for the 121 scripts (strace). Two stats belong to the reads,
  two to the resource-map walk at startup and one to the harness's own directory walk;
  `LocationResource::Exists` is a map lookup. (The first version of this plan said 1,005 opens: it
  counted stats as opens.)
- **`ScriptFunction_Load` re-reads the scripts on every call.** `AUTORELOAD` (`Script.cpp:17`,
  `:188-190`) makes every call run `Script_Reload`, which reads and hashes the script and its whole
  dependency closure again, even when nothing has changed. On an already-loaded script, one call
  costs 9.9 µs for `Object/Ship:Init`, 46 µs for `Icons:Station`, 100 µs for `Object/System:Init`
  and 276 µs for `Object/Colony:Init`, on Linux with a warm cache; in instructions, 11,395 Ir for
  `Object/Thruster:Init` and 1,164,917 for `Object/Colony:Init`, whose closure holds 34 scripts.
  - GameLogic makes such a call for each object it creates: `Ship.cpp:92`, `Colony.cpp:12`,
    `Thruster.cpp:12`, `Region.cpp:153`, `StationType.cpp:42`, `PlanetType.cpp:21`,
    `ScannerType.cpp:43`, `WarpNode.cpp:441`, `ShipType.cpp:215-216` and the other `*Type.cpp`
    generators: 1,351 calls to build ltheory's universe, 443 for war's objects (COMPILE-1, run
    headless).
  - Two make it per frame: `Wormhole.cpp:60` and `Player.cpp:20` load their icon on every
    `GetIcon`, which `Widget/HUD/WorldObject.lts:107` calls every frame for each marker while the G
    layer or the map is shown. `WarpNode.cpp:237` and `:288` keep theirs in a static.

Measuring this baseline found three defects, which the findings call defects 1 to 3. Fields and
locals declared without a value started uninitialized, and each method definition left a null entry
in its script's function table: §3 fixed both. The stack-frame scopes are compiled into Release
(L2).

## 3. What landed on this branch

Thirty-six commits, each one change, written in the legacy files' own style so that every file keeps
its Legacy marker (ADR-015), and each proved on its own with the proof set of §1 before the next one
began. None was built with MSVC (§9). A "Measured" figure is callgrind Ir on the Linux harness,
against the commit before; the other commits were not measured for speed.

### The cumulative effect

Callgrind Ir, the branch's base (`94c4b8a`) against its head. The benchmarks run one script function
a million times (StringOps and ListOps 100,000); `loadall` compiles the 121 scripts once; the
lookups are one `ScriptFunction_Load` on a script that is already loaded, the hot-reload check
included.

| Measure | Base | Head | Change |
|---|---|---|---|
| IntLoop | 576.0 M | 516.7 M | −10.3% |
| WhileLoop | 609.0 M | 578.7 M | −5.0% |
| FloatMath | 1,250.0 M | 1,190.7 M | −4.7% |
| NativeCall | 1,034.0 M | 972.7 M | −5.9% |
| ScriptCall | 973.0 M | 881.7 M | −9.4% |
| MethodCall | 1,298.0 M | 1,195.7 M | −7.9% |
| VecMath | 1,287.0 M | 1,219.7 M | −5.2% |
| Branch | 1,895.5 M | 1,799.2 M | −5.1% |
| StringOps | 376.1 M | 220.1 M | −41.5% |
| ListOps | 223.9 M | 209.8 M | −6.3% |
| StaticVar | 640.0 M | 589.7 M | −7.9% |
| `loadall` | 307.4 M | 181.4 M | **−41.0%** |
| Lookup, `Object/Thruster:Init` | 11,395 | 5,933 | −47.9% |
| Lookup, `Object/Ship:Init` | 30,384 | 5,920 | −80.5% |
| Lookup, `Icons:Wormhole` | 330,906 | 5,802 | −98.2% |
| Lookup, `Object/Colony:Init` | 1,164,917 | 158,198 | −86.4% |

In wall-clock time, with the base and head builds run alternately on the same machine, a full load
takes 34.4 ms against 56.5 ms (−39%; the median of three runs of seven), and ScriptCall, Branch and
StringOps take 85, 164 and 21 ms against 98, 185 and 42 ms. Those times are noisy; the Ir are not.

Run time gains come from the register fast path, the Debug-only null checks and, for StringOps, the
integer text. Load gains come from the front end (reading each script once, one pass per line in the
tokenizer, no infix rewrite for a list without an operator) and from lookups that no longer insert.
Most of the lookups' gain is the stat gate: a call that finds every script of its closure unchanged
no longer reads or hashes them.

### Load

| Commit | Change | Findings | Measured |
|---|---|---|---|
| `d90a3f5` | Name lookups (`Function_Find`, `Type_Find`, `CompileEnvironment::Contains`, the script cache) stop inserting an empty entry on a miss | COMPILE-8, SUPPORT-5, BIND-11 | `loadall` −2.5% |
| `b1d4b57` | `StringListT::GetValue` returns `String const&` | COMPILE-4 | −2.9% |
| `8197850` | Each script is read once per load instead of twice, and `ReadAscii` copies it in bulk | COMPILE-9, SUPPORT-1 | −4.3%; lookups −49% to −70% for Ship, Wormhole and Colony |
| `dd490b6` | The tokenizer reads each line once | COMPILE-2 | −13.7% |
| `cc1c8f3` | The infix rewrite skips a list with no operator in it | COMPILE-5 | −4.6% |
| `fa8b0fa` | `String_Split` finds separators instead of building a `std::stringstream` | COMPILE-6, SUPPORT-4 | −4.3%; lookups −10% to −15% for Thruster and Ship |
| `4298b86` | Integer literals parse with `std::from_chars`; the stream stays the fallback | COMPILE-12, SUPPORT-4 | −2.1% |
| `182f6e1` | The compiler dispatches its keywords on their first character | COMPILE-7 | −3.8% |
| `e19dba8` | Overload resolution asks whether a conversion exists instead of building one | COMPILE-10 | −2.3% |
| `c9ad0ff` | One entry per dependency; `ShipType.cpp`'s second reload removed (Q3) | COMPILE-1 | Colony's lookup −1.3% |
| `b924ab2` | The stat gate (Q3): a script whose size and last write time are unchanged is not reread | COMPILE-1 | lookups −32% to −94% |
| `2e0e5ec` | The global function and type tables are hashed, with transparent lookup (`78acec1` includes `<functional>` for them) | SUPPORT-5, BIND-11 | −5.0% |
| `1c1d1a5` | Move construction and assignment for `Reference<T>` (the converting move included) and `Type`; `Vector::append` and `push` for rvalues; `String(std::string&&)` | SUPPORT-8 (L10) | −4.2%; lookups −4% to −7%; StringOps −1.2% |

The stat gate reads `std::filesystem::file_size` and `last_write_time` and keeps the stamps beside
the script cache. An edit that leaves both the size and the write time unchanged is not seen until
one of them changes; the reload driver confirms that a new size or a new time reloads and that an
edit changing neither does not. Whether two saves inside one tick of the Windows file time can happen is
unmeasured (§9).

### Run time

| Commit | Change | Findings | Measured |
|---|---|---|---|
| `b39bd7f` | Integers become text through `std::to_chars` (short to `uint64`; not `char` or floats) | EVAL-3, SUPPORT-7 | StringOps −38.9% |
| `a9b80ac` | A native call's argument that is a plain variable takes its address without a virtual call | EVAL-6 | IntLoop −6.6%, ScriptCall −3.9%, Branch −3.1% |
| `d1a20bb` | `DEBUG_POINTERS` is defined only with `_DEBUG` (Q4) | EVAL-8 | ScriptCall −5.8%, IntLoop −3.7%, Branch −2.6% |

With `d1a20bb`, a script that dereferences a null handle in Release no longer gets the engine's
error: it takes the access violation the check stood in front of. That is Q4's decision; Debug keeps
the check.

### Crashes that became defined behaviour (Q2)

| Commit | Change | Findings |
|---|---|---|
| `41c3e1e` | Each native call takes its argument slots from its own stack frame, not from the node, so re-entering the node no longer frees the outer call's arguments (`loadall` −0.8%) | EVAL-2, SAFETY-2 |
| `c215aef` | A conversion from a `Data` rvalue reports no l-value instead of dereferencing null | EVAL-7, SAFETY-9 (the null check) |
| `8446be4` | `set` refuses a location that is not an l-value, at compile time | SAFETY-10 |
| `65a9548` | A `for` that fails to compile gives back the registers it reserved | SAFETY-8 |
| `c3e5d65` | Asking whether a local is constant after its block has closed no longer aborts the load | SAFETY-11 |
| `b74d152` | A void `switch`'s default branch evaluates into a temporary, not through null | EVAL verifier |
| `cc76cea` | `Pop` keeps an emptied array's element type | SAFETY-17 (D9) |
| `2474882` | The field mappers of six containers find nothing for a null value | SAFETY verifier (D1) |

### Undefined behaviour that became defined (Q1)

| Commit | Change | Findings |
|---|---|---|
| `b75a075` | A `switch` case's predicate is converted to Bool; a case whose predicate cannot be is dropped with a message | SAFETY-6 |
| `bd2f66e` | The overload chosen among several gets its argument conversions | SAFETY-1 |
| `a5afc72` | Every new value starts zeroed, `Vec` types included, before its constructor runs; over-aligned types get aligned storage | SAFETY-3, defect 1 |
| `67a55a9` | A type's alignment is `alignof`, not an offset taken through a null pointer | BIND-5, SUPPORT-9, SAFETY-14 |
| `d3d00cf` | `~TypeT` is virtual, and `FunctionImpl` and `GetAux` are gone, so the registry's entries die as what they are | BIND-8 (D5) |
| `5a4bf53` | `LocationFile::Read` allocates the `uchar[]` its `Array` deletes | SUPPORT-13 (D3) |
| `4531551` | Script-type fields and sizes round up to their alignment | SAFETY-7, SUPPORT-9 (D8) |
| `52162b7` | A handle's copy assignment reads its source before it releases the old object | SUPPORT verifier (D6) |

The zeroing costs 0.1% to 0.8% of the benchmarks' instructions, measured when it landed.

### Simplification

| Commit | Change | Findings |
|---|---|---|
| `c975998` | Defining a function no longer leaves a null entry in the script's function table (311 before) | SAFETY-12 (D20) |
| `bea0c63` | Eleven files nothing includes are deleted: the binding helpers `Call.h`, `FunctionCall.h`, `FunctionCast.h` and `MemberFn.h`, `Package.*`, the allocators `DataStack.h`, `StackAlloc.h` and `IntrusiveList.h`, and `NeuronClient/LTE.h` with `InternalList.h` | EVAL-13, SUPPORT-13, BIND-13 (L14) |
| `b947a41` | 26 binding macros, `AUTOMATIC_REFLECTION_PARAMETRIC2`, `REGISTER_TYPE`, `offset_of`, `DereferenceT` with its trait, the `castInt` hook and its members, the unreferenced cast templates, and the const `operator[]` of `Map` and `VectorMap` are deleted | BIND-13, SUPPORT-13 (L14) |

Together 919 lines of C++. No native was removed, so ADR-013 decision 8 holds, and the type creation
order and GUIDs did not move. `CheckProjectFiles.py` reports no fault after each.

### What changed on purpose

Every other golden stayed byte-identical through every commit. These changed, each reviewed and
accepted in its commit:

- **The compiled trees.** `bd2f66e` adds exactly 10 conversion nodes, at the 10 calls where more
  than one overload matched (`App/market`, `App/objectinfo`, `Object/Firework` twice, `Object/System`,
  `Widget/Market/MidPanel` twice, `Widget/Market/Transaction` three times); the values are unchanged,
  since those conversions are `int64` to `int` and `Color` to `V3F`. `4531551` changes 23 type sizes
  (`App` 36 to 40, `Button` 58 to 56, `HandlingScript` 152 to 160, ...), 12 field offsets and 39
  access offsets; with sizes and offsets masked, every tree, the text dump, the fresh-process compiles
  and the type list are identical, and the type creation order and GUIDs do not move.
- **The fresh-process compile.** `Widget/Market/Transaction` alone compiles instead of exiting with
  SIGSEGV (`2474882`), without its `switch buying` statement (D23).
- **The load statistics.** 0 null function entries instead of 311 (`c975998`).
- **The Bench corpus.** `MethodCall` read an uninitialized field; since `a5afc72` it returns 1,000,000
  and joined the byte-exact corpus.
- **The probes.** 21 of the 34 changed: 15 crashes became values, and 6 undefined results became
  the defined ones.

| Probe | Before | After | Commit |
|---|---|---|---|
| `ReenterNativeNode`, `SendTemp`, `ArgSlot`, `Reentry` | crash | 2, 7, 7, 7 | `41c3e1e` |
| `NonLValueDataToScriptFn`, `ScriptArg`, `PrintArg` | crash | 42, 42, 0 | `c215aef` |
| `AssignToNonLValue` | crash | 1 (the statement is refused) | `8446be4` |
| `ForFailLeaksRegister` | crash | 22 | `65a9548` |
| `SwitchCaseLocal` | crash | 5 | `c3e5d65` |
| `SwitchDefault:DefaultTaken` | crash | 5 | `b74d152` |
| `SwitchIntPredicate` | crash | 1 | `b75a075` |
| `UnconvertedIntClamp`, `UnconvertedDoubleAsFloat`, `NativeArg`, `NativeArg2` | 50, 0, garbage, garbage | 10, 5, 41, 41 | `bd2f66e` |
| `UninitLocal`, `UninitField` | garbage | 0, 0 | `a5afc72` |
| `ArrayEmptiedThenAppend` | crash | 1 | `cc76cea` |
| `VecTypo`, `ListTypo` | crash | 7, 7 (the statement is dropped) | `2474882` |

`4531551` changes no probe's value: `MisalignedField` returns 12345 both ways. Under UBSan (clang's
objects linked with gcc's libubsan, clang's runtime being absent here) it reports a misaligned
constructor call, load and store before and nothing after. Under memcheck, a full load through exit
is clean after `d3d00cf`, with 230 types destroyed through the new virtual destructor (counted with
gdb).

### What this branch still needs

Before it merges: CI's Debug|x64 build and tests, a Release|x64 and an ARM64 build by hand (the
changed headers are in every project), and the game launched, since none of this ran on Windows.
The items to watch there: `d1a20bb` changes what Release does on a null handle; `b924ab2` depends
on Windows write times; `a5afc72` adds `<new>`'s aligned allocation to `Type.h`; and `1c1d1a5`
changes which overload every temporary handle picks (L10).

## 4. Items for the owner, ranked

Everything proposed that did not land is grouped into fifteen items, each with its finding IDs as
aliases, ranked by value: per-frame run time, then load, then simplification, then conformance.
"Measured" means callgrind Ir on the Linux harness; "estimated" names its method. The per-frame
ranks are provisional until the Windows per-frame counts of §9 exist. L10 and L14 landed on this
branch (§3); their IDs stay, so that references to them hold.

| ID | Item | Aliases | What it buys | Effort | ADR or owner |
|---|---|---|---|---|---|
| L1 | An evaluation stack for temporaries and locals | EVAL-1, EVAL-4 (a), (c) | −18% to −49% of script Ir (measured); an empty C++→script call 358 → 211 Ir (measured) | M | R15 ADR |
| L2 | Script-call scopes and `BUILD_DEBUG` | defect 3, EVAL-4 (b), COMPILE-16, SAFETY-19 | 152 Ir per C++→script call, 1.5% of load (measured); ~113 Ir of it with no text changed (estimated) | S | owner |
| L3 | Trivial types without indirect calls | EVAL-9 | 5-8% of script Ir after L1 (estimated from measured shares) | M | — |
| L4 | The HUD's per-frame rebuilds | EVAL-10 | `Sockets` alone: 129k Ir per frame (measured shape); saving not estimated | M | owner |
| L5 | `Data`, boxing and returned values | BIND-6, BIND-10, BIND-15, EVAL-12 | `List_Get` 400 → 222 Ir, −11% on a list-reading loop (measured); ~100-130 Ir per boxing (estimated) | S-M | owner |
| L6 | Hot reload after the stat gate | COMPILE-1 (a), (b) | ~11 M Ir and ~5,900 system calls left at ltheory's startup (measured, Linux) | S-M | owner; ADR for (b) |
| L7 | The front end and the compiler's resolver | COMPILE-3, -13, -14 | up to −24 M Ir, 7.6% of load (measured upper bound); no 2^depth worst case | L | ADR-018 |
| L8 | Float text and float literals | EVAL-3, SUPPORT-4, SUPPORT-7, COMPILE-12 | 6.4 M Ir, 2.1% of load (measured cost); ~1,400 Ir per float printed (estimated) | S | MSVC proof |
| L9 | The pool allocator under R15 | SUPPORT-6, CONFORM-11 | keeps the 8.7% of load plain `new` and `delete` would add (measured, glibc) | S | R15 ADR |
| L10 | Moves for handles and strings: **landed** (§3) | SUPPORT-8 | see §3 | — | — |
| L11 | Explicit names in registration, and the surface contract | BIND-1, CONFORM-2 | every later rename without touching a script | M | ADR-018 |
| L12 | The binding layer as templates | BIND-2, -7, -8, -12, -14 | −4,400 generated lines (counted); `Type` refcounts, 1.6% of load (measured share) | L | ADR-018; owner for BIND-7 |
| L13 | Registration without static constructors | BIND-9 | `/WHOLEARCHIVE` dropped; aliases independent of link order | M | supersedes ADR-014 decision 3 |
| L14 | Dead code: **landed** (§3) | EVAL-13, SUPPORT-13, BIND-13 | see §3 | — | — |
| L15 | The support layer in conformant form | SUPPORT-10, -11, -12, -14 | `std::expected` usable; conformance | L | ADR-018 |

### L1. An evaluation stack for temporaries and locals

**Change.** `Environment::Allocate` and `Free` (`Environment.h:19-25`) take every temporary,
argument and local from one per-thread LIFO stack instead of `new T` and `delete`. Push zero-fills
and constructs where the allocation did, as `Allocate` has since §3; Pop destructs where the
`delete` was; a full stack falls back to the heap. The register file becomes the thread's too, each
`Environment` truncating it to its base on exit (EVAL-4 (a)), and `VoidCall` takes its discarded
result from the stack (EVAL-4 (c)). A later step lays each function's temporaries out at compile
time. The conformant form is a new file, `EvaluationStack.h`, with a 64 KiB `CAPACITY_BYTES`.

**Why.** Allocation is 24-44% of the benchmarks' instructions (§2). Sizes and alignments are known
and lifetimes are strictly LIFO: blocks and calls free in reverse, LTSL has no `return` or `break`,
and a native that calls back into a script nests.

**Evidence** (CONFIRMED). Built with EVAL-2's fix (now in §3), the stack cut the inclusive Ir of
`ScriptFunctionT::Call`, against `2fb3cde`, by 21.6% in IntLoop, 19.6% in StaticVar, 40.2% in
FloatMath, 39.9% in VecMath, 36.3% in Branch and 47.4% in the per-frame loop of
`Object/WarpNode.lts:102-114`, as the verifier measured it; the reviewer's range is −18% to −49%. It
saves 116-138 Ir per temporary. In bench-full a `CaptureFocus`-style hook fell from 4,588 to 2,529
Ir per call, and `Sockets.CreateChildren` from 127.6k to 108.9k Ir per frame. Per frame, estimated
from static counts times these shapes: about 340k Ir per visible warp node in ltheory, 35k for war's
33 ship hooks, 0.15-0.3 M in the HUD. The verifier's checker, freed bytes poisoned and capacities
from 64 bytes to 1 MiB, found 0 LIFO violations in every run, the 84 shipped functions that run
headless included; no shipped `address` or `ref` points at an evaluator value, and no native keeps
an argument's address. §3 has since removed other per-node costs, so re-measure on the branch.

**Proof.** The proof set of §1, construction counts above all; a Debug-only shadow stack asserting
LIFO order; memcheck. Every value sits on a 16-byte base, which covers every alignment in the
tree, script types included now that §3 lays their fields out aligned (D8). On Windows, a PIX or ETW sample of ltheory and war turns these Ir into milliseconds.
**Rules.** R15 needs an ADR (§7 question 1), and the legacy exemption does not cover R15. The new
file follows §1 and §2 and is registered in `NeuronCore.vcxproj` and its `.filters`.

### L2. Script-call scopes and `BUILD_DEBUG`

**Change.** `BuildMode.h:4-8` defines `BUILD_DEBUG` unconditionally, so `FRAME`, `SFRAME` and
`AUTO_FRAME`, each a `StackFrame_Push`/`Pop` and a `Profiler_Push`/`Pop`, run in Release: in
`ScriptFunctionT::Call` (`ScriptFunction.cpp:36`), every `Expression_Compile` (`Expression.cpp:26`)
and every initialized field of a script-type construction (`Expression.cpp:16`). The options:
(1) keep them; (2) make `BUILD_DEBUG` follow the configuration; (3) keep `StackFrame`, and run the
profiler's push and pop only while it is active, the profiler rebuilding its stack from
`StackFrame`'s frames when it starts.

**Why.** `FRAME` costs 152 Ir per C++→script call, 72 of it in `Profiler_Push` (measured, EVAL-4).
At about 200-300 such calls per frame (estimated from the hooks the HUD and the scripted objects
define), that is 30-45k Ir per frame. At load, `SFRAME` is 4.56 M Ir, 1.5% (measured, COMPILE-16).
Both CONFIRMED. Option 3 saves about 113 of the 152 Ir (estimated from the measured call tree).

**What depends on it** (SAFETY-19, CONFIRMED). Option 2 changes printed text and failures:
`Log_Error`'s prefix (`ProgramLog.cpp:44-66`;
`[Error] (Widget/Settings.Compile Expression) Function 'Create' already exists` loses its
parentheses), `Log_Critical`, which asserts under `BUILD_DEBUG` and closes the window under
`BUILD_RELEASE`, crash traces, and the F2 profiler's attribution. The assert dialog's CONTINUE path
(`Common.cpp:60-78`) is live in Release today for the same reason. Option 3 changes no text.
**Proof.** The proof set; the `[Error]` line of a failing compile; the F2 profiler on Windows. D4 is
in the same file and belongs in the same PR. **Rules.** §3 allows a path keyed on `_DEBUG`;
Release's diagnostics are the owner's (§7 question 2).

### L3. Trivial types without indirect calls

**Change.** A flag on `TypeT`, set from `std::is_trivially_copyable_v` and
`std::is_trivially_destructible_v`. The evaluator then skips the destructor and copies a fixed
size, and skips the constructor too when the type is trivially default-constructible, where
zero-fill already gives its value (EVAL-9, PLAUSIBLE).

**Why.** After L1, `__type_default_assign<int>` is 2.73% of Branch, `construct<int>` and
`destruct<int>` 1.82% each: indirect calls to a no-op or a 4-16 byte copy (`Type.h:385-417`;
measured shares). The 5-8% gain is estimated from them; nobody built it.

**Proof.** The proof set; the corpora would show a skipped constructor with a side effect. `V2T`,
`V3T` and `V4T` have user-provided constructors and keep the call. **Rules.** Every reflection
macro (`Type.h`, `BaseType.h`); after L1.

### L4. The HUD's per-frame rebuilds

**Change.** None yet. In every `PreUpdate`, `WidgetDynamic.cpp:21-56` calls the script's
`CreateChildren`, then `GetHash` on each new child and on every child, and keeps only new hashes.
The HUD's `Sockets`, `Targets`, `Log` and `WorldObjects` use it, as do the map, the observatory,
cargo, assets and the market's middle panel. The options: rebuild on a message, or when a cheap key
changes.

**Why.** Each frame builds and discards 8 socket subtrees with their components, boxed values and
list appends, plus a badge per target and a row per log entry. `Sockets` alone is 129.1k Ir per
frame (measured shape, EVAL-10, CONFIRMED), mostly C++ widget construction; L1 cuts 14.7% of it.
`Targets` and `Log` were not measured.

**Proof.** When `CreateChildren` runs is script-visible, so the owner decides (§7 question 4), after
per-frame counters in ltheory and war size it. **Rules.** NeuronClient's widget code.

### L5. `Data`, boxing and returned values

**Change.** Four options, each bounded by one rule, that a script type is constructed as often as
today: (a) move operations on `Data`, 18 lines (BIND-6); (b) thunks that construct their result in
the caller's storage, for every return type but `Data`, once L1 provides uninitialized storage
(BIND-15); (c) a small-buffer `Data`, 32 bytes inline, copying through the same hooks as often as
today (BIND-10); (d) boxing straight into the payload (EVAL-12).

**Why.** Boxing an `Int` into a `Data` parameter costs 330 Ir and freeing it 124. `List_Get` is 400
Ir, because the native's `Data` is copy-assigned into the caller's and destroyed; with (a) it is
222, and a list-reading loop is 11.1% cheaper. A `String` result costs 58 Ir of thunk overhead. All
measured; (c)'s 100-130 Ir per boxing is estimated, and per-frame counts need the game.

**The constraint.** A boxing constructs the script type twice, a parenthesised `(T)` runs every
initializer twice, and a `Data` copy constructs (EVAL-12, PLAUSIBLE, counts as its verifier
corrected them). With (a), `VBIND/Count` falls from 70 to 66: one construction fewer per call of the
five natives returning `Data` (`List_Get`, `List_GetRandom`, `Item_GetMetatype`, `Data_LoadFrom`,
`Data_None`) and per element when a `Vector<Data>` grows (`DrawState.cpp:18`). Hence BIND-6 and
BIND-15 were REFUTED as neutral. The count reaches the RNG and the GPU (D27), though no shipped
`List_Get` site holds a type with initializers. L10's moves, which landed, leave `VBIND/Attach` at 5
and 6; together with (a) the second `AddScript` on an object constructs 5 times instead of 6.

**Proof.** `VBIND/Count`, `VBIND/Attach`, `VEVAL/Box` and EVAL's corpus unchanged, or changed only
as the owner approves (§7 question 3).

### L6. Hot reload after the stat gate

**Change.** §3 landed Q3, but every call still walks the script's closure and checks each script in
it. Beyond that: (a) check each script at most once per frame, through a generation the main loop
bumps, and cache each closure; (b) watch `GameData/script` with `ReadDirectoryChangesW`, so
`ScriptFunction_Load` becomes a lookup. COMPILE-1's (c), `AUTORELOAD` off in Release, was not
chosen.

**Why.** The gate that landed reads `std::filesystem::file_size` and `last_write_time`, two stats
per script, and keeps the stamps in a table in `Script.cpp`. On this branch a call still costs
about 5,900 Ir for a one-script closure and 158,000 for `Object/Colony:Init` (measured, §3). In
the COMPILE verifier's build, which had the same gate, ltheory's universe spends about 11 M Ir in
these checks (90 M before), and its run makes 5,853 system calls, 4,326 of them stats (15,841
before). NTFS and Defender were not measured.

**Proof.** Hot reload is script-observable (D29), so any change to when an edit is noticed is the
owner's (§7 question 5): the reload driver, strace, and µs per call on Windows. **Rules.** (a) is
legacy style plus a per-frame call from the executable; (b) puts Win32 file watching into the
shared engine, a subsystem with an ADR. D2 and D28 go with this item.

### L7. The front end and the compiler's resolver

**Change.** The compiler tries each node kind in turn (`Expression.cpp:28-60,108-139`), and some
alternatives compile the arguments before failing. The end state, in new conformant files: a
`Lexer` yielding `std::string_view` tokens and a `Parser` building one `SyntaxTree` per script,
applying the dot and infix rewrites as it builds (COMPILE-14); a `Resolver` classifying each atom
once through a symbol table and dispatching a list on its head's kind (COMPILE-3); a per-script
cache of cross-script names (COMPILE-13). It keeps today's precedence: for an atom Variable,
Reference, FunctionCall, ExpressionCall, Constructor, Constant; for a list FunctionCall,
ExpressionCall, Access, Dereference, Constructor, Conversion, the single element.

**Why.** 30,053 compiles serve 27,036 parse nodes, and a dotted chain doubles its work per dot: 36,
263 and 4,107 compiles for one expression at depth 3, 6 and 10 (the corpus's deepest is 4). An
oracle replaying each node's winner saves a further 24.1 M Ir, 7.6% of the original load, on top of
the quick wins: a measured upper bound (COMPILE-3, CONFIRMED, oracle not re-run). The lexer's
"halve what remains" and the name cache's "a few M Ir" are estimates (PLAUSIBLE).

**What it must keep** (the SAFETY verifier's list). A failed statement is omitted and the rest stay,
a block's value is its last survivor's, and only the first list form per function that fails every
alternative prints, after re-running its compile. One pass: calls to later functions and
self-recursion are dropped. Overloads that tie fail (`1 + 2.5`, `2 ^ 3`). A type name resolves
through `Script:Type`, the script's own types so far, then one global map where the last
registration wins, and loads made by failed alternatives count (D23). Natives registered while a
call's arguments compile (`Vector<T>::Size`, `Type_Array`'s methods) are candidates only for later
calls; COMPILE-3's intermediate step, compiling each argument once for every alternative, changes
that, and with it what compiles (D1).

**Proof.** `ast --all`, the fresh-process compile, SAFETY's corpus, the parse dump, and the
tokenizer's fuzzer, since the corpus has no tab, multi-line literal or `\r`. **Rules.** New files
under ADR-018; it replaces cluster C1 (§6).

### L8. Float text and float literals

**Change.** Floats to `String` through `std::to_chars(first, last, value,
std::chars_format::general, 4)`, specified as `%.4g`, which is what the stream's `precision(4)`
prints; non-finite values stay on the stream. Float literals through `std::from_chars`, with a
leading `+`, overflow and letters left to the stream. §3 did the same for integers only.

**Why.** Float literals are 2,411 stream parses, 6.38 M Ir, 2.1% of `loadall` (measured). A float
printed through the stream costs about what an integer did before §3, some 1,400 Ir more than
`to_chars` (estimated from the integer case). The HUD prints floats
(`Widget/HUD/WorldObject.lts:114`).

**Proof.** 3,000,000 random float bit patterns format identically on libstdc++. MSVC's
`num_get<float>` and the UCRT's printf may round otherwise, so it lands only after an MSVC run of
SUPPORT's differential programs and of every numeric literal in the 121 scripts, bitwise, finds no
difference; and it is re-run after any `/fp` change. The same parser implements the `ToFloat`
native (`ScriptApiString.cpp:150`), so the proof covers its inputs too. **Rules.** Legacy files.

### L9. The pool allocator under R15

**Change.** Record `POOLED_TYPE` (`Pool.h:7-112`: per-class free lists in arenas of 32) in an ADR,
and fix two latent defects with it: `operator new` ignores the requested size, so a class derived
from a pooled one would overflow its block, and blocks are aligned for a pointer only. Neither case
exists today. The placement `operator new` is D14, a fix of its own.

**Why.** 104 classes are pooled: 41 in NeuronCore (27 expression nodes, 9 glyph nodes, 3 locations,
2 string-list nodes), 55 in GameLogic, 6 in NeuronClient, 2 in FrontierOutpost. Emptying
`POOLED_TYPE` adds 26.6 M Ir to `loadall`, +8.7% (measured by the verifier on the baseline; the
reviewer's +10.6% had a cheaper base). R15 needs an ADR, and the legacy exemption does not cover it.

**Proof.** Allocation only: every dump is identical either way. The ADR needs what was not measured:
the NT heap, and the per-frame cost of the 55 GameLogic classes made in play (`Missile`,
`Explosion`, `Pulse`, `Trail`, the tasks). **Rules.** R15 (§7 question 8); the pool is
single-threaded.

### L10. Moves for handles and strings: landed

§3 records it (SUPPORT-8, CONFIRMED). What stays open is the verifier's condition: it is in the
four headers every file includes, so it wants an MSVC Release build as well as CI's Debug one, and
the MSVC-only files that see those headers (`FrontierOutpost/Main.cpp`, the two shader registries)
were read, not compiled. A new by-value handle parameter fed from a prvalue of another handle type
would let MSVC, which destroys parameters in the callee, end an object's life earlier than today.

### L11. Explicit names in registration, and the surface contract

**Change.** Registration names every script-visible string with a literal, and composite names are
built from registered names, never from identifiers (BIND-1, CONFORM-2, both CONFIRMED). Today's
form, then a sketch of the proposed one:

```cpp
FreeFunction(int, Int_Add, "Return the sum of 'a' and 'b'", int, a, int, b) { return a + b; }
_registry.Function<&Add>("Int_Add", "Return the sum of 'a' and 'b'", {"a", "b"}).Alias("+");
```

**Why.** 81 macros in 11 NeuronCore files stringize an identifier. 1,005 of the 1,029 registered
function names contain `_`; 232 natives are C++ API functions registered under their own names, 94
of them spelled in scripts; eight scripts declare `Vector<Reference<RenderPassT>>`; scripts pass a
canonical type name to `IsType` 68 times; 18 of the 45 native fields they use would be renamed by
R8. Nothing can be renamed until names are strings.

**The contract.** A surface golden (types, sizes, alignments, hooks, fields and offsets, aliases,
overloads, name resolution) at fixed points: at startup, after the mappers are walked, after
loading. The verifiers add three conditions. It comes from the MSVC executable's link, because
duplicate names resolve by link order (D25). Construction counts go with it, since it cannot see
them. Type creation order needs its own dump, since the golden sorts (D24).

**Rules.** ADR-013 decision 8, R1-R3, R8, R9; ADR-018.

### L12. The binding layer as templates

**Change.** With names as strings, nothing needs the preprocessor (BIND-2): a variadic-template
thunk with today's ABI, `void (*)(void**, void*)`, replaces the arity-expanded macros; a field list
is a tuple walked by a fold; one generator fills every type hook (BIND-7). The registry owns its
entries, and handles become `TypeInfo const*` and `FunctionInfo const*` (BIND-8). A `Reflected`
concept makes a missing registration a compile error, with the 70 "unknown type"s registered under
those very names (BIND-14). The function-object library, 1,050 lines in 13 headers, becomes lambdas
and `std::function`, a cached generator held in a `shared_ptr` so copies still share it (BIND-12).

**Why.** The four macro headers are 4,607 lines, used at 2,471 sites in 425 files, for about 200
lines of templates (estimated). A prototype registered four natives through the template, with the
right results, registry entries matching the macro-built ones and the same 9 Ir per call (measured,
CONFIRMED). `Type` refcounts are 4.94 M Ir, 1.6% of `loadall`, and never reach zero before exit
(measured share).

**What it must keep.** Class-type parameters are references (a `static_assert`), since a `Data` by
value copies its payload and runs initializers, and a `Data` result is still copy-assigned (L5).
BIND-7's generator gives 100 types `assign`, `toString` and their real alignment and fixes `MeshT`
(D25), a surface change for the owner. For clang-tidy to mean conformant (CONFORM-7): no identifier
declared through a macro, registration in functions or `noexcept` initializers (523 findings in the
native files), no reserved identifiers (114). **Proof.** File by file on today's `Function_Create`,
each step keeping the surface golden, the construction counts and the type order. **Rules.** Needs
L11; §1, R10, R17 (304 `Mutable(` casts); markers go file by file (ADR-015 decision 5).

### L13. Registration without static constructors

**Change.** Each ScriptApi file exports a registration function, each library a list, and
`FrontierOutpost/Main.cpp` calls them, then checks every alias once. `/WHOLEARCHIVE` goes with the
last self-registering file (BIND-9, CONFIRMED).

**Why.** Static registration is order-safe only because every registry is constructed on first use;
`Function_AddAlias` copies whatever overloads exist so far (`Function.cpp:62-64`); and nothing the
linker can see uses the registration objects, hence `/WHOLEARCHIVE`.

**What it must keep.** Type creation order (D24), unless the owner accepts new GUIDs. **Rules.**
Supersedes ADR-014 decision 3 (§7 question 10).

### L14. Dead code: landed

§3 records it (EVAL-13, SUPPORT-13, BIND-13, CONFIRMED with corrections). Left for the owner: the
`castReal` path, which `Mission.h:51` reaches (D15), and `Distribution::getMin`, a template member
nothing instantiates, which indexes a `VectorMap` by position and now has no `const operator[]`
to call.

### L15. The support layer in conformant form

**Change** (SUPPORT-10, -11, -12, -14). The lowercase `error(x)` macro (`Common.h:133`) expands
`r.error()` in every file that includes `Common.h`, which rules out `std::expected`; it becomes
`LTE_ERROR` (24 uses), `LTE_ASSERT` becomes a `do { } while (0)` (18 call sites omit its
semicolon), and the compiler reports failure through `std::expected` instead of compiling the list
twice (`Expression.cpp:277-285`). `String`, which derives from `std::string` and converts to
`char const*`, becomes `std::string` with `std::string_view` functions, keeping its registered name,
its FNV-1 hash (widget identity in `Widget/Text.lts:7` and four more) and every native's result;
§3's tokenizer and keyword commits took most of the load gain SUPPORT-11 estimated. The const holes
(286 `Mutable(` in NeuronCore; a const `Array` handing out `T&`, which let EVAL-2's bug compile; an
`AutoPtr` stealing from a `const&`) become honest signatures, `mutable` counts, `std::span` and
`std::unique_ptr`. The containers map onto the standard ones, keeping the conversion order (D17),
the key order of reflected maps, `removeIndex`'s swap-and-pop, the `rand()` calls and what `@`
prints. **Rules.** §1, §4, R1, R2, R9, R10, R17; ADR-018. `long` is 8 bytes on Linux and 4 on
Windows, so a golden is generated on the platform it is compared on.

## 5. Defects found

Every confirmed defect, D1 to D30, with its finding as an alias. None is reached by the 121 scripts
in the game's load order unless the paragraph says so. §7 question 18 gives a recommendation for
each. D1, D3, D5, D6, D8, D20 and D9's `Pop` line landed on this branch; their entries keep one
line each and §3 has the rest.

### Crashes and undefined behaviour

**D1. An unknown method on a container crashed the compiler: landed** (SAFETY verifier). The field
mappers of six containers read the null value `FindField(0, name)` passes. `Transaction`, compiled
alone, now drops its `switch buying` statement instead of crashing, which is still D23.

**D2. Hot reload of an empty or deleted script hands GameLogic a null function** (COMPILE
verifier). Caught empty mid-save or deleted, the script recompiles to nothing, `ScriptFunction_Load`
returns null, and `Thruster.cpp:12`, `Ship.cpp:92`, `Wormhole.cpp:60` and the rest call through it.
The resource map is a startup snapshot (`Location.cpp:23-41`), so a later script is never seen.

**D3. `LocationFile::Read` freed a `new char[]` with `free()`: landed** (COMPILE verifier,
SUPPORT-13). The buffer is now the `uchar[]` its owning `Array` deletes.

**D4. The profiler's sampler thread is never stopped** (SAFETY-15, CONFIRMED). Started by
`ProfilingModule`'s constructor (`Profiler.cpp:114-122`), it polls forever and reads the module
after static destruction frees it (memcheck at `Profiler.cpp:99`, 3 of 3 runs); while profiling it
shares `currentFrame` and `active` unsynchronized, and calls `rand()`, as the script natives do. A
`std::jthread` joined in the destructor, and atomics, fix it; joining at exit needs a Windows check.

**D5. Registry objects were deleted through a base without a virtual destructor: landed** (BIND-8,
SAFETY-14). `~TypeT` is virtual and `FunctionImpl` is gone.

**D6. Handle assignment read its source after releasing it: landed** (SUPPORT verifier). A use
after free for `x = x->child` where `x` owns `child`; no such site existed.

**D7. `Type_Get<T>()` binds a reference to null, and field offsets are taken through a null base**
(BIND-5, SAFETY-14). `Type_Get` picks an overload with `*(T const*)0` (`Type.h:371-374`,
`:477-480`), and mappers take offsets from a null object: of the 2,479 UBSan reports in one
`loadall`, 1,868 are those bindings and 179 the offsets (§3's `alignof` removed another 432). Tag
dispatch through `std::type_identity<T>` is byte-identical on clang, but relies on MSVC finding a
class template's hidden friends through a template argument, and would fail silently, registering
"unknown type". It waits for an MSVC x64 and ARM64 registry comparison; the offsets go with L12.

**D8. Script-type fields were laid out misaligned: landed** (SAFETY-7, SUPPORT-9, EVAL verifier).
8 fields in 6 shipped types were misaligned and 22 sizes were not a multiple of their alignment.

**D9. Arrays, `ref` and `address` outlive their storage** (SAFETY-17, CONFIRMED). `Pop` nulled an
emptied array's element type, so the next `+=` crashed; that line landed (§3). `Get` and `Remove`
check no bounds, and `Pop` on an empty array wraps its size; a `ref` into an array reads freed
memory once it grows (a stale 1), and an escaped `address` reads another variable (28). Shipped
scripts take `address` of widget fields only, keep `ref`s short, and never reuse an emptied array.

**D10. A cast from `Data` does not check the type** (SAFETY-9 (a), CONFIRMED). `cast T data`
reinterprets whatever the `Data` holds, or reads null when it is empty (`Conversion.cpp:64-68`);
scripts guard only with `IsType`, which compares names (D23). The null-dereference half landed (§3).
`ExpressionExpressionCall::Evaluate` also frees by `IsLValue()` what it chose by `GetLValue`
(`ExpressionCall.cpp:50,65`), leaking a temporary on the way to a crash (SAFETY verifier).

**D11. The integer natives have undefined and fatal cases** (SAFETY-18, CONFIRMED). Signed `+`,
`+=`, `++`, `*` and `*=` overflow is undefined (`Int.cpp:57,65,128,176,184`; IntLoop and NativeCall
themselves overflow); integer division and `Mod` by zero raise SIGFPE; `(int)` of NaN is
undefined; `Rand32`'s shift overflows (`LteMath.h:131-135`); `Int_Random` over an empty range
divides by zero (`LteMath.h:123-129`, found by CONFORM's native differential). Wrap-defined
arithmetic gives today's observed results without the undefined behaviour.

### Wrong results a script can reach

**D12. `List_Shuffle` loses elements** (BIND verifier). `(*list)[i] = (*list)[index]` assigns one
`DataRef` temporary to another (`ScriptApiList.cpp:78-80`), so element `i` is never written:
shuffling 0 to 7 gives 0, 0, 2, 3, 4, 4, 3, 3. No script calls it.

**D13. `Vec3_Distance` is defined twice** (BIND-4, CONFIRMED). `ScriptApiV2.cpp:22-28` registers
the 2-D distance as `Vec3_Distance`, so two translation units emit the same inline thunk and
metadata with different bodies and the linker keeps one: the 3-D `Distance` is not registered.
`FunctionAlias(Vec2_Distance, Distance)` and `FunctionAlias(Vec4_Dot, Dot)` alias sources that do
not exist, so `Dot` on two `Vec4` returns a Double through `Vec4d_Dot`. No script calls them. The
fix adds the 3-D `Distance`, renames the 2-D native and returns a Float from `Dot`: a change to the
frozen surface.

**D14. A pooled type constructed through `Type::Construct` is constructed twice** (BIND-16,
REFUTED as a quick win). `POOLED_TYPE`'s placement `operator new` constructs (`Pool.h:103-105`), and
the new-expression constructs again. A script reaches it through a field of a pooled native type:
object IDs taken before and after one `var h Holder`, whose field is an `Asteroid`, are 0 and 3,
because the `Asteroid` takes two IDs; with the one-line fix, 0 and 2. No shipped script does this.

**D15. `CastReal` always asserts** (BIND-13 verifier, corrected). No type sets the `castReal`
hook, so `CastReal` always raises, and `Mission.h:51` calls it, as `DataRef::CastReal` through
`ItemProperty::Evaluate`, whenever a mission has a `unit` property, which a script could set
through `Mission_Create`. Nothing sets it today. (The verifier named `Data::CastReal` as the live
one; it is `DataRef`'s. §3 deleted the `castInt` half, which nothing reached.)

**D16. `String_Substring` ignores `start`**: it returns `s.substr(0, length)`
(`ScriptApiString.cpp:143`; SUPPORT-13). `Widget/TextField.lts:28`, the one shipped caller, passes a
start of 0.

**D17. `int` and `int64` each carry a second `String` conversion that prints them unsigned**
(EVAL-14, SUPPORT-13, CONFIRMED). `uint32_to_string` and `uint64_to_string` are declared on `int32`
and `int64` (`ScriptApiString.cpp:23-24`); the signed one wins only because it is registered first,
so reordering the file would print −1 as 4294967295.

**D18. A `\r` outside a string literal drops the character after it** (COMPILE-15, CONFIRMED).
`Tokenizer.h:127-130` advances twice, so `StringTree_Create` of CRLF text merges lines. Script
files are safe, because `ReadAscii` strips every `\r`, and no script uses `StringTree`.

**D19. Defects in the support headers** (SUPPORT-13, CONFIRMED): `Tuple3` and `Tuple4` compare
`a.z` with `a.z` (`Tuple.h:50,54,72,76`); `Array`'s `operator==` compares `size()` bytes, not
`size() * sizeof(T)` (`Array.h:111`); `Vector::shuffle` wraps for an empty vector (`Vector.h:272`);
`HashMap`'s `DefaultHash` is ill-formed if instantiated and uses `<tr1/unordered_map>` on Linux;
`ProgramLog`'s `DoLog` prints the whole entry once per line of it (`ProgramLog.cpp:34,37`) and stops
logging silently after 4,096 entries, to the file too.

**D20. Each method left a null entry in its script's function table: landed** (SAFETY-12, defect
2). 311 entries after a `loadall`, now 0; eight shipped scripts define a method that shares its name
with a later top-level function, which still compiles as before. `ExpressionType.cpp:108` has the
same inserting lookup for type names; it leaves an entry only when a type's field fails to compile,
which no shipped script does.

**D21. `Traits` declares 6 elements for 7 traits** (`GameLogic/Traits.h:15`), so vector operations
and `ToString` ignore `Sociable`. It is GameLogic, outside the LTSL layer, and fixing it changes
game results.

### Defined behaviour that a refactor must keep

**D22. Statements that fail to compile vanish, and two real script bugs hide that way** (SAFETY-4,
CONFIRMED). `Widget/Slider.lts:43`, `vlaue = (Clamp value minvalue maxValue)`, has two typos, so
the slider's value is never clamped. `Widget/Window.lts:50-52`'s `PrePosition` assigns the bare
names `size` and `pos`, which are not in scope, so the hook does nothing. Comment-only blocks drop
their `if`, and `Item/ShipType/Generate.lts:78` so loses an `rng.Float` draw nothing uses later.
Fixing the scripts changes the game; a diagnostic per dropped statement changes printed output.

**D23. What a script compiles to depends on what was loaded before it** (SAFETY-5, CONFIRMED and
understated). A bare type name resolves through one global map where the last registration wins
(`Script.cpp:104-105`, `Type.cpp:50`), and 20 script types overwrite a name already there (`App` 12
times; `Widget/Messages.lts:30`'s `MessageLink` over the native one). Two of the 121 scripts
compile differently alone: `Widget/Settings` drops a statement, and `Widget/Market/Transaction`
drops its `switch buying` statement (it crashed before D1's fix). `MessageTarget` and `Ribbon` each have two definitions with
different layouts, and `IsType` cannot tell them apart. L7 writes down the resolution order; §1's
fresh-process compile pins it.

**D24. Type creation order is script-visible** (BIND verifier). GUIDs are creation indices
(`Type.cpp:48,56`); `DataDestroyed` and `DataDamaged` hash their type's GUID into the item's hash
(their `.cpp:37`), which scripts read through `Item_GetHash` and which seeds RNGs
(`AssemblyChip.cpp:19`). Any change to registration order moves them: L12's migration, L13, even
deleting a file that creates types at static initialization. The sorted dumps cannot see it; §1's
type-order dump can.

**D25. Two macros race to fill one type, and duplicate names resolve by link order** (BIND-7,
BIND-14, CONFIRMED). For 153 classes `DERIVED_TYPE_EX` and `DefineMetadataInline` fill the same
`TypeT`, and the first to run wins: 100 types have alignment 1 and no `assign` or `toString`, while
`MeshT` went the other way and is missing from `GeometryT`'s derived list, so a saved Geometry
holding a Mesh cannot load (`Serializer.cpp:106-117`). 70 types are named "unknown type" or
`Reference<unknown type>` (`List`, `RNG` among them), and `V3T`, `V4T`, `BoundT` and `Vec` each name
two. `Type_Find` returns the last registered, so a `Vec3` saved through `Data` reloads as a 24-byte
`Vec3d` on the harness; the winners flip in the executable's link order (CONFORM verifier), so on
Windows it likely runs the other way. No script saves `Data` today.

**D26. Declared return types are ignored** (SAFETY-13, CONFIRMED). A function's type is that of its
last expression (`ExpressionFunction.cpp:72`): 31 shipped functions differ from their declaration,
every `App/*:Main` among them, and `FrontierOutpost/Main.cpp:120` relies on the actual type. Hosts
write a hook's result into a buffer of the type they expect, unchecked (`WidgetCustom.cpp:45-68`,
`Scriptable.h:25-26`, and `WarpCustom.cpp:15-16`, whose `V3 result` is uninitialized); every shipped
hook matches today.

**D27. Field initializers run on every construction, however it happens** (SAFETY-16, corrected by
its verifier; EVAL verifier). `object.AddScript WarpNodeScript` runs its initializers at least four
times, at least 12 `rand()` draws, and keeps the first values; each App type's
`Texture/RandomScreenshot:Get` initializer creates a GPU texture per construction. `rand()` is
seeded from the clock (`LteProgram.cpp:36`), so this is not reproducible today; any change to
temporaries, copies or boxing changes the count (§7 question 3). (`Constructor.cpp:115` also tests
its bound after reading `initializers[fieldIndex]`; the argument count keeps it in range.)

**D28. Every reload leaks its script's types** (COMPILE-17, CONFIRMED). `Type_Create` only appends
to the global list (`ExpressionType.cpp:197-198`: "TODO : Type leakage"), so each full reload (F5)
adds 232 types that nothing removes.

**D29. Hot reload changes what a running game computes** (COMPILE verifier). A detected edit
restarts that script's statics and replaces its types, while other scripts keep calling its old
functions. It is behaviour to keep, and the reason every L6 option is the owner's.

**D30. The interpreter's global state is not thread-safe** (SAFETY-20, CONFIRMED by reading):
per-node statics, the pools' free lists, inserting lookups, non-atomic reference counts, the script
cache, the profiler. No script runs off the main thread today; parallel or lazy loading needs this
list resolved first.

## 6. The AGENTS.md conversion

The target is the owner's decision 2 for 75 files, 13,141 lines, all marked Legacy: 54 core files
(10,742 lines, 4,542 of them in the three generated headers) and 21 native files (2,399). Their
closure is 26 more legacy headers (2,967 lines); 426 files use the registration macros 2,458 times;
70 files outside the layer name its identifiers at 165 sites. This condenses CONFORM-1 to CONFORM-13
as their verifier corrected them. The counts are at the review's base: §3 has since deleted 919
lines of C++, `DataStack.h` among them.

Three facts force the shape. Script names are C++ identifiers, so nothing is renamed before
registration names them (L11). A converted `.cpp` compiles its whole header closure at `/W4 /WX`,
inherits `Common.h`'s warning pragmas and `LteCommon.h`'s `using namespace LTE`, and that closure is
in 349 of the tree's 370 translation units: so conversion goes bottom-up, and thin legacy headers
keep the old spellings for everything else. And each slice needs a proof in CI, which does not
exist yet.

### Phases

The clusters, with their files, lines and clang-tidy findings: C1 the front end (`Tokenizer.h`,
`StringList.*`, `LTSL.*`: 5, 505, 81); C2 reflection (`Type.*`, `TypeArray.cpp`, `TypePointer.cpp`,
`BaseType.h`, `Field.h`, `Parameter.h`: 7, 1,331, 298); C3 values (`Data.h`, `DataStack.h`: 2, 322,
41); C4 binding (`Function.*`, the generated headers, `AutoClass.h`: 6, 4,756, 498); C5 the core
(`Expression.*`, `Expressions.h`, `Environment.h`: 4, 489, 180); C6 the nodes (25, 2,854, 435); C7
scripts (`Script.*`, `ScriptFunction.*`, `ScriptType.h`: 5, 485, 52); C8 the natives (21, 2,399,
523).

| Phase | PRs | Content | Proof |
|---|---|---|---|
| 0. Proof and hygiene | ~8 | ADR-018; `Common.h`'s pragmas into `Legacy.targets`; `/W4` hygiene, legacy style, of the closure **and** the core headers; `Tests/NeuronCoreTests` with the goldens; the checker gaps; `SupportJustMyCode`; L9's decision | warnings a subset of today's; identical object code but for debug information |
| 1. Decisions | — | L1's ADR, L2, the approved §5 fixes, each its own PR | the goldens |
| 2. Format | 3 | the pinned clang-format over C1, C2, C3, C5 and C7 (23 files), then C6 (25), then C8 (21); C4 waits for Phase 3 | the token comparison |
| 3. Mechanism | ~5 | the conformant registry with explicit names beside the legacy macros (L11, L12); the LTSL-owned registrations; the 21 native files | full-surface golden, construction counts, type order |
| 4. Convert | ~14 | one cluster per PR in include order: C2, C3, C4, C1 (or L7 instead), C5 with C7, C6 in four PRs, C8 in three (its float PR carries `/fp`), then the namespace move | every golden; the three checkers clean |
| 5. Shims | later | each legacy header goes with its last consumer's conversion | the registry golden |

About 30 PRs (estimated, 25 to 35). CONFORM-1 put C1 first; its verifier showed that order cannot
build (PLAUSIBLE, corrected): `StringList.h` (C1) includes `BaseType.h` (C2) and `AutoClass.h` (C4),
C5 and C7 include each other's headers, and 116 clang-proxy warning sites sit in core headers that
every new conformant file includes (`DeclareFunction.h` 29, `Type.h` 17, …). Hence the order above.

**What each phase must prove.** The goldens move into CI in Phase 0, with two conditions from the
verifier: the registry golden comes from the MSVC executable's link (D25), and the native-bits
golden runs in Release too, per configuration and platform, because CI builds Debug only and under
clang's proxy `-O0` hides the `/fp` difference in 21 of its 22 natives. Format PRs are proved by a
token comparison: CONFORM-6's covered 55 translation units, 18,150,409 tokens, with 0 differences
after normalizing only `__LINE__`, `__FILE__` and `>>` (CONFIRMED). Rename PRs cannot show identical
object code, since a rename changes decorated names, RTTI names and the `__FUNCTION__` text
`AUTO_FRAME` embeds; they compare disassembly modulo symbols, and the goldens.

### `/fp:fast` to `/fp:precise`

Legacy files compile with `/fp:fast` (`Build/Legacy.targets:21`), converted ones with `/fp:precise`
(R16). CONFORM called 168 numeric natives on 48 fixed inputs each, built both ways with clang's
`-ffast-math` as the proxy: 374 of 8,033 calls in 22 natives returned different bits (CONFORM-5;
CONFIRMED for clang, PLAUSIBLE for MSVC). They are the vector `Divide`, `Mix`, `Normalize`, `Dot`,
`Length`, `Distance`, `Min`, `Max` and `Vec3_Spherical`, `Float_Mix`, and `ToHSL` and `ToRGB`. Most
differ by 1-2 ULP; `Vec3f_Mix` by up to 3,277 and `Vec3d_Mix` 6,554 on ill-conditioned inputs; `Min`
and `Max` return +0.0 against −0.0; a `ToRGB` component near zero moves from −3.24e-5 to −2.16e-5.
Scripts call `Mix` 50 times, `Normalize` 17, `Length` 13 and `Dot` 10. Taken one relaxation at a
time, reciprocal math changes 126 calls in 9 natives, reassociation 50 in 17, signed zeros 12 in 6,
and contraction none on x64. So the direction holds and the detail does not transfer: 112 of the 374
come from clang's reciprocal estimates, which the verifier reads MSVC as not generating, and MSVC's
documentation lets it narrow the `StdMath.h` wrappers to the UCRT's float functions, which could
move scalar natives that are identical here.

This collides with "identical results". Worse, the math is inline in shared headers, and where it
is not inlined one copy per link, whichever the linker meets first, serves every file, so mid-way
an optimized build's results depend on link order; and under the proxy today's Debug and Release
already disagree on these bits. The choices (§7 question 11): (a) keep `/fp:fast` for the float
natives and the inline math they share, as an exception ADR-018 lists and `CheckProjectFiles.py`
checks per item; (b) move the whole float surface in one PR, accepting what an MSVC run measures;
(c), rejected, reproduce `/fp:fast` results by hand. The evidence is CONFORM's differential built
with MSVC: x64 Debug and Release, and ARM64 Release, where `/fp:fast` also contracts to FMA.

### The header closure

MSVC sets the warning level per translation unit, so a converted `.cpp` at `/W4 /WX` compiles every
legacy header it includes at that level: with MSVC-like clang flags, 617 unique warning sites, 427
in the core, 60 in the natives and 130 in the closure (CONFORM-3, CONFIRMED). `Common.h:49-79`
disables seven warnings and promotes three for the rest of every file that includes it, the
converted file's own code included, which §4 forbids. `LteCommon.h:330`'s `using namespace LTE;` is
load-bearing: without it 118 of 122 NeuronCore files fail, with 11,836 errors. `RunClangTidy.py`
drops legacy-header diagnostics by basename. Phase 0 moves the pragmas into `Legacy.targets` (an
amendment to ADR-015 decision 2; no first-party file includes a NeuronCore header today), and
converted code qualifies legacy names. Whether MSVC's C4458 fires on constructor parameters as
clang's shadowing warning does is unverified.

### Renames, namespaces and files

R2 forbids the `T` suffix the object types carry. Renaming the objects touches 165 sites; renaming
the handles instead, about 1,205 (CONFORM-8, CONFIRMED). So the handles keep their names and the
objects take concepts: `ExpressionT` to `ExpressionNode`, `StringListT` to `StringListNode`, `TypeT`
to `TypeInfo`, `FunctionT` to `FunctionInfo`, `ScriptT` to `ScriptModule`, `ScriptFunctionT` and
`ScriptTypeT` to `ScriptFunctionInfo` and `ScriptTypeInfo`, `TypeImpl` and `FunctionImpl` to
`TypeRecord` and `FunctionRecord`, `EmptyBase` to `NoFields`, `HashT` to `HashValue`. Registered
names stay, through L11. Converted code goes to one namespace, with `LTE` kept as a legacy alias
(CONFORM-9; §7 question 13). New files are named for their types, and the old headers stay as legacy
shims that include them and supply the old spellings, so the 426 consumer files need no edit
(CONFORM-12). Unedited is not unchanged: re-expressing the legacy macros over the new registry
changes how every consumer registers, so the full-surface golden must exist before that PR.

### Tests

NeuronCore has no test project, so nothing in CI would notice a renamed native, a changed overload
or a changed float (CONFORM-4, CONFIRMED). `Tests/NeuronCoreTests` is a new project, the owner's
decision (§7 question 12); GameLogic's surface goes in `Tests/GameLogicTests`, which already links
both libraries whole. The verifier found three limits. Only the executable's link shows the game's
duplicate-name winners, and the full surface needs NeuronClient's natives and FrontierOutpost's 37
registering files. The corpus has no legal home yet: ADR-014 forecloses a `Scripts/` subfolder,
and the interpreter reads scripts only from `GameData/` or `./mod/<name>/` under the working
directory, and writes `./cache/` there (R13; the harness left one in the repository root). And a
test that includes `Type.h` waits for Phase 0's hygiene, or enters through legacy-file entry points.

### Checker gaps and settings

Six gaps, each needing a `Build/TestCheckers.py` case (CONFORM-13, CONFIRMED). The R2 pattern in
`CheckProjectFiles.py` misses a trailing `T` (with exemptions needed for template parameters and for
first-party forward declarations of legacy types, `GameLogic/Visual.h:6-10`), and the checker cannot
see an item-level `FloatingPointModel`, `WarningLevel` or `TreatWarningAsError`. clang-tidy reports
no identifier declared through a macro. `RunClangTidy.py` excludes legacy headers by basename and
lints a converted header only through a first-party file that includes it. `HeaderFilterRegex`
misses `Function_Generated.h`, `AutoClass_Generated.h` and `Compare_*.h`. And nothing says which
clang-tidy decides: `.clang-tidy` pins 22.1.8, CI uses Visual Studio's and 22.1.0.

Removing a marker flips a file's settings at once; for these files only `/W4 /WX` and `/fp` matter,
since none throws, calls Win32, holds a non-ASCII byte or tests `__cplusplus` or `_WINDOWS`
(CONFORM-10, PLAUSIBLE). `/JMC` is off for legacy files because it "slows every call in Debug",
and `NeuronCore.vcxproj` states nothing; a Debug command line shows whether converted files would
get it (§7 question 15).

### Order of work, relative to §3 and §4, and verification

1. §3 is in first: legacy-style changes proved on the Linux harness. This branch is then built with
   MSVC (Debug|x64, Release|x64, ARM64) and the game launched, since none of §3 was; then come §9's
   Windows measurements.
2. Phase 0, once §7 questions 11, 12 and 16 are answered.
3. Before Phase 2's formatting, since after it each would rebase onto files 79% reformatted: the §4
   items that edit legacy files in their own style (L2, L3, L5, L6 (a) and L8) and the approved §5
   fixes, each its own PR; and L1, a new conformant file that ADR-015 allows the legacy nodes to
   call.
4. Phase 2; then Phase 3 with L11, L12 and, if approved, L13.
5. Phase 4, with L7 in place of C1, L9 decided before C1, C5 and C6 (their classes are pooled), and
   `/fp` in C8.
6. L15 and Phase 5 with the consumers. L4 whenever the owner decides; it is NeuronClient's.

For every item: **build** Debug|x64, Release|x64 and ARM64 through the solution (ADR-006, ADR-014
decision 8), Release by hand for a header change, since CI builds Debug only; run **the checkers**
(`CheckFormat.py`, `CheckProjectFiles.py` with any new file in the `.vcxproj` and its `.filters`,
`RunClangTidy.py`); reproduce **the proof set** of §1, on the Linux harness until the test projects
carry it, plus the Windows checks the item names; and **look**: launch `war`, `ltheory` and a widget
app, because scripts drive the UI, the HUD and every object, and no Linux run reaches their hooks
(AGENTS.md §3).

### ADR-018 outline: the LTSL layer is converted to AGENTS.md behind a frozen script surface

CONFORM's outline, corrected by its verdict.

- **Context.** ADR-001 and ADR-015 exempt the 75 files file by file; the owner wants AGENTS.md in
  full for them, with the language, the script API and their results frozen (ADR-013 decision 8).
  The surface is spelled by C++ identifiers. The layer's headers are in 349 of 370 translation units
  and carry warning pragmas and `using namespace LTE`. `/fp:fast` and `/fp:precise` give different
  bits in 22 of 168 numeric natives under clang's proxy. Duplicate names resolve by link order, and
  the harness's order is not the executable's.
- **Decision.**
  1. The script surface is a contract held in goldens CI checks: the registry at fixed points from
     the executable's link; the structural compile dump and a fresh-process compile of each script;
     native bits in Debug and, per platform, in Release; construction counts; type creation order;
     a language corpus. Changing a golden needs the owner.
  2. Registration names every script-visible string with a literal; no registered name comes from a
     C++ identifier, and no registration declares an identifier through a macro.
  3. Conversion goes bottom-up by include closure, one cluster per PR: C2, C3, C4, C1 or its
     replacement, C5 with C7, C6, C8, then the namespace move; format and mechanism before renames,
     the marker last. No file loses its marker while a header it includes is not `/W4`-clean or
     carries a warning pragma. Format PRs are proved by token comparison, hygiene by identical
     object code, renames by disassembly modulo symbols, and all by the goldens.
  4. `Common.h`'s warning pragmas move into `Legacy.targets` (amends ADR-015 decision 2).
  5. Converted code uses the legacy vocabulary types as an external API until their own conversion.
     A legacy file may take three kinds of new lines and no others: compatibility aliases and
     forwarding macros in the shims, registration through the new mechanism before its file
     converts, and the entry points Phase 0's tests need (narrows ADR-015's "new code with the
     marker").
  6. Object types renamed to concepts, handles kept; one namespace (§7 question 13); files named for
     their types. Floating point as the owner chooses (§7 question 11), with the MSVC evidence
     attached. Allocators as L1's and L9's ADRs decide.
  7. Registration stays static, its outcome pinned by the goldens, unless L13's ADR supersedes
     ADR-014 decision 3.
- **What it forecloses.** Deriving a script name from a C++ identifier; converting a file before its
  closure is clean; a conversion PR that changes a golden; mixing `/fp` models inside the shared
  float surface except as decided here; new code in legacy files beyond decision 5; removing a shim
  while a legacy consumer uses it; reformatting or renaming a legacy file outside this plan.

## 7. Decisions for the owner

Answered before this plan: Q1 to Q4 (header). The conversion's ADR is called ADR-018 here, as
CONFORM proposed; whichever ADR lands first takes that number.

1. **L1: the evaluation stack.** Recommended: approve, with its ADR. It is the largest measured
   gain, its prerequisites (EVAL-2's fix, zero-fill) are in, and its proofs are strong. Take the
   Windows per-frame sample first, so the ADR can quote milliseconds. ADR outline. *Context:* every
   temporary, argument and local is a heap allocation, 24-44% of the benchmarks' instructions, with
   strictly LIFO lifetimes; the stack measured −18% to −49% of script Ir. *Decision:* one LIFO stack
   per thread, 64 KiB, heap fallback; Push zero-fills and constructs, Pop destructs; a 16-byte base
   for every value; the register file shared per thread; LIFO asserted in Debug. *Forecloses:*
   evaluator temporaries allocated one by one; one `Environment` evaluated on two threads; a native
   keeping an argument's address past its call.
2. **L2: script-call scopes.** Recommended: option 3. It keeps every printed line, crash trace and
   the F2 profiler, and takes about three quarters of the cost. Leave `BUILD_DEBUG` alone until the
   conversion: making it follow the configuration changes Release's failure behaviour (the `[Error]`
   prefix, `Log_Critical`, the CONTINUE dialog) and deserves its own decision then. Fix D4 with it.
3. **Construction counts.** Is how often a script type is constructed frozen? Recommended: yes, for
   now, pinned by `VBIND/Count`, `VBIND/Attach`, `VEVAL/Box` and EVAL's corpus. The count reaches
   the RNG and the GPU (D27), and L5 (a)'s measured gain is on a path whose per-frame count nobody
   has measured. Revisit L5 (a) and (d) after L1, with per-frame counts.
4. **L4: when `CreateChildren` runs.** Recommended: measure first, with per-frame counters in
   ltheory and war, then decide; any change moves script side effects.
5. **L6: hot reload beyond the stat gate.** Recommended: time `ScriptFunction_Load` and ltheory's
   startup on Windows first; if the stats matter there, (a); not (b), which puts Win32 file watching
   into the shared engine and would need its own ADR. With it, D2's fix (a reload that reads an
   empty or missing file keeps the previous compile, and logs it) and D28.
6. **L7: the resolver.** Recommended: after Phase 0's goldens, in place of converting C1, and
   without COMPILE-3's intermediate step, which changes the candidate snapshot and so what
   compiles. The fresh-process compile of every script is its gate.
7. **L8: float text and literals.** Recommended: approve the MSVC proof run; land only on zero
   differences.
8. **L9: the pool.** Recommended: keep it under an ADR, after the NT-heap and per-frame
   measurements, with its size and alignment fixes. ADR outline. *Context:* 104 pooled classes;
   R15; removing the pool costs 8.7% of load on glibc, plus the Windows figures. *Decision:*
   `PoolRaw` stays for those classes; `operator new` asserts its size; blocks are keyed on alignment
   too; single-threaded. *Forecloses:* pooled objects created off the main thread; deriving from a
   pooled class without its own `POOLED_TYPE`; any other pool without its own ADR.
9. **The frozen surface.** Everything registered (1,056 natives and 391 types at startup; 1,168 and
   535 once every mapper has run), or what the 121 scripts reach (404 function names, 30 type names,
   45 native fields)? Recommended: everything, as ADR-013 decision 8 reads; accidental names
   ("unknown type", one `V3T` for both vectors) kept byte for byte until a deliberate API change;
   lazy registration kept; order-dependent outcomes pinned from the executable's link.
10. **L13: registration without static constructors.** Recommended: not before L12 is well along,
    and then keeping today's type creation order. ADR outline. *Context:* self-registration needs
    `/WHOLEARCHIVE` (ADR-014 decision 3), and aliases copy whatever was registered before them.
    *Decision:* each library registers its script API from one function the executable calls;
    aliases are checked once, after everything; `/WHOLEARCHIVE` goes with the last self-registering
    file; type creation order stays as its golden pins it. *Forecloses:* registration from static
    initializers; a script API that depends on link order.
11. **`/fp`.** Recommended: (a) now, as ADR-018's exception, checked per item by
    `CheckProjectFiles.py`; decide (b) with the MSVC differential in hand, ARM64 included, when the
    engine's math converts as one. (b) without that evidence breaks "identical results".
12. **The test project.** Recommended: `Tests/NeuronCoreTests`, x64 and ARM64 like the other test
    projects; the corpus as data files in the project's folder, loaded through `LocationMemory`, the
    engine's in-memory location, with the working directory set to a temporary folder so `./cache/`
    lands there; and the full-surface golden from the executable itself, through a registry-dump
    option. That amends ADR-014 decision 1, which keeps the executable's arguments unchanged, but
    only the executable's link shows the game's winners.
13. **The namespace.** The lenses proposed `Neuron::Ltsl`, `Ltsl` and `Neuron`. Recommended:
    `Neuron::Ltsl`. The engine layers use `Neuron` (ADR-005 decision 6), and the nested name keeps
    the script layer's short names (`Compile`, `Registry`, `Value`) from colliding with the
    engine's.
14. **Names and shims.** Recommended: objects renamed and handles kept; the legacy vocabulary types
    used like an SDK until their own conversion; legacy headers as shims; format first, although
    `CheckFormat.py` will not hold those files until they convert.
15. **`/JMC`.** Recommended: `SupportJustMyCode` false at NeuronCore's project level, both
    configurations, if a Debug command line shows `/JMC` on.
16. **The Linux harness.** Recommended: into the repository as `Tools/LtslHarness/`, with its
    goldens, run in CI's existing Linux job until the Windows test projects carry them. Every proof
    here ran through it. Its file lists should come from the `.vcxproj` files through
    `Build/ProjectModel.py`, so it cannot drift; it runs from a scratch working directory (R13); its
    figures stay labelled clang/Linux. ADR outline. *Context:* no MSVC where the interpreter is
    reviewed; the harness compiles the engine's own sources with clang and 115 trap stubs.
    *Decision:* a development tool under `Tools/`, never shipped or linked into the product (R14
    does not bind it), gating only the goldens of §1. *Forecloses:* reading its results as MSVC
    evidence; building the product with it.
17. **Dropped statements.** Recommended: a diagnostic per dropped statement in Debug builds only, so
    Release prints what it prints today; and D22's two script bugs fixed as game changes, each its
    own commit and looked at in the game, since they change what the slider and the window do.
18. **The §5 fixes:**

| Defect | Recommendation |
|---|---|
| D1 unknown container method | landed (§3) |
| D2 null function after a reload | with L6: keep the previous compile on an empty or missing file |
| D3 `free` of `new[]` | landed (§3) |
| D4 profiler thread | with L2 |
| D5 exit-time deletes | landed (§3) |
| D6 assignment after release | landed (§3) |
| D7 null `Type_Get` | after an MSVC x64 and ARM64 registry comparison |
| D8 misaligned fields | landed (§3) |
| D9 arrays, `ref`, `address` | `Pop` landed (§3); bounds checks, `Pop` on an empty array included, under Q1, the owner choosing the result; lifetimes documented |
| D10 unchecked `cast` | under Q1: check the type, log, zero the result; fold in the lvalue leak |
| D11 integer natives | wrap-defined arithmetic under Q1; the owner picks results for division by zero and an empty range |
| D12 `List_Shuffle` | fix, as an owner-approved API fix; no script calls it |
| D13 `Vec3_Distance` | fix: no script affected, but it changes the frozen surface |
| D14 pooled double construction | fix, with L9 or before: IDs change on a path no script uses |
| D15 `CastReal` | install `castReal` for arithmetic types (Q2) |
| D16 `Substring` | fix, as an owner-approved API change; the shipped caller passes 0 |
| D17 unsigned conversions | leave; if L8 touches the file, declare them on the unsigned types, order kept |
| D18 `\r` in `StringTree` | fix with a test; an API change no script sees |
| D19 support headers | fix `Tuple`, `Array` and `shuffle`, which nothing reaches today; `HashMap` with L15; `DoLog` changes the log's text, so the owner's |
| D20 null method entries | landed (§3); the same fix for type names at `ExpressionType.cpp:108` is a one-line follow-up |
| D21 `Traits` size | route to GameLogic's owner; it changes game results |
| D22 dropped statements | question 17 |
| D23 load order | keep; pinned by the fresh-process compile |
| D24 type creation order | keep; pinned by its golden |
| D25 hook race and names | decide with L12: accept the 100 types' hooks and alignment, fix `MeshT`, keep the names |
| D26 declared return types | keep; add a typed `VoidCall<T>` that checks a hook's return type, and initialize `WarpCustom`'s result |
| D27 initializer runs | question 3 |
| D28 type leak on reload | with L6 |
| D29 hot reload semantics | keep; question 5 |
| D30 thread safety | document; resolve before any parallel proposal |

## 8. Checked and dismissed

Refuted by the verifiers:
- **BIND-6 as a quick win:** `Data`'s move operations drop one construction per `Data`-returning
  native (`VBIND/Count` 70 → 66). Now L5 (a).
- **BIND-15's "no semantic change":** a `Data` result constructed in place loses a construction. The
  rest is L5 (b).
- **BIND-16 as a quick win:** the fix changes object IDs a script can read. Now D14.
- **SUPPORT-5's reference binding at `ExpressionFunctionCall.cpp:219`:** a reference sees natives
  created while the arguments compile, and `VSUPPORT/RefBind` then compiles differently. Dropped;
  it would have saved 2.74 M Ir.

Not pursued:
- **SUPPORT-2's `[[noreturn]]` failure path:** it changes what CONTINUE does after a null handle,
  and with the checks now Debug-only (§3) it would only speed up Debug.
- **EVAL-11's `Emit` overrides:** the harness's `ast` mode sees under conversions, switches, prints,
  `ref` and `static` with no engine change.
- **`AUTORELOAD` off in Release:** the owner chose not to (Q3).

Leads the reviewers dismissed that a later reader might reopen:
- **A bytecode or closure compiler:** a rewrite; L1, L3 and §3's register fast path take the cheap
  part of the dispatch cost.
- **Constant folding:** `IsConstant` is false for native calls, and natives carry no purity
  metadata (RNG, time). **Constants passed by address:** natives mutate arguments in place
  (`Int.cpp:65`), so a constant could change for later evaluations.
- **Reference counts at run time:** the loops copy no handle (68.6k Ir of 576 M in IntLoop); only
  the load path's counts matter (L10, L12). **Argument copies per native call:** none; thunks bind
  `T const&` to the caller's objects.
- **An on-disk compile cache:** the `#if 0` code in `ScriptFunction.cpp:6-29` caches results, not
  compiled scripts; a compile cache is a new runtime file (R13) for about 40 ms on Linux.
- **`std::flat_map`:** not in MSVC 14.50 or libstdc++ 13, and an insert invalidates references
  `Script_Load` and `Function_AddAlias` hold. **`std::any` for `Data`:** it cannot hold a
  script-type payload, which has no C++ type.
- **`DataStack.h` as L1's stack:** no capacity check, and `Free` trusts its caller; §3 deleted it.
  **Removing the pool:** +8.7% of load (L9). **Parallel loading:** the global state is not thread-safe (D30).
- **The static-initialization order fiasco:** none; registries are constructed on first use, and
  only aliases and D25 depend on order.
- **R11, R17, `/EHsc`, Unicode, `/utf-8`, the precompiled header:** no effect on the 75 files.
  **`\r` in script files:** `ReadAscii` strips it first; only `StringTree` reaches D18.

## 9. Evidence

**How each lens measured, and how its verifier checked.** All on the harness of §1: clang 18.1.3,
libstdc++ 13, Linux x86-64, 4 shared cores, hence Ir rather than time. Every lens worked in a copy
of the sources, and no tracked file was modified; runs from the repository's root wrote only its
git-ignored `cache/` folder.

| Lens | Reviewer | Verifier |
|---|---|---|
| EVAL | eight step builds of NeuronCore (stack, argument slots, shared registers, no `FRAME`, `to_chars`, register fast path); a LIFO-assertion build; re-entry, `Data`-argument and corpus scripts; memcheck | single-fix builds; its own LIFO checker, freed bytes poisoned, at six capacities; 17 adversarial cases; the 84 shipped functions that run headless; all 2^32 `int` and `unsigned` values through the stream and `to_chars` |
| COMPILE | an instrumented copy with counters and switchable variants; a rich dump showing overloads and conversions; a headless link (ADR-016's shape) creating ltheory's universe and war's objects; strace | every quick win in one build, and single-change builds; a CRLF copy of the 121 scripts; differential fuzzers for the tokenizer (340,000 strings, 35.8 M results) and `String_Split` (2,000,000 pairs); hot-reload drivers; memcheck |
| BIND | a surface dump (2,840 lines: hooks, offsets, name resolution); a probe of what scripts look up; two experiment builds; SAFETY's sanitizer binaries; a template-thunk prototype | four single-change builds; `VBIND/Count` and `VBIND/Attach`; probes of derived lists and of type creation order |
| SUPPORT | one experiment tree in seven steps, each checked against the dump and profiled; differential tests (2,000,000 splits, 12,000,012 number parses); a layout tool | every quick win together, all four projects rebuilt against the headers; three variants and a no-pool build; a move-site audit over 351 files; an exhaustive integer fast-path test (1,357,374 cases) |
| SAFETY | an inert audit build logging every suspect path the 121 scripts reach; sanitizer objects linked with gcc 13's runtime, clang's being absent; two fix builds; the 122-entry corpus; crash probes | the fixes in seven stages; the probes; an AST scan of every native call; the fresh-process compile of all 121 scripts |
| CONFORM | a registry dump; warnings with clang flags standing in for `/W4`; clang-tidy 22.1.0 and clang-format 22.1.3 as CI pins them, with a token comparison; an include census; a native differential under `-ffast-math` | the relaxations one at a time; an ODR experiment on shared inline math; the same objects relinked in the executable's order |

**How §3 was proved.** Each commit was built into the harness from the working tree and run through
the whole proof set of §1 before it was committed; a golden changed only where the commit says so,
and the change was then accepted as the new golden. Its speed was measured with callgrind against
the commit before. The lead also used UBSan (clang's objects linked with gcc 13's libubsan) before
and after the alignment fix, memcheck and gdb through exit for the destructor fix, and a parent
build in a separate worktree to isolate the moves.

**Measured and estimated.** Every figure marked measured is callgrind Ir, or a count, on that
harness. These are estimates, and each names its method where it appears: L1's per-frame figures,
the 200-300 C++→script calls per frame and L2's option 3 saving, L3's 5-8%, L5's 100-130 Ir per
boxing, L7's gains beyond the oracle, L8's per-float saving, L12's "about 200 lines", every effort
size and the conversion's PR count. Wall-clock times are §2's idle-machine medians only.

**Not verified at all.**
- Every figure is clang on Linux; relative costs carry over, absolute ones do not.
- Nothing was built with MSVC: no compile, warning count, link order, ADL behaviour (D7) or code
  generation here is MSVC's, and §3's commits were proved on clang only. Three MSVC-only files that
  include the changed headers (`FrontierOutpost/Main.cpp` and the two shader registries) were read,
  not compiled.
- Release MSVC is unbuilt, and so is ARM64, which every project targets (ADR-006, ADR-014 decision
  8) and where `/fp:fast` contracts to FMA.
- The Windows file-system costs of loading and of hot reload (NTFS, Defender), and whether two saves
  inside one timer tick get distinct write times.
- The NT heap: every allocation cost here is glibc's (L1, L5, L9).
- The game itself: rendering-bound scripts cannot run headless, so the widget and object hooks, and
  re-entry through them, were covered by static analysis and audit builds only.
- MSVC's float formatting and parsing (L8) and its `/fp:fast` results (§6); C4458 on constructor
  parameters; whether `/JMC` is on in Debug.

**Measurements that need Windows**, in the order they unblock items:
1. §3 as Debug|x64, Release|x64 and ARM64, the game launched, the integer-print and re-entry scripts
   run.
2. A PIX or ETW sample of ltheory and war with the HUD, and per-frame counters in
   `ScriptFunctionT::Call` and `WidgetDynamic` (L1, L2, L4).
3. µs per `ScriptFunction_Load` for three scripts, and ltheory's startup time (L6).
4. SUPPORT's differential programs and every numeric literal, bitwise (L8).
5. Release `loadall` with and without the pool, and a per-frame measurement in a fight (L9).
6. The registry and surface dumps from the executable's link, before and after D7's patch (D7, L11).
7. CONFORM's native differential under both `/fp` models: x64 Debug and Release, ARM64 Release (§6).
8. `/W4` warnings with the marker removed from the 75 files, and `RunClangTidy.py` in its clang-cl
   mode, against the Linux counts (§6).
