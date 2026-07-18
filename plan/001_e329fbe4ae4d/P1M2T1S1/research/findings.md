# Research Notes — P1.M2.T1.S1 (State struct, ops enum, sizing constants)

## Task scope
Append the NFA **core type definitions** to `pattern_match.c`:
1. `#define NFA_MAX_PATTERN 128`
2. `#define NFA_MAX_STATES (2 * NFA_MAX_PATTERN + 2)` → **258**
3. `enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };`
4. `typedef struct State State;` + `struct State { int op; char arg; State *out; State *out1; int lastlist; };`
5. `static int nfa_gen = 0;` (the ONLY file-scope mutable global)
6. Mode-A inline comments on each OP_* enum value and on `lastlist`.

Consumed by: `nfa_compile` (P1.M2.T1.S2) and `nfa_addstate`/`nfa_match` (P1.M2.T2).

## Source of truth
- **Live reference = git commit `81df853` ("implemented nfa matching engine")**,
  `pattern_match.c` lines 64–77 (struct/constants/enum) and line 125 (nfa_gen).
  - NOTE: `git HEAD` (commit `84e3013`) holds ONLY `process_escapes()` — the file
    was RESET to start the incremental rebuild. The full matcher lives in
    history at `81df853`. The P1.M1.T2.S2 PRP's claim of "git HEAD = 514 lines"
    is stale; the reference NFA definitions are confirmed at `81df853`.
- PRD §7.5 "The NFA engine" — exact struct/enum/`#define` layout (authoritative).
- PRD §7.9 "Sizing note (MCU RAM)" — pool lives on the stack, ~6–8 KB.
- PRD Appendix B (§16) — constant table: `NFA_MAX_PATTERN=128`,
  `NFA_MAX_STATES=2*128+2=258`, both in `pattern_match.c`.
- Architecture doc `pattern_match_architecture.md` §"NFA State" + §"Global State".

## Exact reference block (commit 81df853, lines 64–77)
```c
#define NFA_MAX_PATTERN 128                 /* max processed-pattern length      */
#define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)

enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };

typedef struct State State;
struct State {
    int    op;
    char   arg;       /* OP_CHAR: pattern byte; OP_ASSERT: 0x0B or 0x0C */
    State *out;
    State *out1;
    int    lastlist;  /* generation tag, set during simulation           */
};
```
And (line 125, just before nfa_addstate in the reference):
```c
static int nfa_gen = 0;                           /* monotonic generation tag */
```

## Empirical validation performed (all PASSED)
1. **Build gate** — appended the 5-item block to the CURRENT `pattern_match.c`
   (237 lines, S2-complete) and ran `gcc -Wall -Wextra -std=c99 -c`:
   - **exit 0**
   - **exactly 2 warnings**: `nfa_gen defined but not used [-Wunused-variable]`
     (NEW this task; self-resolves in P1.M2.T2.S1 when nfa_addstate touches it)
     + `get_escaped_char defined but not used [-Wunused-function]` (carried from
     S2; self-resolves in P1.M3.T2.S1). NO warnings for the enum / struct /
   `#define`s — only the file-scope static *int* warns.
2. **`-fsyntax-only`** → exit 0, **zero warnings** (fsyntax-only does not emit
   unused-var/unused-function warnings — same behavior as S2).
3. **Test-suite LINK** — `gcc -Wall test_metachar_verification.c pattern_match.c`
   links cleanly (public API intact; only the 2 known warnings filtered).
4. **Sizing math** — `2*128+2 = 258` ✓. `sizeof(State)=32` on x86-64 → pool
   `258×32 = 8256` B (~8 KB); ~5 KB on 32-bit MCUs (matches §7.9 "~6–8 KB").

## Key decisions for the PRP
- **Placement**: append ONE dedicated `===== P1.M2 NFA Engine core definitions =====`
  block at the END of the current file (after S2's `pattern_match()`). Rationale:
  both downstream consumers (`nfa_compile` next; `nfa_addstate`/`nfa_match` in
  P1.M2.T2) are appended AFTER this block, so all 5 items are in scope. Grouping
  `nfa_gen` with the struct (rather than the reference's mid-file position) is
  behavior-identical (file-scope int) and matches the item-spec grouping. Validated.
- **`typedef struct State State;` before `struct State {...}`**: REQUIRED — the
  typedef lets the struct body self-reference as `State *out` / `State *out1`.
- **nfa_gen unused warning is EXPECTED** (mirrors S2's get_escaped_char pattern):
  accept it; do NOT use `__attribute__((unused))` (not this codebase's idiom); it
  self-resolves in P1.M2.T2.S1.
- **Mode-A comments**: each OP_* value gets a one-line comment; `lastlist` gets a
  comment explaining the generation-tag dedup (bumped per sim step; equal to
  nfa_gen ⇒ already on current list ⇒ skip, which is what stops OP_SPLIT/\b\b
  from recursing forever). Comment drift is tolerated (PRD §17); logic must be
  byte-for-behavior identical to the reference.
