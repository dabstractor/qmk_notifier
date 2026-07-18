# Research Notes — P1.M2.T1.S2: nfa_compile() (Thompson construction)

## Authoritative source of truth

`git show 81df853:pattern_match.c` lines 79–121 (PRD §17: code+tests win). The
reference body is reproduced verbatim below:

```c
/* ---- compile: processed pattern -> State pool, returns start state ---- */
static State *nfa_compile(const char *pat, State *pool, int *nstates_out) {
    int n = 0;
    State *start = NULL;
    State **tail = &start;            /* slot to write the next unit's start into */

    #define NEW() (&pool[n < NFA_MAX_STATES ? n++ : n])   /* allocate one state */

    for (const char *p = pat; *p; p++) {
        unsigned char b = (unsigned char)*p;
        if (b == 0x2A) {                         /* glob '*'  ==  regex .* */
            State *any = NEW(); any->op = OP_ANY;
            State *sp  = NEW(); sp->op  = OP_SPLIT; sp->out = any; sp->out1 = NULL;
            any->out = sp;                        /* loop back */
            *tail = sp; tail = &sp->out1;
        } else if (b == 0x0B || b == 0x0C) {      /* \b / \B : zero-width assert */
            State *a = NEW(); a->op = OP_ASSERT; a->arg = (char)b; a->out = NULL;
            *tail = a; tail = &a->out;
        } else if (b == 0x0E) {
            /* standalone quantifier marker should not occur (process_escapes
             * only emits it right after a consuming element). Skip defensively. */
            continue;
        } else {                                  /* consuming element (literal/class/dot) */
            State *c = NEW(); c->op = OP_CHAR; c->arg = (char)b; c->out = NULL;
            if ((unsigned char)p[1] == 0x0E) {    /* X+  ->  c then SPLIT(loop/exit) */
                State *sp = NEW(); sp->op = OP_SPLIT; sp->out = c; sp->out1 = NULL;
                c->out = sp;                      /* after one X, reach the split */
                *tail = c; tail = &sp->out1;
                p++;                              /* consume the 0x0E marker */
            } else {
                *tail = c; tail = &c->out;
            }
        }
    }

    State *m = NEW(); m->op = OP_MATCH;           /* accepting state */
    *tail = m;
    /* zero lastlist on every allocated state (fresh pool each call) */
    for (int i = 0; i < n; i++) pool[i].lastlist = 0;
    *nstates_out = n;

    #undef NEW
    return start;
}
```

## Compile-construct semantics (verified empirically against merged file)

| Processed input | NFA shape | States (n) |
|---|---|---|
| `""` (empty) | `MATCH` | 1 |
| `abc` (literals) | `CHAR(a)->CHAR(b)->CHAR(c)->MATCH` | 4 (N+1) |
| `\x01\x02` (escaped literals) | `CHAR(01)->CHAR(02)->MATCH` | 3 |
| `\x0D` (dot) | `CHAR(0D)->MATCH` | 2 |
| `\x05` (\d class) | `CHAR(05)->MATCH` | 2 |
| `\x0B` (\b) | `ASSERT(0B)->MATCH` | 2 (zero-width) |
| `\x0C` (\B) | `ASSERT(0C)->MATCH` | 2 |
| `\x0B`+`w` | `ASSERT(0B)->CHAR(77)->MATCH` | 3 |
| `\x2A` (glob `*`) | `SPLIT -> ANY(loop back to SPLIT) -> ... -> MATCH` | 3 |
| `a\x0E` (a+) | `CHAR(a) -> SPLIT(loop back to CHAR) -> ... -> MATCH` | 3 |
| `a\x0Eb\x0Ec` (a+b+c) | 2+2+1 CHAR + 2 SPLIT + MATCH | 6 |
| `a+` x50 (a+a+a+...) | 50×(CHAR+SPLIT) + MATCH | **101** (LINEAR) |

### Key invariant (PRD §7.8): `a+a+a+...` compiles LINEARLY
- Each `X+` consumes exactly 2 states (1 OP_CHAR + 1 OP_SPLIT).
- 50 repetitions → 101 states. NO blow-up. This is what killed the old
  backtracking engine (exponential). Thompson construction is the fix.

## Build behavior (verified against realistic merged file)

Merged file = current `pattern_match.c` (S1 process_escapes + S2 pipeline +
M2.T1.S1 State/enum/constants/nfa_gen) + nfa_compile appended.

```
$ gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o   # exit 0
pattern_match.c:302:15: warning: 'nfa_compile' defined but not used [-Wunused-function]
pattern_match.c:293:12: warning: 'nfa_gen' defined but not used [-Wunused-variable]
pattern_match.c:104:13: warning: 'get_escaped_char' defined but not used [-Wunused-function]

$ gcc -Wall -Wextra -std=c99 -fsyntax-only pattern_match.c      # exit 0, SILENT
```

