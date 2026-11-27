---
name: clean-code
description: Clean-code principles for writing, changing and reviewing code in this engine. Use whenever writing or editing source code, tooling or build scripts, preparing a commit or PR, or reviewing a diff. Covers naming, function design, structure, platform specializations, comments, verification and commit hygiene.
---

# Clean code

Apply these whenever code is written, changed or reviewed. They are not optional style tips: a
change that breaks one of them is not finished. When a rule and an existing file disagree, follow
the rule in new code and mention the old pattern instead of copying it ("the class already does
it" is never an argument).

## Naming

- A name says what the thing does or holds. `checkServices` says nothing; `restartStoppedServices`
  does. Rename vague names you touch, and flag them in reviews.
- Functions are verbs, values are nouns, booleans read as questions (`isSettled`, `hasSword`).
- No abbreviations a newcomer would have to guess; established domain terms are fine.
- New identifiers, files, macros and symbols carry **no project name** (no `deceptus`): the
  project may be renamed. Existing names stay unless a rename is requested.
- Follow the repository's casing and formatting (C90, camelCase functions,
  UPPER_CASE constants, CamelCase types, three-space indent, braces on their own lines,
  declarations at block start).

## Functions

- One function, one job, at one level of abstraction. If describing it needs "and", split it.
- Setters set and return `void`. No chaining setters returning a reference.
- No hidden side effects: a function named like a query does not change state.
- Prefer early returns over deep nesting; keep parameter lists short (group related values in a
  struct rather than adding a sixth argument).
- No duplicated logic. Extract the shared part; for data, describe the difference instead of
  copying the whole (e.g. a per-platform step override instead of a second route file).

## Structure and boundaries

- Shared code is the reference behaviour on every platform and never names a CPU or console.
  No `#if CPU` blocks in shared files.
- Platform specializations live in the platform's own folder or tooling, sorted by concern
  (deceptus-console: `system/`, `video/`, `audio/`, and `mechanisms/` for a mechanism's platform
  presentation, named like the shared file):
  - optimized primitives behind a declared interface with an exact contract and a portable
    reference implementation (`include/arith.h`, `PLATFORM_ARITH`,
    `src/platform/<platform>/platform_arith.h`);
  - whole-function replacements as a separate source file the platform build selects;
  - intentional behaviour differences (content limits, re-timed animations) in platform tooling,
    with that platform's reference adjusted and the limit kept away from other targets.
- Every specialization is checked against its reference (function-level comparison over broad
  inputs, plus the platform's end-to-end replay). Undeclared drift is a bug.
- Fixed layouts shared between C and assembly are tied together by compile-time checks, never by
  convention alone.

## Expressions

- Use parentheses wherever they make a condition read intuitively, even when precedence makes
  them redundant: `(frame >= LAST) || (water == 0)`, `(flags & MASK) != 0`. Never make the
  reader recall the precedence of mixed `||`, `&&`, `!`, comparisons and bit operators.

## Constants, comments and leftovers

- No magic numbers: name them, and assert relations between them at compile time.
- Comments explain *why* (a constraint, a cycle cost, a hardware rule), not *what* the next line
  does. Keep them true when the code changes.
- No dead code, commented-out code, debug counters, profiling hooks or temporary prints in a
  commit. Instrumentation stays in local patches.
- Optimizations that change no behaviour must be exact: same results for all inputs, proven by
  a test against the previous or reference implementation.

## Change discipline

- Keep the change to what was asked; note unrelated problems instead of fixing them silently.
- Update the documentation that describes what you changed, in the same PR.
- After a change to shared code or tooling, verify **every** platform that consumes it, not only
  the one being worked on (`deceptus.py verify gba` / `smd`, the SNES verifier,
  Amiga build plus a Kickstart 1.3 boot, host unit tests, and CMake, which CI builds too).
- Test scripts and test code stay local: stage files by path, never `git add -A` in a tree with
  local harnesses; report test results in the PR description.
- Commits and PRs: describe what changed and why, and list what was checked.

## Review checklist

When reviewing (own diff before committing, or someone else's):

1. Do names say what things do? Any vague or project-prefixed new names?
2. Any setter returning something, any query with side effects?
3. Any function doing two jobs, any duplicated logic?
4. Any CPU or platform condition in shared code, any platform limit leaking to other targets?
5. Any magic number, stale comment, debug leftover or dead code?
6. Is every optimization proven exact, every specialization checked against its reference?
7. Are docs updated, and was every consuming platform verified?
