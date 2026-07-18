# PRP — P1.M1.T1.S1: Write `pattern_match.h`

## Goal

**Feature Goal**: Create `pattern_match.h` — the public C header that declares the
single public function of the Thompson-NFA pattern matcher and exhaustively
documents its contract in a `/** */` doc comment.

**Deliverable**: The file `pattern_match.h` at the repository root
(`/home/dustin/projects/qmk-notifier/pattern_match.h`), ~53 lines, containing
`#pragma once`, `#include <stdbool.h>`, the full doc comment, and exactly one
function declaration.

**Success Definition**:
- `wc -l pattern_match.h` reports ~53 lines (reference is exactly 53).
- `gcc -fsyntax-only -x c pattern_match.h` parses with zero warnings/errors.
- A one-line C stub that `#include "pattern_match.h"` and references
  `pattern_match(...)` parses cleanly under `gcc -fsyntax-only -Wall -Wextra`.
- Header contains exactly ONE externally-visible symbol: `pattern_match`.
- The 8 host test programs (task P1.M4) will later compile unchanged against it.

## User Persona (if applicable)

**Target User**: C developer rebuilding the qmk-notifier matcher, and the
downstream consumers — host test programs and `notifier.c`.

**Use Case**: Host test programs `#include "pattern_match.h"` to get the
declaration of the function under test; `notifier.c` calls `pattern_match()`
(though it includes the `.c` directly, the `.h` is the canonical public contract).

**Pain Points Addressed**: Defines a stable, documented public API boundary so the
implementation (`pattern_match.c`, task P1.M1.T2) and the tests (P1.M4) can be
built against a fixed contract.

## Why

- **First file in the project** — establishes the public contract everything else
  (the `.c` implementation, all 9 test suites, `notifier.c`) depends on.
- The doc comment IS the documentation (PRD §6 Mode A) — there is no separate
  docs file; the header must be self-documenting.
- Per PRD §17 "Living source of truth": the production codebase is authoritative;
  where spec and code disagree, "the code + the passing tests win."

## What

A minimal, dependency-free public C header with:
1. `#pragma once` include guard.
2. `#include <stdbool.h>` (for the `bool` type).
3. A comprehensive `/** ... */` doc comment reproduced **verbatim**.
4. Exactly one function declaration:
   ```c
   bool pattern_match(const char *pattern, const char *str, bool case_sensitive);
   ```
5. **No** internal declarations — `process_escapes`, `parse_pattern`, the NFA
   types/ops, character-classification helpers, etc. are all `static`/file-scope
   inside `pattern_match.c` and MUST NOT appear in this header.

### Success Criteria

- [ ] File `pattern_match.h` exists at repo root and is ~53 lines.
- [ ] Contains `#pragma once` as the first non-blank line.
- [ ] Contains `#include <stdbool.h>`.
- [ ] Contains the full verbatim doc comment (all 10 sections — see below).
- [ ] Contains exactly the one declaration of `pattern_match`.
- [ ] `gcc -fsyntax-only -x c pattern_match.h` succeeds with no diagnostics.

## All Needed Context

### Context Completeness Check

_Pass_: This is the first file with no project-internal dependencies. The full
content contract (the verbatim doc comment) is provided inline below and the
reference (`pattern_match.h`, 53 lines) is the live source of truth. An
implementer with only this PRP + the repo can produce the file byte-identically.

### Documentation & References

```yaml
# MUST READ — the authoritative contract
- url: ""    # (local) PRD.md §6 "File Specification: pattern_match.h"
  file: PRD.md
  section: "## 6. File Specification: `pattern_match.h`"
  why: "States: 'One function, exhaustively documented in the doc comment
        (reproduce the doc comment verbatim in a rebuild).' Plus the contract
        bullets: wildcard *, anchors ^/$, escapes \\^ \\$ \\* \\\\, NULL→false,
        thread-safe, internal memory mgmt, no-anchors⇒substring."
  critical: "The verbatim doc comment block to reproduce lives in the existing
             pattern_match.h (source of truth). See the 'VERBATIM DOC COMMENT'
             block in Implementation Tasks below."

- url: ""    # (local) PRD §17 — sizing/source of truth
  file: PRD.md
  section: "## 17. Appendix C — File Sizes & Live Source of Truth"
  why: "pattern_match.h ≈ 53 lines; 'the code + the passing tests win' over the spec."
  critical: "Tolerates minor line drift; the 53-line target is a ballpark, not a hard cap."

- url: ""    # (local) architecture overview
  file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "## Overview"
  why: "Confirms the public API is a single function and the NULL→false + linear-time invariants."
  critical: "Reinforces that ONLY `pattern_match` is public; everything else is file-scope in the .c."

- url: ""    # (local) PRD §15 — pattern-semantics reference
  file: PRD.md
  section: "## 15. Appendix A — Pattern-Semantics Reference Table"
  why: "Authoritative behavior the doc comment's EXAMPLES must align with
        (substring vs ^/$ exact vs escapes)."
  critical: "No new behavior is invented here — the doc comment only documents
             what §15 and the existing header already state."

# Toolchain reference (for validation gates)
- file: run_all_tests.sh
  why: "Shows the build style: plain gcc, no make/cmake. e.g.
        `gcc -o test_pattern_match test_pattern_match.c pattern_match.c`."
  gotcha: "pattern_match.c does NOT exist yet at this stage (task P1.M1.T2);
           validation for this header is syntax-only, NOT linking."
```

