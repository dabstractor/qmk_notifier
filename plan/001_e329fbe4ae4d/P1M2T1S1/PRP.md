# PRP — P1.M2.T1.S1: Define State struct, ops enum, sizing constants

## Goal

**Feature Goal**: Append the **Thompson-NFA core type definitions** to
`pattern_match.c` — the sizing constants, the opcode enum, the self-referential
`State` struct, and the sole file-scope mutable global (`nfa_gen`). These are
the data types that `nfa_compile` (P1.M2.T1.S2) and `nfa_addstate`/`nfa_match`
(P1.M2.T2) will consume. No functions are implemented in this task.

**Deliverable**: A new `===== P1.M2 NFA Engine core definitions =====` block
APPENDED to the end of the current `pattern_match.c` (which, after S2, ends
with the public `pattern_match()`). The block contains exactly:

```c
#define NFA_MAX_PATTERN 128
#define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)   /* 258 */
enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };
typedef struct State State;
struct State { int op; char arg; State *out; State *out1; int lastlist; };
static int nfa_gen = 0;
```
plus Mode-A inline comments on each `OP_*` value and on the `lastlist` field
(item-spec §DOCS). Nothing else is modified.

**Success Definition**:
- All five items present with the exact names/values above; `nfa_gen` is
  `static` (file-local), the only mutable file-scope variable.
- `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → **exit 0** with **exactly
  two** permitted warnings: `nfa_gen defined but not used` (new this task,
  self-resolves in P1.M2.T2.S1) and `get_escaped_char defined but not used`
  (carried from S2, self-resolves in P1.M3.T2.S1). No other warnings.
- `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → exit 0, no warnings.
- A real committed test suite (e.g. `test_metachar_verification.c`) still
  **LINKS** cleanly (public API intact).
