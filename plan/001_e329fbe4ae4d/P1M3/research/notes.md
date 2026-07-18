# Research Notes — P1.M3: Character Classification & Anchor Strategy

## Scope (from plan_status)

- **P1.M3.T1.S1** — `is_digit_char`, `is_word_char`, `is_whitespace_char`,
  `is_word_boundary` (PRD §7.6)
- **P1.M3.T2.S1** — `pattern_char_matches` (PRD §7.7)
- **P1.M3.T2.S2** — `match_with_anchors` (real) + `match_string_with_start` +
  `match_reaches_end_with_start` (PRD §7.4)

All are `static`/file-local in `pattern_match.c`. None are reachable by tests
directly — they are exercised **indirectly** via the public `pattern_match()`.

## Source of truth

PRD §17: "the code + the passing tests win." The live `pattern_match.c` is the
authoritative reference (implemented, 2018/2019 tests pass). The PRP must
reproduce it branch-for-branch. This is a "from-scratch rebuild" milestone; the
implementer rebuilds P1.M3's functions to match the live code's behavior.

## P1.M2 contract (the parallel milestone — what exists when P1.M3 starts)

Per `plan/001_e329fbe4ae4d/P1M2/PRP.md`, when P1.M3 begins the file contains:

- The **P1.M1 pipeline** (complete): `process_escapes`, `parsed_pattern_t`,
  `get_escaped_char`, `free_parsed_pattern`, `parse_pattern`, public `pattern_match`.
- The **P1.M2 NFA engine** (complete):
  - Sizing/`State`/`OP_*`/`static int nfa_gen = 0;`
  - `nfa_compile`, `nfa_addstate`, `nfa_has_match`, `nfa_match`
  - A **STUB** `is_word_boundary` returning `false` (so the TU links —
    `nfa_addstate` references it; GCC emits the static `nfa_addstate` even when
    unused → undefined reference without the stub).
  - A **STUB** `match_with_anchors` returning `false` (so `pattern_match` links).
  - **Forward declarations** for `nfa_match`, `match_string_with_start`,
    `match_reaches_end_with_start` (so the stub/real `match_with_anchors` can
    reference them before their definitions).

⚠️ **P1.M2 G2 fix is still pending in the live code**: `NFA_MAX_PATTERN` is a
plain `#define 2048` with NO `#ifndef` guard (the P1.M2 PRP requires the guard).
That is **P1.M2 scope** — P1.M3 must NOT fix it, only reference it as a contract
item. P1.M3 adds nothing to sizing.

## Verified baseline (live code, this repo)

- `bash run_all_tests.sh` → **2018/2019** pass. The 1 failure is
  `"Anchored huge pattern exact match"` in `test_memory_stress.c` — the **G3**
  ~40 KB pattern vs fixed-stack-pool conflict (P1.M4/P3 scope, NOT a P1.M3 defect;
  documented in P1M2/PRP.md Known Gotchas §G3).
- §11.2B pathological stress `a+a+a+a+a+a+a+a+a+a+b` vs 199×`a` → `result=0`
  in **~1.8 ms** (< 50 ms gate). The linear `X+` from P1.M2 is what makes this
  fast; P1.M3 owns no part of it but the anchor strategy routes it correctly.
- Performance: **0.106 µs/call** for `*chrome*` (PRD §12 sub-µs requirement met).

## P1.M3 behavioral probe (all PASS against live code)

Built `/tmp/m3_probe.c` linking `pattern_match.c`, ran 23 cases covering every
P1.M3 responsibility:

