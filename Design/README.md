# Design — standards for build and design sessions

This directory is the **design record** for *Frontier Outpost*: what the game is, what has been decided, and why. [`AGENTS.md`](../AGENTS.md) at the repository root is the **conformance record**: how code is written, built and verified.

The split is worth stating plainly, because putting a rule in the wrong one is how both stop being read:

| | `AGENTS.md` | `Design/` |
|---|---|---|
| Answers | *How do I write this correctly?* | *What are we building, and why this way?* |
| Changes when | A convention or a gate changes | A decision is taken, revised or abandoned |
| Enforced by | `.clang-tidy`, `.clang-format`, `Build/*.py`, CI | Review, and by whoever notices the code no longer matches |
| Audience | Every session, every time | The sessions whose task touches that subsystem |

**Read AGENTS.md every session. Read the part of `Design/` your task touches, before you start.**

---

## 1. The baseline this game is designed against

These are settled. They are not preferences to be re-litigated in a session; changing one is an ADR and an owner decision, in that order.

| | Decision |
|---|---|
| **Presentation** | 640×400, **16 colours**. A fixed paletted framebuffer, scaled to the window by a **whole number** (2× today). A fractional scale is what turns a crisp legacy screen into mush, so integer scaling is a design constraint, not a default. |
| **Graphics API** | Direct3D 12, on Windows 11. The legacy look is a deliberate aesthetic on a modern stack — not a limitation being worked around, and not a reason to reach for an older API. |
| **Language** | C++23 (`/std:c++latest` under MSVC v145), `/permissive-`, `/W4` with warnings as errors. |
| **Platform** | x64 only. |
| **Shape** | One executable. `FrontierOutpost.exe` starts the client and the authoritative server in the same process. |
| **Distribution** | **The executable ships alone.** No assets folder, no data directory. Art, palettes, fonts, audio and compiled shaders are embedded in the binary (AGENTS.md R13). |
| **Authority** | The server is authoritative. `GameLogic` is server-side and the client never links it (AGENTS.md §2). |
| **Dependencies** | The Windows SDK and the MSVC standard library. Nothing else (AGENTS.md R14). |

Every one of these constrains design work in a way that is easy to forget mid-session. A UI mock at 1920×1080, a texture atlas loaded from disk, a `std::print` of a wall-clock timestamp inside the simulation — each is a perfectly good idea that this game has already decided against.

---

## 2. What lives here

```
Design/
  README.md                 ← this file: the standards
  ADR/                      ← decisions, numbered, immutable once Accepted
    ADR-000-template.md
    ADR-001-....md
  Plans/                    ← work in flight: what is being built, in what order
  Reference/                ← things that are true rather than decided (formats, tables, maths)
  Archive/                  ← superseded plans and finished reviews, kept for the record
```

**Which one is it?** The test is what happens when the document turns out to be wrong.

- An **ADR** records a decision with alternatives that were genuinely available. If it turns out wrong, you write a *new* ADR that supersedes it — you do not edit the old one. Transport choice, tick rate, the palette format, how ships are spatially indexed.
- A **Plan** records intended work. If it turns out wrong, you edit it. It is expected to change every week and to be moved to `Archive/` when it is done.
- A **Reference** records something that is simply the case. If it turns out wrong, it was a bug in the document. Wire record layouts, the 16-colour palette table, coordinate conventions.
- **`Archive/`** is where a plan goes when it is finished and where a review goes when it is answered. Nothing in `Archive/` is authoritative; it exists so a future session can find out why something was done.

If you cannot tell which a document is, it is probably two documents.

---

## 3. Writing standards for everything in this directory

These are the rules that make the record trustworthy enough to act on without re-deriving it.

**1. State what is true, not what is intended.** The most expensive failure mode in a design record is a document that describes a system in the present tense before it exists. If it is not built, say so: "planned", "not implemented", "stage 3 of the plan". A future session — human or agent — will read the present tense as a fact and build on it.

**2. Figures are measured, not estimated.** If you quote a frame time, a packet size, an entity count or a build duration, say how you measured it and when. If you are estimating, write "estimated" and show the arithmetic. An unsourced number becomes a requirement within two documents.