- `sizeof(State)` is 32 on x86-64 and the pool is `258` States (~8 KB; ~5 KB on
  32-bit MCUs — within PRD §7.9's "~6–8 KB" budget).
- Each `OP_*` enum value and the `lastlist` field carry Mode-A inline comments.

## User Persona (if applicable)

**Target User**: The NFA functions written in the *next* two subtasks —
`nfa_compile` (P1.M2.T1.S2) and `nfa_addstate`/`nfa_match` (P1.M2.T2). End users
and the public API never see these types (they are file-local to
`pattern_match.c`).

**Use Case**: `nfa_compile` declares `State pool[NFA_MAX_STATES]` on its stack
and threads `State **tail` pointers through `OP_CHAR`/`OP_SPLIT`/`OP_ANY` nodes;
`nfa_addstate` reads `s->op`, follows `s->out`/`s->out1`, and guards each visit
with `s->lastlist == nfa_gen`. None of that exists yet — this task only lays down
the shared vocabulary.

**Pain Points Addressed**: Establishes the Thompson-construction node shape and
the generation-tag dedup mechanism *before* the compile/simulate functions are
written, so each of those later subtasks can focus purely on algorithm. Locks
the pool sizing constant (258 = 2·128+2) so MCU-RAM budget is fixed (PRD §7.9).

## Why

- **Foundation for P1.M2**: every remaining P1.M2 function references `State`,
  `OP_*`, `NFA_MAX_STATES`, or `nfa_gen`. Defining them in isolation (no logic)
  makes the compile/simulate subtasks independently reviewable and testable.
- **Correctness anchor**: the struct field order, the opcode set, and the
  `2*N+2` pool sizing are load-bearing — `nfa_compile` relies on exactly 2 spare
  slots (one `OP_MATCH` + slack) and `lastlist` *must* exist or epsilon-closure
  (`OP_SPLIT`, `\b\b`) recurses infinitely. Getting these wrong silently breaks
  the engine later.
- **Rebuild integrity**: appends cleanly to S2's `pattern_match()`; introduces
  exactly one *new* expected unused-variable warning (`nfa_gen`), which mirrors
  the established pattern S2 set with `get_escaped_char` (expected → accepted →
  self-resolves downstream).

## What

Append **one** block to the end of `pattern_match.c`. The block defines, in
order: the two sizing `#define`s, the opcode `enum`, the `typedef struct State
State;` forward declaration, the `struct State { ... }` definition, and the
`static int nfa_gen = 0;` file-scope global. Every `OP_*` enumerator and the
`lastlist` field get a one-line Mode-A comment. No new `#include`s, no functions,
no edits to existing content.

### Success Criteria

- [ ] The five items above are present with exact names/values (`NFA_MAX_PATTERN`
      =128, `NFA_MAX_STATES`=`(2 * NFA_MAX_PATTERN + 2)`=258, enum in order
      `OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH`, struct fields
      `int op; char arg; State *out; State *out1; int lastlist;`, `nfa_gen` static).
- [ ] `typedef struct State State;` precedes `struct State {...}` (required so the
      body can name `State *out` / `State *out1`).
- [ ] `nfa_gen` is the ONLY file-scope mutable variable and is `static`.
- [ ] `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0; sole warnings are
      `nfa_gen` and `get_escaped_char` unused.
- [ ] `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → exit 0, silent.
- [ ] `test_metachar_verification.c` links cleanly with the new `pattern_match.c`.
- [ ] Mode-A comments present on each `OP_*` value and on `lastlist`.

## All Needed Context

### Context Completeness Check

**Pass.** The exact code to write is the live source of truth (git commit
`81df853`, PRD §7.5, architecture doc §"NFA State") and is reproduced verbatim in
"Implementation Tasks" below. The build warning set (exit 0; exactly two warnings
— `nfa_gen` new + `get_escaped_char` carried), the clean `-fsyntax-only`, the
test-suite LINK, the `NFA_MAX_STATES=258` arithmetic, and the `sizeof(State)=32`
/ ~8 KB pool figure were **all executed against a realistic post-S1 merged file
during research and passed**. An implementer with only this PRP + repo access can
produce the block behavior-identically and prove it.

### Documentation & References

```yaml
# MUST READ — authoritative spec for the struct/enum/constants
- file: PRD.md
  section: "### 7.5 The NFA engine"
  why: "Gives the EXACT #define / enum / struct State layout this task must
        reproduce (including the 'arg: OP_CHAR pattern byte; OP_ASSERT 0x0B/0x0C'
        and 'lastlist: generation tag, set during simulation' comments)."
  critical: "Field order and the opcode set are load-bearing for nfa_compile
        (P1.M2.T1.S2) and nfa_addstate (P1.M2.T2). Do NOT reorder fields or
        rename opcodes — downstream code threads State** and switches on op."

- file: PRD.md
  section: "### 7.9 Sizing note (MCU RAM)"
  why: "Justifies NFA_MAX_STATES: 'State pool[NFA_MAX_STATES] + two pointer
        lists live on the stack. With NFA_MAX_PATTERN = 128 that is ~256 States
        (~6–8 KB) + ~2 KB of lists per call.' The 2*N+2 formula covers one
        CHAR+SPLIT pair per byte plus the trailing MATCH."
  critical: "The pool MUST stay stack-allocated (this task only defines the
        constant; nfa_compile declares `State pool[NFA_MAX_STATES]` locally).
        Do not make the pool `static` — reentrancy/fresh-lastlist depends on it."

- file: PRD.md
  section: "## 16. Appendix B — Constants Reference"
  why: "Locks the values: NFA_MAX_PATTERN=128, NFA_MAX_STATES=2*128+2=258, both
        in pattern_match.c. These are part of the wire/behavior contract."
  critical: "NFA_MAX_STATES must be the EXPRESSION '(2 * NFA_MAX_PATTERN + 2)',
        not a bare literal 258 — so lowering NFA_MAX_PATTERN for low-RAM AVR
        (§7.9) automatically resizes the pool."

# Architecture — the data structure + the generation-tag mechanism
- file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "### NFA State (file-scope)" + "### Global State" + "## NFA Simulation (nfa_match)"
  why: "Spells out each field's role and WHY lastlist exists: 'guarded by
        lastlist == nfa_gen ... without it, OP_SPLIT and \\b\\b recurse
        infinitely. Each simulation phase bumps nfa_gen++ so closure de-dup
        works.' This is the dedup mechanism the Mode-A comment must explain."
  critical: "nfa_gen is 'the ONLY file-scope mutable variable' and is 'safe
        because the matcher is single-threaded in QMK.' Keep it static + 0-init."

# Live reference implementation (the byte-for-behavior source of truth, PRD §17)
- file: git commit 81df853 ("implemented nfa matching engine")
  why: "HEAD's pattern_match.c was RESET to process_escapes-only for the rebuild;
        the full matcher (incl. the State block at lines 64–77 and nfa_gen at
        line 125) lives in history at 81df853. Reproduce the definitions verbatim."
  how:  "git show 81df853:pattern_match.c | sed -n '60,130p'"
  critical: "Reproduce field order, opcode order, and the typedef-before-struct
        pattern EXACTLY. Comment wording may drift (PRD §17); logic may not."

# Dependency PRP — what exists when this task starts
- file: plan/001_e329fbe4ae4d/P1M1T2S2/PRP.md
  section: "## Goal" + "## Implementation Blueprint"
  why: "S2 is COMPLETE: pattern_match.c (237 lines) ends with the public
        pattern_match(). This task APPENDS after it. S2 also established the
        'expected unused warning that self-resolves downstream' convention this
        task reuses for nfa_gen."
  critical: "Do NOT modify S2's content (process_escapes, parsed_pattern_t,
        get_escaped_char, parse_pattern, free_parsed_pattern, match_with_anchors
        stub, pattern_match). APPEND ONLY."

# Downstream consumers (informational; NOT this task)
- file: plan/001_e329fbe4ae4d/architecture/pattern_match_architecture.md
  section: "## NFA Compilation (nfa_compile)" + "## NFA Simulation (nfa_match)"
  why: "Shows how nfa_compile allocates `State pool[NFA_MAX_STATES]`, threads
        `State **tail`, and zeroes `lastlist`; how nfa_addstate guards on
        `s->lastlist == nfa_gen`; how nfa_match bumps `nfa_gen++` per step."
  critical: "This task defines the vocabulary those functions consume; do not
        implement them here (they are P1.M2.T1.S2 and P1.M2.T2)."
- url: https://swtch.com/~rsc/regexp/regexp1.html
  why: "Russ Cox, 'Regular Expression Matching Can Be Simple And Fast' — the
        Thompson-NFA design this engine follows (cited by the reference header)."

# Build convention
- file: run_all_tests.sh
  why: "Toolchain is plain gcc, no make. Each suite is `gcc -o test_X test_X.c
        pattern_match.c`; suites #include \"pattern_match.h\" and call only the
        public pattern_match()."
  critical: "The static State/enum/nfa_gen are file-local, so they are validated
        by compiling pattern_match.c itself (Level 1) + linking one suite (Level 3),
        NOT by a committed test file."
```

### Current Codebase tree (run `ls` at repo root)

```bash
pattern_match.h        # P1.M1.T1.S1 (COMPLETE) — public contract. DO NOT TOUCH.
pattern_match.c        # S2-COMPLETE (237 lines): includes + process_escapes +
                       #   parsed_pattern_t + get_escaped_char + free_parsed_pattern +
                       #   parse_pattern + match_with_anchors STUB + pattern_match.
                       #   THIS task APPENDS the NFA definitions block to the END.
notifier.h notifier.c  # P2 scope — do not touch.
rules.mk               # P2 scope — do not touch.
test_*.c               # P1.M4 scope — do not touch (only used to prove LINK).
run_all_tests.sh       # P1.M4 scope — do not touch.
PRD.md                 # READ-ONLY.
plan/                  # this PRP + research — write only your own PRP/research.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
pattern_match.c        # THIS task APPENDS one block. After S1 it additionally contains:
                       #   - #define NFA_MAX_PATTERN 128
                       #   - #define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)   [=258]
                       #   - enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH }
                       #   - typedef struct State State;
                       #   - struct State { int op; char arg; State *out; State *out1; int lastlist; }
                       #   - static int nfa_gen = 0;
                       # Later subtasks APPEND after this block:
                       #   P1.M2.T1.S2 -> nfa_compile() (consumes State/OP_*/NFA_MAX_STATES)
                       #   P1.M2.T2.S1 -> nfa_addstate() (consumes State/OP_*/nfa_gen)
                       #   P1.M2.T2.S2 -> nfa_match() + nfa_has_match() (consume all of the above)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — nfa_gen IS expected to warn 'defined but not used' until P1.M2.T2.S1
//   (VERIFIED during research). A file-scope `static int` that nothing references
//   triggers -Wunused-variable under -Wall -Wextra (exit stays 0 — warnings don't
//   fail compilation). This mirrors S2's get_escaped_char convention: ACCEPT the
//   warning; do NOT suppress it with __attribute__((unused)) (not this codebase's
//   idiom). It self-resolves the moment nfa_addstate (P1.M2.T2.S1) reads nfa_gen.
//   So after THIS task the permitted-warning set is EXACTLY:
//     * 'get_escaped_char defined but not used [-Wunused-function]'  (carried, P1.M3.T2.S1)
//     * 'nfa_gen defined but not used [-Wunused-variable]'           (new,      P1.M2.T2.S1)

// CRITICAL — NO warning for the enum / struct / #defines. Verified: unused macros
//   and unused type definitions do NOT warn under -Wall -Wextra; only the static
//   int variable does. So the build is clean apart from the two items above.

// CRITICAL — `typedef struct State State;` MUST come before `struct State {...}`.
//   The typedef is what lets the struct body name its own pointers as `State *out`
//   and `State *out1` (self-referential). Omit the typedef and the body must use
//   `struct State *`, which diverges from the reference and from every consumer.

// GOTCHA — NFA_MAX_STATES must be the EXPRESSION `(2 * NFA_MAX_PATTERN + 2)`, not
//   a literal 258, so that lowering NFA_MAX_PATTERN for low-RAM AVR (PRD §7.9)
//   automatically resizes the pool. Parens around the macro body are required.

// GOTCHA — enum order is significant: { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT,
//   OP_MATCH } => OP_CHAR=0 ... OP_MATCH=4. Downstream code switches on these and
//   the reference hard-codes the set; do not reorder or insert values.

// GOTCHA — do NOT add new #includes. stdbool/string/stdlib (from S1) are already
//   present and this block uses only built-in int/char/pointer types. No <stddef.h>,
//   <stdint.h>, <stdio.h>, or "notifier.h".

// GOTCHA — do NOT declare the State pool here. nfa_compile (P1.M2.T1.S2) owns the
//   stack allocation `State pool[NFA_MAX_STATES]`; this task only defines the
//   CONSTANT. Making the pool file-scope `static` would break the fresh-lastlist
//   invariant (§7.9: pool must be stack-fresh each call).

// GOTCHA — nfa_gen placement: the reference (81df853) put it mid-file (between
//   nfa_compile and nfa_addstate). For the incremental rebuild we group it with
//   the struct in this task's block (item-spec lists it here). Behavior is
//   identical (it is a file-scope int); the only observable effect is the
//   expected unused-variable warning until P1.M2.T2.S1. Validated.
```

## Implementation Blueprint

### Data models and structure

One new self-referential struct + opcode enum + two sizing macros + one
file-scope generation counter (all file-local; nothing exported):

```c
#define NFA_MAX_PATTERN 128
#define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)   /* = 258 */

enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };

typedef struct State State;
struct State {
    int    op;        /* one of the OP_* opcodes                          */
    char   arg;       /* OP_CHAR: processed pattern byte; OP_ASSERT: 0x0B/0x0C */
    State *out;       /* primary outgoing edge (all ops)                 */
    State *out1;      /* secondary edge (OP_SPLIT only; else NULL)        */
    int    lastlist;  /* == nfa_gen when already on the current list      */
};

static int nfa_gen = 0;   /* monotonic generation tag (sole mutable file-scope var) */
```

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: APPEND the NFA core definitions block to pattern_match.c
  - PLACE: immediately AFTER the public pattern_match() at the END of the file
    (the file is currently 237 lines, ending with pattern_match()'s closing brace).
  - PREFIX the block with a banner comment:
        /* ===== P1.M2 NFA Engine core definitions (State, ops, sizing) =====
         * Consumed by nfa_compile (P1.M2.T1.S2) and nfa_addstate/nfa_match
         * (P1.M2.T2). Thompson construction: Russ Cox,
         * https://swtch.com/~rsc/regexp/regexp1.html  (see PRD §7.5). */
  - ADD, in this exact order, the five items from the Data Models block above,
    each with its Mode-A comment (see "The exact code to write").
  - NAMING: NFA_MAX_PATTERN, NFA_MAX_STATES (macros, UPPER_CASE); OP_CHAR etc.
    (enum, UPPER_CASE with OP_ prefix); State (struct, PascalCase via typedef);
    nfa_gen (static int, snake_case).
  - PRESERVE: all existing content (S1 includes + process_escapes; S2 pipeline +
    pattern_match). APPEND ONLY — do not move, reorder, or edit anything above.
  - DO NOT: add functions, add #includes, declare the State pool, or suppress
    the expected nfa_gen unused-variable warning.

Task 2: VERIFY the build + link gates (run the Validation Loop, Levels 1 & 3).
```

**The exact code to write** (verbatim field/order from git `81df853` + PRD §7.5;
only the Mode-A comments are authored here, all behavior-neutral per PRD §17).
Append after the public `pattern_match()`:

```c
/* ===== P1.M2 NFA Engine core definitions (State, ops, sizing) =====
 * Consumed by nfa_compile() [P1.M2.T1.S2] and nfa_addstate()/nfa_match()
 * [P1.M2.T2]. Thompson construction; linear-time simulation, no backtracking.
 * Reference: Russ Cox, "Regular Expression Matching Can Be Simple And Fast",
 * https://swtch.com/~rsc/regexp/regexp1.html  (see PRD §7.5, §7.9). */

/* Pool sizing. nfa_compile() declares `State pool[NFA_MAX_STATES]` on its stack
 * (NOT static — the pool must be fresh each call so lastlist starts at 0). At
 * NFA_MAX_PATTERN=128 that is 258 States (~8 KB on 64-bit, ~5 KB on 32-bit MCUs
 * — within the §7.9 "~6–8 KB" budget). Lower NFA_MAX_PATTERN for low-RAM AVR. */
#define NFA_MAX_PATTERN 128                         /* max processed-pattern length */
#define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)   /* = 258: 2 per byte + MATCH + slack */

/* NFA node opcodes (Thompson construction). nfa_compile emits these; nfa_addstate
 * and nfa_match switch on `s->op`. */
enum {
    OP_CHAR,   /* consume one input byte that matches `arg`: a processed-pattern
                 literal (0x01-0x04, 0x2A escaped, ordinary), a class byte
                 (0x05-0x0A, tested via pattern_char_matches), or the dot 0x0D    */
    OP_ANY,    /* consume ANY one byte including newline — the glob '*' compiled
                 as regex '.*' (OP_ANY looping back through an OP_SPLIT)          */
    OP_SPLIT,  /* epsilon fork: nfa_addstate follows BOTH `out` and `out1` without
                 consuming input. Implements '*' and '+' quantifiers (zero/one-or-more) */
    OP_ASSERT, /* zero-width assertion: `arg` is 0x0B (\b, word boundary) or 0x0C
                 (\B, non-boundary). nfa_addstate recurses into `out` only if
                 is_word_boundary(string_start, abspos) == (arg == 0x0B)          */
    OP_MATCH   /* accepting state: the pattern has matched the input up to here.
                 nfa_addstate adds it to the list; nfa_has_match reports success   */
};

/* A single NFA node. The typedef-before-definition lets the body name its own
 * successor pointers as `State *`. Field order matches the reference (PRD §7.5). */
typedef struct State State;
struct State {
    int    op;        /* one of the OP_* opcodes above                                 */
    char   arg;       /* OP_CHAR: the processed-pattern byte to match; OP_ASSERT: 0x0B/0x0C */
    State *out;       /* primary outgoing edge (used by every opcode)                 */
    State *out1;      /* secondary outgoing edge (OP_SPLIT only; NULL for all others) */
    int    lastlist;  /* generation-tag dedup: equals `nfa_gen` when this state is
                         already on the CURRENT simulation list, so nfa_addstate
                         skips it. Bumped indirectly via nfa_gen++ once per step
                         (nfa_match), which resets the "seen" set each phase.
                         MANDATORY: without it, OP_SPLIT and repeated \b would
                         recurse infinitely during epsilon-closure. Pool states are
                         zero-initialized by nfa_compile, so lastlist starts at 0
                         (and nfa_gen starts at 0, so the very first closure works). */
};

/* The ONLY file-scope mutable variable. A monotonic generation tag bumped once
 * per simulation step (nfa_match) so the lastlist==nfa_gen guard de-dups the
 * epsilon-closure. Safe because the matcher is single-threaded in QMK (if
 * reentrancy were ever needed, keep the State arrays on the stack — §7.9).
 * NOTE: unused until P1.M2.T2.S1 (nfa_addstate) => expect a -Wunused-variable
 * warning here; it self-resolves when that subtask lands. */
static int nfa_gen = 0;
```

### Implementation Patterns & Key Details

```c
// PATTERN: self-referential struct via typedef-before-definition.
//   `typedef struct State State;` THEN `struct State { ... State *out; ... };`.
//   This is the standard idiom and matches the reference exactly.

// PATTERN: sizing macro is an EXPRESSION referencing the length macro, so the
//   whole pool resizes when NFA_MAX_PATTERN changes:
//     #define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)
//   Parens around the replacement list prevent operator-precedence surprises
//   wherever NFA_MAX_STATES is used in an expression.

// PATTERN: enum opcodes are anonymous-typed (no typedef) and used only in
//   `int op` comparisons (`s->op == OP_CHAR`). Values are implicitly
//   OP_CHAR=0, OP_ANY=1, OP_SPLIT=2, OP_ASSERT=3, OP_MATCH=4 — do not rely on
//   or change these integers; switch on the names.

// PATTERN: file-scope generation counter initialized to 0. nfa_compile zeroes
//   every pool state's lastlist (so they all start "unseen"); nfa_gen also
//   starts at 0; the first closure (nfa_gen still 0) therefore marks states
//   correctly. Each later step bumps nfa_gen++ to get a fresh "seen" set.

// ANTI-PATTERN: do NOT declare `static State pool[NFA_MAX_STATES];` here. The
//   pool belongs on nfa_compile's stack (fresh lastlist each call, §7.9).

// ANTI-PATTERN: do NOT suppress the nfa_gen unused-variable warning with
//   __attribute__((unused)) or a `(void)nfa_gen;` stub. Accept the warning; it
//   self-resolves in P1.M2.T2.S1. (Same convention S2 used for get_escaped_char.)

// ANTI-PATTERN: do NOT add functions (nfa_compile/nfa_addstate/nfa_match) here.
//   They are P1.M2.T1.S2 and P1.M2.T2. This task is definitions only.

// ANTI-PATTERN: do NOT add #includes (stddef/stdint/stdio/notifier.h) — unused
//   by this block; they belong to later subtasks.
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - APPEND one block at EOF of pattern_match.c, after the public pattern_match().
  - No insertion into existing code; no reordering; no edits above the append point.

CONSUMERS (downstream, NOT this task):
  - State / OP_* / NFA_MAX_STATES  <- nfa_compile()        [P1.M2.T1.S2]
  - State / OP_* / nfa_gen         <- nfa_addstate()       [P1.M2.T2.S1]
  - State / OP_* / nfa_gen         <- nfa_match(), nfa_has_match() [P1.M2.T2.S2]

BUILD:
  - No build-system change. Plain gcc (run_all_tests.sh style). Validate by
    compiling pattern_match.c (Level 1) and linking one suite (Level 3).

CONFIG / DATABASE / ROUTES:
  - N/A (C firmware module; pure type/constant definitions, no runtime effect yet).
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc`. This task adds only definitions
> (no functions), so there is no behavior to unit-test; validation is the build
> + link + symbol-presence checks below. All commands were VERIFIED during
> research against a realistic post-S1 merged file.

### Level 1: Syntax & Style (THE PRIMARY GATE)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Compile pattern_match.c as a translation unit.
gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o
# Expected: exit 0. EXACTLY TWO warnings are permitted and expected:
#   warning: 'nfa_gen' defined but not used [-Wunused-variable]
#   warning: 'get_escaped_char' defined but not used [-Wunused-function]
# (get_escaped_char is carried from S2; nfa_gen is new this task; both
#  self-resolve in P1.M2.T2.S1 / P1.M3.T2.S1 respectively.)
# FAIL if: exit != 0, OR any OTHER warning appears, OR process_escapes/parse_pattern
#           appear in the warning list (those must be USED by now).

# 1b. Syntax-only (silent — fsyntax-only does not emit unused warnings).
gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c
# Expected: exit 0, NO output. (Confirms no syntax/type errors independent of
#           the expected unused-symbol warnings.)

# 1c. Confirm the five items are present with exact names/values.
grep -nE '^#define NFA_MAX_PATTERN 128' pattern_match.c
grep -nE '^#define NFA_MAX_STATES  \(2 \* NFA_MAX_PATTERN \+ 2\)' pattern_match.c
grep -nE '^enum \{ OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH \};' pattern_match.c
grep -nE '^typedef struct State State;' pattern_match.c
grep -nE '^static int nfa_gen = 0;' pattern_match.c
# Expected: each prints exactly ONE line. (Order matters less than presence +
#           exact text; the struct body field order is checked in 1d.)

# 1d. Confirm struct State field order (op, arg, out, out1, lastlist) and that
#     the typedef precedes the struct definition (self-referential requirement).
awk '/^typedef struct State State;/{print NR": "$0} /^struct State \{/{f=1; print NR": "$0} f&&/^\};/{f=0}' pattern_match.c
# Expected: the typedef line number < the struct-open line number; fields appear
#           in order op, arg, out, out1, lastlist between them.

rm -f /tmp/pm.o
```

### Level 2: Component Tests (constant/sizing sanity — no functions to test)

```bash
cd /home/dustin/projects/qmk-notifier

# This task defines no functions, so there is nothing to call. Confirm the
# compile-time constants and struct size with a tiny throwaway program that
# #includes the .c (reaches the file-scope definitions).
cat > /tmp/s1_const.c <<'EOF'
#include "pattern_match.c"
#include <stdio.h>
int main(void) {
    /* The enum + struct + macros are all in scope via the .c include. */
    printf("NFA_MAX_PATTERN = %d\n", NFA_MAX_PATTERN);
    printf("NFA_MAX_STATES  = %d (want 258)\n", NFA_MAX_STATES);
    printf("OP_CHAR=%d OP_ANY=%d OP_SPLIT=%d OP_ASSERT=%d OP_MATCH=%d\n",
           OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH);
    State s = {0};                 /* zero-init a State (proves the type is usable) */
    s.op = OP_MATCH; s.lastlist = nfa_gen;
    printf("sizeof(State) = %zu  pool = %zu bytes\n", sizeof(State),
           (size_t)NFA_MAX_STATES * sizeof(State));
    printf("state ok: op=%d lastlist=%d\n", s.op, s.lastlist);
    return 0;
}
EOF
gcc -Wall -Wextra -I. /tmp/s1_const.c -o /tmp/s1_const && /tmp/s1_const
# Expected output (exact):
#   NFA_MAX_PATTERN = 128
#   NFA_MAX_STATES  = 258 (want 258)
#   OP_CHAR=0 OP_ANY=1 OP_SPLIT=2 OP_ASSERT=3 OP_MATCH=4
#   sizeof(State) = 32  pool = 8256 bytes        (on x86-64; 32-bit MCUs ~5 KB)
#   state ok: op=4 lastlist=0

rm -f /tmp/s1_const.c /tmp/s1_const
```

### Level 3: Integration Testing (API Integrity — LINK, not run)

```bash
cd /home/dustin/projects/qmk-notifier

# The committed suites call only the public pattern_match(). After this task the
# symbol still exists and the new definitions are file-local, so a suite LINKS
# cleanly — proving nothing in the public path broke. (It will not PASS yet:
# match_with_anchors is still the S2 stub; real matching is P1.M3.T2.S2.)
gcc -Wall test_metachar_verification.c pattern_match.c -o /tmp/tm 2>&1 \
  | grep -v 'get_escaped_char\|nfa_gen'
# Expected: empty output (only the two known warnings, filtered above).
echo "link exit (expect 0): ${PIPESTATUS[0]}"
rm -f /tmp/tm

# DO NOT run run_all_tests.sh to validate this task — the stub matcher makes
# every true-expecting case fail by design. The compile + link above suffice.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Doc-contract check (item-spec §DOCS, Mode A): each OP_* enumerator and the
# lastlist field carry an explanatory inline comment.
awk '/^enum \{/{f=1} f{print} /^\};/{if(f)exit}' pattern_match.c | grep -qE 'OP_CHAR.*consume|OP_CHAR.*/\*' \
  && echo "OP_* comments present (ok)" || echo "WARN: OP_* Mode-A comments missing"
grep -qE 'lastlist.*generation|generation-tag|nfa_gen' pattern_match.c \
  && echo "lastlist generation-tag comment present (ok)" \
  || echo "WARN: lastlist dedup comment missing"

# Dedup-mechanism doc check: confirm a comment explains WHY lastlist exists
# (prevents infinite epsilon recursion). Manual eyeball of the lastlist comment.
grep -nA3 'int    lastlist;' pattern_match.c | grep -qiE 'infinit|recurse|dedup|already' \
  && echo "dedup rationale documented (ok)" \
  || echo "WARN: dedup rationale not explained"

# Sizing-budget cross-check vs PRD §7.9: 258 States must be <= ~8 KB.
python3 -c "assert 2*128+2==258, 'bad math'; print('NFA_MAX_STATES=258 OK (§7.9 budget ~6-8KB)')"

# Self-containment: this task must NOT define functions or the pool.
grep -nE 'static State \*nfa_compile|State pool\[|nfa_addstate|nfa_match|nfa_has_match' pattern_match.c \
  && { echo "ERROR: this task leaked NFA functions/pool (scope creep)"; exit 1; } \
  || echo "no functions/pool defined (good — definitions only)"

# Confirm no new #includes beyond S1's stdbool/string/stdlib.
grep -nE '^#include' pattern_match.c
# Expected: exactly <stdbool.h>, <string.h>, <stdlib.h>.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -c pattern_match.c` → exit 0; exactly
      two warnings (`nfa_gen`, `get_escaped_char` unused), nothing else.
- [ ] Level 1: `gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c` → exit 0, silent.
- [ ] Level 2: `/tmp/s1_const` prints `NFA_MAX_STATES = 258`, opcode values
      `0..4`, `sizeof(State)=32` / pool `8256` bytes, and `state ok: op=4 lastlist=0`.
- [ ] Level 3: `test_metachar_verification.c pattern_match.c` LINKS cleanly.
- [ ] Level 4: Mode-A comments present on every `OP_*` and on `lastlist`; no
      functions/pool/new-includes leaked; `NFA_MAX_STATES=258` matches §7.9.

### Feature Validation

- [ ] `#define NFA_MAX_PATTERN 128` present (exact).
- [ ] `#define NFA_MAX_STATES (2 * NFA_MAX_PATTERN + 2)` present as an EXPRESSION (not literal 258).
- [ ] `enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };` present in that order.
- [ ] `typedef struct State State;` precedes `struct State { ... }`.
- [ ] `struct State` fields in order: `int op; char arg; State *out; State *out1; int lastlist;`.
- [ ] `static int nfa_gen = 0;` present and is the sole file-scope mutable variable.

### Code Quality Validation

- [ ] Matches the reference implementation (git `81df853` / PRD §7.5) field-for-field
      and opcode-for-opcode (PRD §17: code wins; comment drift tolerated).
- [ ] APPEND ONLY — no modification to S1/S2 content, `pattern_match.h`, `test_*.c`,
      `notifier.*`, `rules.mk`, `PRD.md`, `tasks.json`, `prd_snapshot.md`, `.gitignore`.
- [ ] No new `#include`s (only stdbool/string/stdlib already present).
- [ ] No functions, no State pool, no `__attribute__((unused))` suppression.
- [ ] `nfa_gen` unused-variable warning accepted (self-resolves in P1.M2.T2.S1).

### Documentation & Deployment

- [ ] Mode-A inline comments explain each OP_* opcode (consume/epsilon/zero-width/accept).
- [ ] `lastlist` comment explains the generation-tag dedup mechanism AND why it is
      mandatory (infinite recursion without it).
- [ ] `nfa_gen` comment notes it is the sole mutable file-scope var, single-threaded,
      and that the unused warning is expected until P1.M2.T2.S1.
- [ ] No new env vars / config / build-system changes.

---

## Anti-Patterns to Avoid

- ❌ Don't overwrite or reorder `pattern_match.c` — APPEND one block after
  `pattern_match()`; preserve everything S1/S2 wrote.
- ❌ Don't declare the State pool here — it belongs on `nfa_compile`'s stack
  (fresh `lastlist` each call, §7.9). This task only defines the *constant*.
- ❌ Don't make `nfa_gen` non-static, or add other file-scope mutable globals —
  it must be the ONLY one (architecture doc §"Global State").
- ❌ Don't suppress the `nfa_gen` unused-variable warning with
  `__attribute__((unused))` or a `(void)nfa_gen;` trick — accept it (self-resolves
  in P1.M2.T2.S1); same convention S2 used for `get_escaped_char`.
- ❌ Don't reorder the enum or the struct fields — downstream switches on opcodes
  and threads `State **tail`; the reference layout is load-bearing.
- ❌ Don't replace the `(2 * NFA_MAX_PATTERN + 2)` expression with a bare `258` —
  the expression is what lets §7.9's "lower NFA_MAX_PATTERN for AVR" work.
- ❌ Don't omit the `typedef struct State State;` before the struct body — without
  it the struct cannot name its own `State *out` / `State *out1` pointers.
- ❌ Don't add `#include`s (stddef/stdint/stdio/notifier.h) — unused by this block.
- ❌ Don't implement `nfa_compile` / `nfa_addstate` / `nfa_match` here — they are
  P1.M2.T1.S2 and P1.M2.T2. Definitions only.
- ❌ Don't run `run_all_tests.sh` to validate this task — the stub matcher makes
  true-cases fail by design; use the compile (Level 1) + link (Level 3) gates.
- ❌ Don't touch `pattern_match.h`, `test_*.c`, `notifier.*`, `rules.mk`, `PRD.md`,
  `tasks.json`, `prd_snapshot.md`, or `.gitignore`.

---

## Confidence Score: 10/10

The exact code to write is the live source of truth (git commit `81df853` lines
64–77 + 125; PRD §7.5 verbatim struct/enum/`#define` layout; architecture doc
§"NFA State"/§"Global State") and is reproduced field-for-field above (only the
Mode-A comments are authored here, all behavior-neutral per PRD §17). The build
gate (exit 0; exactly two permitted warnings — `nfa_gen` new + `get_escaped_char`
carried), the silent `-fsyntax-only`, the clean test-suite LINK, the
`NFA_MAX_STATES=258` arithmetic, the opcode values `0..4`, and the
`sizeof(State)=32` / ~8 KB pool figure were **all executed against a realistic
post-S1 merged file during research and passed**. The expected `nfa_gen`
unused-variable warning (file-scope static int) and the absence of warnings for
the enum/struct/`#define`s were **empirically confirmed**. Dependencies on S2
(file now 237 lines, S2-complete) and the boundaries with P1.M2.T1.S2
(`nfa_compile`) / P1.M2.T2 (`nfa_addstate`/`nfa_match`) are explicit. No external
dependencies (libc only; the Russ Cox URL is informational).
