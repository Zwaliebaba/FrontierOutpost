# LTSL interpreter: performance, simplification and the path to AGENTS.md

- **Status:** In progress, 2026-09-27. This first version records the review's brief and its
  measured baseline. The ranked findings, the owner decisions and the quick wins follow on the same
  PR once each finding has been verified adversarially. Nothing here is approved. The owner
  approves items by ID, and each approved item that is not a quick win lands as its own PR
  (AGENTS.md §6), with its ADR in the same commit where the item is a decision. The next free ADR
  number is ADR-018.
- **Scope:** the LTSL interpreter in `NeuronCore/`:
  - the front end: `Tokenizer.h`, `StringList.*` and `LTSL.*`;
  - the compiler and the evaluator: `Expression.*`, one `Expression_*` node per file (`Access.cpp`
    to `While.cpp`), `Environment.h`, `Script.*`, `ScriptFunction.*` and `ScriptType.h`;
  - the reflection and binding layer: `Type.*`, `Data.h`, `Function.*`, `DeclareFunction.h`,
    `Function_Generated.h` and `AutoClass*.h`, with the containers and handles under them.

  The `ScriptApi*.cpp` files in NeuronCore, NeuronClient, GameLogic and FrontierOutpost consume the
  binding layer and are in scope only as consumers. The 121 scripts in `GameData/script/` are the
  ground truth of what the language must keep doing.
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
- **Source:** a multi-agent review on 2026-09-27, at `2fb3cde`. Six read-only reviewers each took a
  lens: the evaluator's hot path, the compiler and load path, the binding layer, the support
  library, the conversion to AGENTS.md, and correctness. A seventh pass verifies each finding
  adversarially. The interpreter files are unchanged between `2fb3cde` and `94c4b8a`, the base of
  this branch.

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
function's signature and emitted text, and dumps the registered native surface (1,056 functions and
391 types). The two dumps are the golden baselines that a quick win must reproduce byte for byte.

Costs are compared as callgrind instruction counts, which do not vary between runs. Times are
medians of 5 or 7 runs on an idle machine. **Every figure is clang on Linux, not MSVC on Windows.**
Relative costs carry over; absolute times do not, and the file-system costs of the load path are
far higher on Windows.

Whether the harness should live in the repository, for example as `Tools/LtslHarness/`, is a
decision for the owner: it is a second build of the engine's sources, for Linux, beside the
MSBuild one.

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
`DataStack.h`, a bump allocator, is used by nothing.

| Benchmark | Instructions | `malloc` and `free` | Native-call node | Other notable |
|---|---|---|---|---|
| IntLoop | 576 M | 24% | 41% | the block node 11%, the variable node 6% |
| ScriptCall | 973 M | 29% | 24% | the script-call node 14% |
| MethodCall | 1,298 M | 32% | 25% | the script-call node 10% |
| VecMath | 1,287 M | 44% | 25% | the implicit-conversion node 1.2% |
| Branch | 1,895 M | 41% | 30% | the switch node 2.6% |
| StringOps | 376 M | 23% | 7% | each `Int` to `String` conversion builds a `std::stringstream` and a locale, about 20% |

### Load

- Compiling all 121 scripts takes **53.8 ms** (median of 7). Summed over the scripts, reading the
  files takes 1.6 ms, parsing 8.5 ms and the infix rewrite (`LTSL_ApplyRewrites`) 5.5 ms, which
  leaves about 38 ms for the compiler.
- One such pass opens files under `GameData/script/` **1,005 times**, about 7 times per script:
  `Exists`, `GetHash` and `ReadAscii` each open the file again (strace).
- **`ScriptFunction_Load` re-reads the scripts on every call.** `AUTORELOAD` (`Script.cpp:17`,
  `:188-190`) makes every call run `Script_Reload`, which reads and hashes the script and its whole
  dependency closure
  again, even when nothing has changed. On an already-loaded script, one call costs 9.9 µs for
  `Object/Ship:Init`, 46 µs for `Icons:Station`, 100 µs for `Object/System:Init` and 276 µs for
  `Object/Colony:Init`, on Linux with a warm cache. GameLogic makes such a call for each object it
  creates: `Ship.cpp:92`, `Colony.cpp:12`, `Thruster.cpp:12`, `Region.cpp:153`,
  `StationType.cpp:42`, `PlanetType.cpp:21`, `ScannerType.cpp:43`, `Player.cpp:20`,
  `Wormhole.cpp:60`, `WarpNode.cpp:441` and the `*Type.cpp` generators. `WarpNode.cpp:237` and
  `:288` keep theirs in a static.

## 3. Defects confirmed so far

1. **Scalar fields of script types start uninitialized, and so do locals declared without a value.**
   A fresh `Counter` with an `Int count` field returned 1607693205, 475454879 and -169129822 on
   three runs, and memcheck reports the value as created by a heap allocation. The cause is that
   `__type_default_allocator<T>` is `new T` and `__type_default_construct<T>` is `new (buf) T`
   (`Type.h:386`, `:406`): default-initialization, which leaves scalars indeterminate. Value
   initialization would make them zero. Since that changes what a script can observe, it is a
   decision, not a quick win.
2. **Each method definition leaves a null entry in its script's function table.**
   `ExpressionFunction.cpp:26` checks for an existing function with `env.script->functions[name]`,
   and `Map::operator[]` inserts. After the whole corpus compiles, the tables hold 311 null entries
   beside the 702 functions.
3. **The stack-frame scopes are compiled into Release.** `BuildMode.h:7` defines `BUILD_DEBUG`
   unconditionally, so `FRAME` and `SFRAME` (a `StackFrame_Push`/`Pop` and a
   `Profiler_Push`/`Pop`) run in every configuration: in `ScriptFunctionT::Call`
   (`ScriptFunction.cpp:36`), and in every `Expression_Compile` (`Expression.cpp:26`). The engine's
   sampling profiler attributes script time through these scopes, so removing them is a trade-off.