| Construct | Behavior verified |
|---|---|
| `\d \D \w \W \s \S` | class membership via `pattern_char_matches` → `is_*_char` |
| `.` vs `*` newline | dot **excludes** `\n`; glob **includes** `\n` (§13 #8) |
| `\+` `\.` | escaped-literal decode via `get_escaped_char` + case-fold |
| `\bword\b` substring, `^word\b` | word-boundary **absolute position** (§13 #10) |
| `^hello$`, `^hello`, `world$`, `abc` | all 4 anchor strategies (§7.4) |
| case-sensitivity | `abc`/`ABC` cs=0 true, cs=1 false |
| `^$` empty | exact empty match |
| empty core | `""` vs `"x"`→false, vs `""`→true (special case) |

0 failures. This probe is reused in the PRP Level-2 gate.

## Key placement / ordering constraints (live code layout)

P1.M3 touches 4 sites in `pattern_match.c`:

1. **`match_with_anchors` site** (~line 222, right after `parse_pattern`, BEFORE
   public `pattern_match`): REPLACE the P1.M2 stub with the real anchor-strategy
   dispatcher. It calls `match_string_with_start` / `match_reaches_end_with_start`
   (forward-declared by P1.M2) and, for the loop strategies, iterates `i = 0..len`.
2. **Classifier block** (just BEFORE `nfa_addstate`): the 3 class predicates +
   the REAL `is_word_boundary` REPLACE the P1.M2 stub. Order within block: the 3
   predicates first (they are leaf functions), then `is_word_boundary` (calls
   `is_word_char`). All must precede `nfa_addstate` (which calls
   `is_word_boundary`).
3. **`pattern_char_matches`** (AFTER `nfa_addstate`, BEFORE `nfa_match`): calls
   `get_escaped_char` (defined early) + the 3 predicates. Must precede `nfa_match`
   (which calls it).
4. **Two wrappers** (at END, after `nfa_match`): thin forwarders to `nfa_match`
   with `full_match` false/true. P1.M2 already forward-declared them, so no new
   declarations needed.

## Critical gotchas (P1.M3-owned)

- **Empty-original-string `\b`/`\B` semantics live in `nfa_addstate`, NOT in
  `is_word_boundary`.** The `*string_start != '\0' &&` short-circuit (P1.M2 scope)
  fires BEFORE `is_word_boundary` is called. `is_word_boundary` itself only does
  the position-based test with NULL/len guards for defensiveness. Do NOT move the
  empty-string check into `is_word_boundary` — that would double-handle it and
  risk diverging from the test suite's "legacy semantics".
- **`is_word_boundary` semantics (PRD §7.6)**: `pos==0` → true iff
  `str[0]` word char; `pos==strlen` → true iff `str[len-1]` word char;
  `pos>strlen` → false; interior → `is_word_char(str[pos-1]) !=
  is_word_char(str[pos])`; `str==NULL` → false.
- **`tolower` requires `(unsigned char)` cast** in `pattern_char_matches`
  (sign-extension UB on signed-char platforms). Cast BOTH the literal and the
  input char: `tolower((unsigned char)literal) == tolower((unsigned char)sc)`.
  Decode the escaped literal via `get_escaped_char(pc)` FIRST, then fold — never
  fold the placeholder byte.
- **dot `0x0D` excludes BOTH `\n` AND `\r`** (`sc != '\n' && sc != '\r'`).
  Glob `*` (OP_ANY) includes them. Do not conflate (§13 #8).
- **Empty-core special case in `match_with_anchors` substring branch**: an empty
  `core_pattern` matches ONLY the empty string (`strlen(core)==0 →
  strlen(str)==0`). Without this, the offset loop would match any string at
  offset 0 via the empty-NFA-accepts-immediately path.
- **Classifiers take `char`** — no `unsigned char` cast needed for the ASCII
  range tests (`c >= '0' && c <= '9'` etc.); a high-bit byte is negative as
  signed char but still falls outside all the ASCII ranges → correctly returns
  false. Only `tolower` needs the cast.
- **`is_word_char` includes `_`** (underscore) — `[A-Za-z0-9_]`. Easy to forget.
- **`is_whitespace_char`** = space, `\t \n \r \f \v` (6 chars) — matches `isspace`
  for the standard whitespace set.

## No-op confirmations (NOT P1.M3 scope — do not implement)

- The `*string_start != '\0' &&` empty-string guard in `nfa_addstate` → P1.M2.
- `nfa_gen` bumping, `lastlist` de-dup → P1.M2.
- The linear `X+` compile → P1.M2 (P1.M3 consumes `nfa_match`, never touches it).
- Sizing `#ifndef NFA_MAX_PATTERN` guard → P1.M2 G2 fix (still pending in live code).
- The G3 40KB memory-stress failure → P1.M4/P3.

## Test framework (how P1.M3 is validated)

PRD §11.4: each counting suite is a `test_case_t {pattern,input,case_sensitive,
expected,description}` table + a `run_test(t)` helper printing `PASS:`/`FAIL:` +
a summary line `Total tests run: N / Tests passed: P / Tests failed: F`.
`run_all_tests.sh` greps the summaries. P1.M3 functions are hit indirectly:

- `test_char_classification.c` (179 cases) → `\d\D\w\W\s\S` via `pattern_char_matches`.
- `test_word_boundary_basic.c` (74) + `test_word_boundary_integration.c` (189) →
  `is_word_boundary` via `nfa_addstate` OP_ASSERT.
- `test_metachar_verification.c` → smoke test of all 6 classes.
- `test_pattern_match.c` (376) → anchor strategies via `match_with_anchors`.
- `test_invalid_patterns.c` (1008) → robustness; classifiers must return false
  predictably for garbage bytes.

All suites call only the **public** `pattern_match()`; the static helpers are
never named. So the acceptance gate is purely behavioral.

## Confidence

High. The live code is the verified reference; every behavior was probed and
matches PRD §7.4/§7.6/§7.7/§15. The only assembly risk is placement/ordering and
remembering the empty-core special case + the empty-string guard living upstream
in `nfa_addstate`. The PRP encodes both as hard requirements.
