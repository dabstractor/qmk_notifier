# Research Notes — P1.M1.T1.S1 (Write pattern_match.h)

## Task
Create `pattern_match.h`: the public header for the Thompson-NFA pattern matcher.
First file in the project. ~53 lines. Single function declaration + full doc comment.

## Critical project framing
This is a **one-shot rebuild** (see PRD "Definition of Done" and §17 "Living source
of truth"). The repo already contains `pattern_match.h` (and `.c`, `notifier.*`,
the full test corpus) — these are the **live source of truth** the PRD was
extracted from. Per PRD §17:
> "Where this spec and the code disagree, the code + the passing tests win."
And PRD §6:
> "One function, exhaustively documented in the doc comment (reproduce the doc
> comment verbatim in a rebuild)."

**Therefore**: the verbatim doc comment to reproduce IS the one in the existing
`pattern_match.h` (53 lines, verified via `wc -l`). The PRD §6 "Contract (from the
doc comment)" bullets are a *summary* of it, not the literal block. The implementer
must reproduce the full `/** ... */` block from the source of truth.

## Verified facts

### Reference header line count
`wc -l pattern_match.h` → **53 lines**. Matches contract "~53 lines" exactly.

### Structure of the canonical header (exact)
```
#pragma once
<blank>
#include <stdbool.h>
<blank>
/** ... ~45 line doc comment ... */
bool pattern_match(const char *pattern, const char *str, bool case_sensitive);
```

### Doc comment sections (all must be reproduced verbatim)
1. Brief line: "Enhanced pattern matching with regex anchor and escape sequence support."
2. Support summary (wildcards, anchors, escapes, case)
3. ANCHOR CHARACTERS (^ start, $ end, ^...$ exact)
4. ESCAPE SEQUENCES (\^ \$ \* \\)
5. WILDCARD BEHAVIOR (* any sequence incl empty; combinable with anchors)
6. BACKWARD COMPATIBILITY (no anchors ⇒ substring, unchanged)
7. EXAMPLES (8 examples: substring, ^prefix, suffix$, ^exact$, ^reject$, \^, \$, \*)
8. @param pattern / @param str / @param case_sensitive
9. @return true/false
10. @note NULL → false ; memory managed internally ; thread-safe (no global state)

### Consumers (inclusion contract)
- 8 host test files include via `#include "pattern_match.h"`:
  test_pattern_match.c, test_char_classification.c, test_word_boundary_basic.c,
  test_word_boundary_integration.c, test_metachar_verification.c,
  test_comprehensive_integration.c, test_error_handling.c, test_invalid_patterns.c,
  test_memory_stress.c
- `notifier.c` includes `#include "pattern_match.c"` (NOT the .h), but the .h is
  still the canonical public contract.

### What is NOT in the header (do NOT declare)
All internal machinery is file-scope (`static`) inside pattern_match.c:
process_escapes, parse_pattern, free_parsed_pattern, get_escaped_char,
nfa_compile, nfa_addstate, nfa_match, nfa_has_match, is_*_char, is_word_boundary,
pattern_char_matches, match_with_anchors, match_string_with_start,
match_reaches_end_with_start, parsed_pattern_t, State, OP_* enum, NFA_MAX_*.
The header exports EXACTLY ONE symbol: `pattern_match`.

### Build toolchain (for validation gates)
- Compiler: gcc (GCC 16.1.1) at /usr/bin/gcc; clang also present.
- Build style: direct gcc invocation, no makefile, no cmake.
  Example from run_all_tests.sh:
  `gcc -o test_pattern_match test_pattern_match.c pattern_match.c`
  Integration test uses: `gcc ... -std=c99 -DNOTIFIER_STUB`
- No linter/formatter configured (C project). The relevant gate is the compiler.

### Header-only validation (pattern_match.c does NOT exist yet at this stage —
it is task P1.M1.T2, a later sibling). So validation for THIS subtask is:
- Header parses cleanly as a translation unit on its own:
  `gcc -fsyntax-only -x c pattern_match.h`
- A one-line stub that includes the header and references the declared function
  parses cleanly (link not possible/expected — impl deferred):
  create stub → `gcc -fsyntax-only -Wall -Wextra stub.c`
- Line count: `wc -l pattern_match.h` ≈ 53.
- Content presence checks (grep).

### .gitignore
`test_*` binaries are ignored, `!test_*.c` keeps sources. Header is tracked. No
plan/ or PRD entries — leave .gitignore untouched.

## Scope boundaries (do not over-reach)
- This subtask writes ONLY pattern_match.h.
- Do NOT create pattern_match.c (that's P1.M1.T2).
- Do NOT write tests (P1.M4).
- Do NOT modify PRD.md, tasks.json, prd_snapshot.md, .gitignore.