### Expected warning set after THIS task (3 total, ALL self-resolve downstream)
1. `get_escaped_char defined but not used` — carried from S2; self-resolves **P1.M3.T2.S1** (pattern_char_matches)
2. `nfa_gen defined but not used` — carried from M2.T1.S1; self-resolves **P1.M2.T2.S1** (nfa_addstate)
3. `nfa_compile defined but not used` — **NEW this task**; self-resolves **P1.M2.T2.S2** (nfa_match calls it)

Exit stays 0 (warnings don't fail the build). `-fsyntax-only` is silent (it does
not emit unused warnings). Do NOT suppress with `__attribute__((unused))` — not
this codebase's idiom (same convention S2 used for get_escaped_char).

## Consumer contract (what nfa_match will do — P1.M2.T2.S2, NOT this task)

From `git show 81df853:pattern_match.c` lines 157–193:
```c
static bool nfa_match(const char *pattern, const char *str,
                      const char *string_start, bool case_sensitive, bool full_match) {
    State pool[NFA_MAX_STATES];
    int nstates;
    /* ... */
    State *start = nfa_compile(pattern, pool, &nstates);
    /* seed clist with epsilon-closure of start; simulate char-by-char */
}
```
So nfa_compile's contract:
- INPUT: `pat` = NUL-terminated processed-pattern bytes (from process_escapes);
  `pool` = caller-allocated `State[NFA_MAX_STATES]` on the stack; `nstates_out` = int*.
- OUTPUT: returns start State* (into pool); writes used-state count to *nstates_out;
  guarantees `lastlist == 0` on every used state (so first nfa_addstate closure works,
  since nfa_gen also starts at 0).
- NULL `pat` is NOT guarded by nfa_compile (the `for (*p; ...)` would deref NULL).
  The PUBLIC pattern_match() NULL-guards upstream, and nfa_match (P1.M2.T2.S2) is
  the only caller — it receives parse_pattern's non-NULL core_pattern. Do NOT add a
  NULL guard here (deviates from reference; the caller owns that invariant).

## The NEW() macro — bounds-safe allocator

```c
#define NEW() (&pool[n < NFA_MAX_STATES ? n++ : n])
```
- Returns `&pool[n]` then increments n — BUT only if `n < NFA_MAX_STATES`.
- At/over the limit, returns `&pool[n]` WITHOUT incrementing (clamps), so the last
  slot is reused and the compile cannot overflow the pool. This is the
  bounds-safety mechanism; it means a too-long pattern silently overwrites the last
  state rather than crashing. NFA_MAX_STATES = 2*NFA_MAX_PATTERN+2 = 258 guarantees
  any valid (<=128-byte) processed pattern fits (max 2 states/byte + MATCH).
- MUST `#undef NEW` before return (file-scope macro hygiene; matches reference).

## The tail-pointer threading pattern

`State **tail = &start;` — points at the slot where the NEXT unit's start goes.
- Each construct writes `*tail = <its start state>` then advances `tail` to point
  at that construct's "dangling" outgoing slot (`&sp->out1` for SPLIT, `&c->out`
  for CHAR/ASSERT).
- At the end, `*tail = m` (the OP_MATCH) closes the final dangling slot.
- This is what makes the construction LINEAR and chain-able regardless of order.

## Placement / append boundary

APPEND immediately AFTER the M2.T1.S1 block (after `static int nfa_gen = 0;`),
which is the current EOF of pattern_match.c. No edits above. No new #includes
(uses only State/OP_*/NFA_MAX_STATES from M2.T1.S1; the for-loop uses `unsigned
char`/`const char *` — built-in types, no headers).

## Mode-A documentation requirement (item-spec §5 DOCS)

Add inline comments on each branch explaining the Thompson construction pattern:
- (a) 0x2A glob → OP_ANY + OP_SPLIT loop-back (.* semantics, matches empty AND newline)
- (b) 0x0B/0x0C → OP_ASSERT zero-width, arg carries the byte
- (c) 0x0E standalone → skip defensively (should not occur)
- (d) consuming X + 0x0E → OP_CHAR(X) then OP_SPLIT loop-back (linear X+)
- (e) plain consuming X → OP_CHAR(X)
- end → OP_MATCH
Reference PRD §7.8 for WHY NFA (not backtracking) — the exponential blow-up fix.

## Anti-patterns confirmed NOT to do
- Do NOT NULL-guard `pat` (caller owns it; reference doesn't).
- Do NOT suppress the nfa_compile unused warning.
- Do NOT add #includes.
- Do NOT implement nfa_addstate/nfa_match/nfa_has_match (P1.M2.T2).
- Do NOT declare the State pool (caller nfa_match owns it).
- Do NOT touch nfa_gen (M2.T1.S1 owns it; this task only zeroes pool[].lastlist).