**3. Dates are absolute.** "2026-09-09", never "last week" or "recently". Sessions are read months later and out of order.

**4. Record what you rejected and why.** A decision without alternatives is not a decision, it is a description. The rejected option is what a future session needs when the accepted one starts hurting — otherwise it re-proposes the same idea and re-discovers the same problem.

**5. Link, do not duplicate.** If `.clang-tidy` sets a value, cite it; do not restate it. Two copies of a number is one wrong number waiting. This applies across `Design/` too — one document owns each fact.

**6. Say who decided.** "Owner decision, 2026-09-09" is a complete provenance. A rule with no owner is one nobody can change.

**7. Prose, not bullets, for reasoning.** Bullets are for enumerations. A rationale compressed to a bullet loses exactly the part a future reader needed — the "because".

---

## 4. Design sessions

A **design session** produces documents, not code. It ends with a decision recorded, a plan written, or a question sharpened — never with a design "discussed".

**Before you start**

1. Read `AGENTS.md` §1–§3 and the baseline in §1 above.
2. Read every existing ADR that touches your subsystem, and the plan that covers it. `ADR/` is small; when in doubt read all of it.
3. Read the code the design describes. A design session that never opens the tree writes fiction.

**While you work**

- **Bring the constraint list to every choice.** Most bad designs here are good designs for a different game — one that ships an assets folder, or draws at native resolution, or runs the simulation on the client.
- **Name the alternatives before you pick.** Two is a comparison, one is a decision that has already been made.
- **If the design forces a rule in `AGENTS.md` to bend, say so explicitly in the document.** Do not quietly write a design that cannot be implemented under the conformance rules.

**When you finish**

- One ADR per decision, numbered next in sequence, using [`ADR/ADR-000-template.md`](ADR/ADR-000-template.md).
- Update or supersede whatever your decision invalidated, in the same commit.
- If the decision changes how code must be written — a new banned pattern, a new invariant — **that belongs in `AGENTS.md`**, added in the same commit, with the ADR cited.
- Say in your report what you decided, what you rejected, and what you left open.

### ADR lifecycle

`Proposed` → `Accepted` → (`Superseded by ADR-0NN` | `Deprecated`).

An **Accepted** ADR is immutable except for its status line. Corrections, refinements and reversals are new ADRs that name the one they supersede. This is the whole point of the format: the record shows what was believed at the time, which is what makes it possible to work out why the code looks the way it does.

---

## 5. Build sessions

A **build session** produces code. `AGENTS.md` governs how; this section governs its relationship to the design record.

**Before you start**

1. Read `AGENTS.md`, in full, every session.
2. Read the ADRs and the plan covering what you are about to touch. Implementing against a stale mental model is the most common way a session's output has to be thrown away.
3. Check that the design actually describes what is in the tree. If it does not, **that is a finding** — report it, and fix the document if the code is right.

**While you work**

- **Implement the decision that was recorded, not the one you would have made.** If you think the recorded decision is wrong, say so in your report and implement it anyway, or stop and ask. Silently designing something else is the failure this whole directory exists to prevent.
- **A decision you have to make mid-implementation, that has alternatives, is an ADR.** Do not bury it in a commit message. Small, obvious choices are not decisions; if you find yourself writing a paragraph of justification in a code comment, it was one.

**When you finish**

- Update `Design/` for anything your change completed or invalidated, in the same commit as the code.
- Move a finished plan to `Archive/`.
- Work through AGENTS.md §7 honestly.

---

## 6. Definition of done

A session — either kind — is done when all of these are true. Not "mostly", and not "the remaining bit is trivial".

- The task as asked is complete, or the part that is not is stated explicitly and in full.
- Every claim in the report is something you actually verified. "Builds clean, not run" and "builds and runs" are different claims; say which.
- The design record and the code agree, or the disagreement is written down.
- `Build/CheckFormat.py`, `Build/CheckProjectFiles.py` and `Build/RunClangTidy.py` pass, and Debug|x64 builds and its four test suites run — for a build session.
- Assumptions are listed. Rules bent are listed. Things noticed and deliberately left alone are listed.

The last line is not paperwork. A session that reports only its successes is a session whose output has to be re-verified from scratch, which costs more than the session saved.