### Current Codebase tree (run `tree`/`ls` at repo root)

```bash
pattern_match.h        # ← the file to (re)create (this task). Currently present as source-of-truth reference.
pattern_match.c        # exists as reference; NOT to be touched/created here (later task P1.M1.T2)
notifier.h  notifier.c # P2 scope — do not touch
rules.mk              # P2 scope — do not touch
test_*.c              # P1.M4 scope — do not touch; these are the consumers that #include "pattern_match.h"
run_all_tests.sh      # P1.M4 scope — do not touch
PRD.md                # READ-ONLY
plan/                 # plan/architecture + this PRP — do not modify (except your own PRP/research)
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
pattern_match.h        # (re)written by THIS task. Public contract: pragma once,
                       #   <stdbool.h>, full doc comment, single pattern_match() decl.
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL: This is a REBUILD. The existing pattern_match.h (53 lines) is the
//   live source of truth (PRD §17). Reproduce the doc comment VERBATIM; do not
//   paraphrase, do not add/remove @note tags, do not reorder EXAMPLES.
//
// GOTCHA: Only ONE public symbol. Do NOT forward-declare any internal helper.
//   process_escapes / parse_pattern / free_parsed_pattern / nfa_* / is_*_char /
//   is_word_boundary / pattern_char_matches / match_with_anchors / parsed_pattern_t
//   / State / OP_* enum / NFA_MAX_* are ALL static/file-scope inside pattern_match.c.
//   Putting them in the .h would leak the private API and break the "one public
//   function" contract.
//
// GOTCHA: `bool` requires <stdbool.h>. The function returns `bool`, not `int`.
//
// GOTCHA: `#pragma once` is the chosen guard (the reference uses it; do NOT
//   switch to #ifndef/#define/#endif include guards).
//
// GOTCHA: pattern_match.c does not exist yet during this subtask. Do NOT attempt
//   to compile a full program that links against pattern_match() — it will fail
//   at link time by design. Validate with -fsyntax-only.
//
// GOTCHA: The header is C, not C++. It is consumed by host gcc test builds and
//   (transitively, via the .c include) by QMK firmware builds. No C++ guards
//   (`extern "C"`) are needed — the reference header has none.
```

## Implementation Blueprint

### Data models and structure

None. The header declares no types, no structs, no macros, no constants. It
exposes one function only. (All data structures — `parsed_pattern_t`, `State`,
the `OP_*` enum, `NFA_MAX_*` — are file-scope in `pattern_match.c`.)

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: WRITE pattern_match.h  (repo root — single file, no subdirectories)
  - CREATE the file with exactly this content, in this order:
      1. `#pragma once`
      2. blank line
      3. `#include <stdbool.h>`
      4. blank line
      5. the full `/** ... */` doc comment (VERBATIM — see block below)
      6. `bool pattern_match(const char *pattern, const char *str, bool case_sensitive);`
  - NAMING: function is lowercase snake_case `pattern_match` (matches reference + all callers).
  - PLACEMENT: repository root (same directory as the future pattern_match.c and
    the test_*.c consumers, so `#include "pattern_match.h"` resolves with `-I.`).
  - LINE COUNT: target ~53 lines (the reference is exactly 53). Tolerate ±2.
  - DO NOT add: include guards (#ifndef), extern "C", internal declarations,
    header-wide macros, TODO comments, or a trailing implementation.
  - PRESERVE: the exact 8 EXAMPLES, the exact three @note lines, and the exact
    @param/@return wording from the source of truth.
  - DEPENDENCIES: none — this is the first file.

# === VERBATIM DOC COMMENT (reproduce exactly, between the /** and */) =========
# Source: existing pattern_match.h (live source of truth, PRD §17).
# The implementer MUST reproduce this block character-for-character. If the
# existing pattern_match.h is present in the working tree, the safest path is to
# confirm this text matches it before writing.
```

The exact doc comment + declaration to write (reproduce verbatim):

```c
#pragma once

#include <stdbool.h>

/**
 * Enhanced pattern matching with regex anchor and escape sequence support.
 * 
 * This function performs pattern matching with support for:
 * - Wildcard characters (*) for matching any sequence of characters
 * - Anchor characters (^ and $) for position-specific matching
 * - Escape sequences (\^, \$, \*, \\) for literal character matching
 * - Case-sensitive and case-insensitive matching
 * 
 * ANCHOR CHARACTERS:
 * - ^ at the start of pattern: matches only from the beginning of the string
 * - $ at the end of pattern: matches only to the end of the string
 * - ^...$ together: exact string matching (entire string must match)
 * 
 * ESCAPE SEQUENCES:
 * - \^ matches literal ^ character
 * - \$ matches literal $ character  
 * - \* matches literal * character
 * - \\ matches literal \ character
 * 
 * WILDCARD BEHAVIOR:
 * - * matches any sequence of characters (including empty sequence)
 * - Can be combined with anchors: ^prefix*suffix$ 
 * 
 * BACKWARD COMPATIBILITY:
 * - Patterns without anchors work exactly as before (substring matching)
 * - Existing wildcard functionality is preserved
 * - Case sensitivity behavior is unchanged
 * 
 * EXAMPLES:
 * - pattern_match("hello", "hello world", false) → true (substring match)
 * - pattern_match("^hello", "hello world", false) → true (starts with)
 * - pattern_match("world$", "hello world", false) → true (ends with)
 * - pattern_match("^hello$", "hello", false) → true (exact match)
 * - pattern_match("^hello$", "hello world", false) → false (not exact)
 * - pattern_match("\\^start", "^start", false) → true (escaped ^)
 * - pattern_match("end\\$", "end$", false) → true (escaped $)
 * - pattern_match("file\\*.txt", "file*.txt", false) → true (escaped *)
 * 
 * @param pattern The pattern to match against, may contain wildcards, anchors, and escapes
 * @param str The string to test for pattern matching
 * @param case_sensitive If true, performs case-sensitive matching; if false, case-insensitive
 * @return true if the string matches the pattern according to the specified rules, false otherwise
 * 
 * @note Returns false if either pattern or str is NULL
 * @note Memory is managed internally; no cleanup required by caller
 * @note Thread-safe (no global state modified)
 */
bool pattern_match(const char *pattern, const char *str, bool case_sensitive);
```

> **If the working tree already contains `pattern_match.h`**: confirm its content
> is byte-identical to the block above; if so, the file already satisfies this
> task and no write is needed (but still run the validation gates). If it differs,
> overwrite with the verbatim block above — the doc comment is the contract.

### Implementation Patterns & Key Details

```c
// The entire header. Nothing else. No macros, no types, no extras.
#pragma once
#include <stdbool.h>
/** ... full doc comment ... */
bool pattern_match(const char *pattern, const char *str, bool case_sensitive);

// ANTI-PATTERN to avoid: do NOT declare internals here, e.g.
//   typedef struct parsed_pattern_t parsed_pattern_t;   // ❌ private to .c
//   #define NFA_MAX_PATTERN 128                          // ❌ private to .c
//   bool process_escapes(...);                           // ❌ static in .c
// The header's job is ONLY the public contract.
```

### Integration Points

```yaml
CONSUMERS (no action needed — listed to explain the contract):
  - 8 host test files: #include "pattern_match.h"  (resolved via -I. / same dir)
  - notifier.c: includes "pattern_match.c" directly (NOT this .h), but the .h
    remains the canonical public contract.

BUILD:
  - No build-system change. Plain gcc (see run_all_tests.sh). No CMakeLists, no Makefile.

CONFIG:
  - None. Header introduces no macros/constants/config.

DATABASE/ROUTES:
  - N/A (C firmware module; no DB or HTTP).
```

## Validation Loop

> NOTE: There is no ruff/mypy/pytest here — this is a C project validated with gcc.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. The header must parse cleanly as a standalone translation unit.
# NOTE: -Wno-pragma-once-outside-header silences the benign warning gcc emits
# when a #pragma-once header is the main file (it is normally only #included).
gcc -fsyntax-only -x c pattern_match.h -Wall -Wextra -Wno-pragma-once-outside-header
# Expected: no output, exit code 0.

# 1b. Confirm exactly one public symbol is declared (no internals leaked).
grep -nE '^[a-zA-Z_].*pattern_match' pattern_match.h
# Expected: exactly one line — the `bool pattern_match(...)` declaration.

# 1c. Confirm required scaffolding is present.
grep -q '#pragma once'           pattern_match.h && echo "pragma ok"
grep -q '#include <stdbool.h>'   pattern_match.h && echo "stdbool ok"
grep -q 'bool pattern_match(const char \*pattern, const char \*str, bool case_sensitive);' \
  pattern_match.h && echo "decl ok"

# Expected: all three "ok" lines printed.
```

### Level 2: Unit Tests (Component Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# A header-only deliverable has no functions to link yet (pattern_match.c is a
# LATER task). Validate that the declaration is usable by creating a throwaway
# stub that includes the header and references the function, then syntax-check it.
cat > /tmp/pm_stub.c <<'EOF'
#include "pattern_match.h"
int main(void) {
    bool r = pattern_match("x", "y", true);
    (void)r;
    return 0;
}
EOF

gcc -fsyntax-only -Wall -Wextra -I. /tmp/pm_stub.c
# Expected: no output, exit code 0 (parses; linking is intentionally impossible
# until pattern_match.c is implemented in P1.M1.T2).

# DO NOT attempt: gcc /tmp/pm_stub.c   ← will fail at link time by design.
rm -f /tmp/pm_stub.c
```

### Level 3: Integration Testing (System Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# Line-count contract (~53; reference is exactly 53). Allow ±2.
LINES=$(wc -l < pattern_match.h)
echo "pattern_match.h: $LINES lines"
test "$LINES" -ge 51 -a "$LINES" -le 55 && echo "line count ok" || echo "line count OUT OF RANGE"

# Verify the doc comment carries all required @note/@param/@return tags and the
# EXAMPLES block — these ARE the documentation (Mode A).
for needle in "Enhanced pattern matching" "ANCHOR CHARACTERS" "ESCAPE SEQUENCES" \
              "WILDCARD BEHAVIOR" "BACKWARD COMPATIBILITY" "EXAMPLES" \
              "@param pattern" "@param str" "@param case_sensitive" "@return" \
              "Returns false if either pattern or str is NULL" \
              "Memory is managed internally" "Thread-safe"; do
  grep -qF "$needle" pattern_match.h || { echo "MISSING: $needle"; exit 1; }
done
echo "all doc-comment sections present"

# NOTE: Full program build/run (gcc test_*.c pattern_match.c) is deferred to
# later tasks — pattern_match.c is not written until P1.M1.T2.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Cross-check the EXAMPLES in the doc comment against PRD §15 semantics table so
# the documentation does not contradict the authoritative behavior reference.
# (Manual review: the 8 EXAMPLES must agree with PRD §15 rows for substring,
#  ^start, end$, ^exact$, escaped \^ \$.)
grep -nE 'pattern_match\("' pattern_match.h   # list the documented examples

# Optional: confirm no C++-ism / no extern "C" crept in (reference has none).
! grep -q 'extern "C"' pattern_match.h && echo "no C++ guards (correct)"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1 passed: `gcc -fsyntax-only -x c pattern_match.h -Wall -Wextra` → clean.
- [ ] Level 2 passed: stub include + function reference parses under `-fsyntax-only`.
- [ ] Level 3 passed: line count in [51, 55]; all doc-comment sections present.
- [ ] Level 4 passed: doc EXAMPLES consistent with PRD §15; no C++ guards.

### Feature Validation

- [ ] Exactly one public declaration (`bool pattern_match(...)`); no internals leaked.
- [ ] `#pragma once`, `#include <stdbool.h>`, full doc comment, declaration — present.
- [ ] Doc comment reproduced verbatim (8 EXAMPLES, 3 @param, 1 @return, 3 @note).
- [ ] File at repo root so `#include "pattern_match.h"` resolves for consumers.

### Code Quality Validation

- [ ] Matches reference header structure exactly (source of truth, PRD §17).
- [ ] No anti-patterns: no include guards, no extern "C", no internal decls, no macros.
- [ ] No modification to PRD.md, tasks.json, prd_snapshot.md, .gitignore, or any
      other source file.

### Documentation & Deployment

- [ ] The doc comment is self-documenting (Mode A) — no separate docs file needed.
- [ ] No new environment variables or config introduced.

---

## Anti-Patterns to Avoid

- ❌ Don't paraphrase the doc comment — reproduce it verbatim (PRD §6 + §17).
- ❌ Don't declare internal helpers/types/macros in the header — they are
  `static`/file-scope in `pattern_match.c`.
- ❌ Don't switch `#pragma once` for `#ifndef` guards.
- ❌ Don't add `extern "C"` — this is a C-only header (reference has none).
- ❌ Don't try to link a full program yet — `pattern_match.c` is a later task;
  validate with `-fsyntax-only`.
- ❌ Don't pad/trim lines arbitrarily — 53 is the target; the content is fixed.
- ❌ Don't touch any other file (PRD.md, tasks.json, test_*.c, notifier.*, rules.mk).

---

## Confidence Score: 10/10

This is the first, self-contained file in the project. Its exact content (53
lines, verbatim doc comment) is fully specified above and cross-checked against
the live source of truth and PRD §6/§15/§17. Validation uses only gcc
syntax-checks (the project's actual toolchain) plus targeted content/line-count
assertions. No external research is required; no ambiguity remains.
