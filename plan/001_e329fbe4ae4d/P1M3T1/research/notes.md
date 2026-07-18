# Research Notes — P1.M3.T1 (Character Classification Helpers)

Scope: the **four** file-local classifier functions only:
`is_digit_char`, `is_word_char`, `is_whitespace_char`, `is_word_boundary`.
(Excludes `pattern_char_matches` = T2.S1, `match_with_anchors` + wrappers = T2.S2.)

## State of pattern_match.c when T1 starts (P1.M2 complete)

Verified against the parent `P1M2/PRP.md` stub idiom (lines 406–412, 493–495,
516–517, 592–593) and the live reference (pattern_match.c:448–477):

- `is_word_boundary` exists as a **STUB** returning `false`, placed immediately
  before `nfa_addstate` so the translation unit links. Its body is never
  observed by any test while M2 stands.
- `nfa_addstate` (M2.T2.S1) ALREADY calls `is_word_boundary(string_start, abspos)`
  inside its `OP_ASSERT` branch, guarded by `*string_start != '\0' &&` (the
  empty-original-string short-circuit that makes `\b`/`\B` "neither match" on "").
- `pattern_char_matches` is a stub-or-absent (M2 leaves it pending; it is the
  sole caller of the 3 leaf predicates → they don't exist yet either).
- `match_with_anchors` is a **STUB** returning `false` (T2.S2 not done) ⇒ the
  public `pattern_match()` returns `false` for EVERY input until T2.S2 lands.

## Consequence #1 — T1 cannot be validated via the public API

Because `match_with_anchors` is still a stub, `pattern_match()` is inert.
So `test_word_boundary_basic.c` (74), `test_word_boundary_integration.c` (189),
`test_char_classification.c` (179) — all of which drive the PUBLIC API — will
**not** exercise T1 behavior until T2.S2. The ONLY way to validate the 4 static
classifiers in isolation is a **direct unit probe** that does
`#include "pattern_match.c"` (NOT `.h`) so the `static` functions are reachable.

## Consequence #2 — transient `-Wunused-function` after T1 alone

`is_word_boundary` gains its caller (nfa_addstate) immediately ⇒ no warning, and
`\b`/`\B` logic becomes correct (though unobservable until T2.S2). The three
leaf predicates (`is_digit_char`, `is_word_char`, `is_whitespace_char`) are
called ONLY by `pattern_char_matches` (T2.S1). If T2.S1 has not landed, they
emit `-Wunused-function`. This is EXPECTED and transient; the P1M2 PRP
explicitly forbids suppressing it with `__attribute__((unused))` (lines 571, 892).
Gate: tolerate exactly those 3 known warnings until T2.S1.

## The exact code (live source-of-truth, pattern_match.c:454–477)

```c
static bool is_digit_char(char c) { return c >= '0' && c <= '9'; }

static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || (c == '_');          /* NOTE: underscore */
}

static bool is_whitespace_char(char c) {
    return c == ' ' || c == '\t' || c == '\n'
        || c == '\r' || c == '\f' || c == '\v';           /* exactly 6 chars */
}

static bool is_word_boundary(const char *str, size_t pos) {
    if (!str) return false;
    size_t str_len = strlen(str);
    if (pos == 0)        return (str_len > 0 && is_word_char(str[0]));
    if (pos == str_len)  return (str_len > 0 && is_word_char(str[str_len - 1]));
    if (pos > str_len)   return false;
    return is_word_char(str[pos - 1]) != is_word_char(str[pos]);  /* interior XOR */
}
```

## Hard requirements distilled from PRD §7.6 / §13 #10 / architecture

1. `is_word_boundary` is a **pure position test**. The empty-original-string
   `\b`/`\B` "neither matches" semantics live UPSTREAM in `nfa_addstate`'s
   `OP_ASSERT` branch (`*string_start != '\0' &&`). Do NOT move them here.
2. `is_word_char` includes `_` → `[A-Za-z0-9_]`. `\w`/`\W`/`\b`/`\B` all depend.
3. `is_whitespace_char` is exactly 6 chars. Do NOT use `<ctype.h>` `isspace`
   (locale-dependent + needs unsigned-char cast; the explicit ranges are exact).
4. Plain-`char` args need NO unsigned cast for the ASCII range tests — a high-bit
   byte (>0x7F) is negative as signed char but still falls outside every range.
   (Only `tolower` needs the cast — that's T2.S1's concern, not T1's.)
5. Replace the M2 STUB `is_word_boundary`; place all four before `nfa_addstate`.

## References (authoritative)
- PRD.md §7.6 "Character classification helpers (static)" — exact contract.
- PRD.md §7.5 "OP_ASSERT" + §13 #10 — abspos/empty-string semantics in nfa_addstate.
- PRD.md §15 Appendix A — `\bword\b` vs `a word here` (cs=0 → true) truth table.
- architecture/pattern_match_architecture.md:156–177 (classifier narrative).
- Russ Cox, "Regular Expression Matching Can Be Simple And Fast",
  https://swtch.com/~rsc/regexp/regexp1.html (Thompson NFA background).