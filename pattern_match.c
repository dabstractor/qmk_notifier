/* SPDX-License-Identifier: GPL-2.0-or-later */
/* pattern_match.c — Thompson-NFA pattern matcher (rebuilt incrementally; see PRD §7). */

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>   /* tolower() for case-insensitive matching in pattern_char_matches */

/*
 * Process escape sequences, the dot metacharacter, and the '+' quantifier in a
 * pattern. Uses control-byte placeholders for elements the matcher must treat
 * specially (see PRD §7.1 "processed-pattern byte contract"):
 *   \x01-\x04 : escaped literals ^ $ * \         \x05-\x0A : classes \d \D \w \W \s \S
 *   \x0B \x0C : zero-width \b \B                 \x0D      : dot metacharacter
 *   \x0E      : '+' quantifier (follows the element it quantifies)
 * A literal '.' (from \.) and literal '+' (from \+ or a bare '+' not following a
 * consumable element) are emitted as their ordinary ASCII bytes (0x2E / 0x2B);
 * a bare '*' is emitted as its ordinary byte 0x2A (the glob wildcard).
 *
 * This is the single malloc per pattern_match() call; the result is freed by
 * free_parsed_pattern(). Output length is always <= input length, so
 * malloc(strlen(pattern)+1) is sufficient.
 */
static char *process_escapes(const char *pattern) {
    if (!pattern) return NULL;

    size_t len = strlen(pattern);
    char *processed = malloc(len + 1);
    if (!processed) return NULL;

    const char *src = pattern;
    char *dst = processed;
    bool last_consumable = false;   /* did the previous emitted element consume a char? */

    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            src++;  /* skip the backslash */
            switch (*src) {
                case '^':  *dst++ = '\x01'; src++; last_consumable = true;  break;  /* \^ */
                case '$':  *dst++ = '\x02'; src++; last_consumable = true;  break;  /* \$ */
                case '*':  *dst++ = '\x03'; src++; last_consumable = true;  break;  /* \* */
                case '\\': *dst++ = '\x04'; src++; last_consumable = true;  break;  /* \\ */
                case '.':  *dst++ = '.';    src++; last_consumable = true;  break;  /* \.  -> literal dot  (0x2E) */
                case '+':  *dst++ = '+';    src++; last_consumable = true;  break;  /* \+  -> literal plus (0x2B) */
                case 'd':  *dst++ = '\x05'; src++; last_consumable = true;  break;  /* \d */
                case 'D':  *dst++ = '\x06'; src++; last_consumable = true;  break;  /* \D */
                case 'w':  *dst++ = '\x07'; src++; last_consumable = true;  break;  /* \w */
                case 'W':  *dst++ = '\x08'; src++; last_consumable = true;  break;  /* \W */
                case 's':  *dst++ = '\x09'; src++; last_consumable = true;  break;  /* \s */
                case 'S':  *dst++ = '\x0A'; src++; last_consumable = true;  break;  /* \S */
                case 'b':  *dst++ = '\x0B'; src++; last_consumable = false; break;  /* \b zero-width */
                case 'B':  *dst++ = '\x0C'; src++; last_consumable = false; break;  /* \B zero-width */
                default:   /* unrecognized escape: keep backslash + char literally (2 bytes) */
                    *dst++ = '\\'; *dst++ = *src++; last_consumable = true; break;
            }
        } else if (*src == '\\' && *(src + 1) == '\0') {
            *dst++ = *src++;        /* trailing lone backslash -> literal '\' */
            last_consumable = true;
        } else if (*src == '*') {
            *dst++ = '*'; src++;    /* bare '*' -> 0x2A glob wildcard (handled by the matcher) */
            last_consumable = false;
        } else if (*src == '+') {
            if (last_consumable) {
                *dst++ = '\x0E';    /* quantifier: one-or-more of the previous element */
                last_consumable = false;
            } else {
                *dst++ = '+';       /* literal '+' (not after a consumable element) */
                last_consumable = true;
            }
            src++;
        } else if (*src == '.') {
            *dst++ = '\x0D'; src++; /* bare '.' dot metacharacter */
            last_consumable = true;
        } else {
            *dst++ = *src++;        /* ordinary literal */
            last_consumable = true;
        }
    }

    *dst = '\0';
    return processed;
}

/* ===== P1.M1.T2.S2: parse_pattern, free_parsed_pattern, get_escaped_char,
 *                     pattern_match (public); match_with_anchors (PRD §7.4, P1.M3.T2.S2) ===== */

/* Holds the result of parsing a user pattern: anchor flags + the
 * process_escapes()-processed core the NFA consumes. */
typedef struct {
    const char *core_pattern;    /* points into processed_pattern, or the raw pattern on malloc failure */
    bool        start_anchored;  /* true if the original pattern began with '^' */
    bool        end_anchored;    /* true if the original pattern ended with an unescaped '$' */
    char       *processed_pattern; /* malloc'd by process_escapes(); freed by free_parsed_pattern() */
} parsed_pattern_t;

/* Forward declaration: match_with_anchors (the anchor strategy, P1.M3.T2.S2)
 * and its NFA helpers are defined below; forward-declared here so pattern_match
 * (the public entry) can call match_with_anchors before its definition. */
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive);

/* Forward declarations for the NFA simulation + anchor helpers (GOTCHA-6):
 * match_with_anchors calls the two wrappers, which call nfa_match; all are
 * defined at the bottom (after nfa_addstate). Forward-declared here so the
 * match_with_anchors body can sit at its existing site. */
static bool nfa_match(const char *pattern, const char *str,
                      const char *string_start, bool case_sensitive,
                      bool full_match);
static bool match_string_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive);
static bool match_reaches_end_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive);

/* Reverse-map a process_escapes() placeholder byte back to the human-readable
 * character. Used by pattern_char_matches() (P1.M3.T2.S1) for the escaped-literal
 * branch (0x01-0x04); the class/assertion/dot entries (0x05-0x0D) are kept for
 * debug/diagnostic readability. (Item-spec §3c, debug-only.) */
static char get_escaped_char(char placeholder) {
    switch (placeholder) {
        case '\x01': return '^';   /* \^ */
        case '\x02': return '$';   /* \$ */
        case '\x03': return '*';   /* \* */
        case '\x04': return '\\';  /* \\ */
        /* The following metacharacters have no single literal equivalent; they
         * represent classes/assertions handled directly in pattern_char_matches.
         * Returned here only for debug/error messages. */
        case '\x05': return 'd';   /* \d */
        case '\x06': return 'D';   /* \D */
        case '\x07': return 'w';   /* \w */
        case '\x08': return 'W';   /* \W */
        case '\x09': return 's';   /* \s */
        case '\x0A': return 'S';   /* \S */
        case '\x0B': return 'b';   /* \b */
        case '\x0C': return 'B';   /* \B */
        case '\x0D': return '.';   /* .  (dot metacharacter) */
        default:     return placeholder;  /* ordinary literal byte */
    }
}

/* Release the malloc'd processed-pattern buffer (if any) and NULL both pointers.
 * Safe on a zero-initialized struct or when parsed == NULL. On the malloc-failure
 * fallback path processed_pattern is NULL and core_pattern points at the CALLER's
 * pattern, so we must NOT free core_pattern — the `processed_pattern` guard
 * ensures we only ever free what we allocated. */
static void free_parsed_pattern(parsed_pattern_t *parsed) {
    if (parsed && parsed->processed_pattern) {
        free(parsed->processed_pattern);
        parsed->processed_pattern = NULL;
        parsed->core_pattern = NULL;  /* it aliased processed_pattern (or the caller's pattern) */
    }
}

/* Detect a leading '^' (start anchor) and a trailing UNESCAPED '$' (end anchor),
 * carve out the core substring between them, and process its escapes.
 *
 * EVEN-BACKSLASH-COUNT RULE for the end anchor (PRD §7.3, item-spec §6 Mode A):
 * A trailing '$' is a real end anchor ONLY when an EVEN number of backslashes
 * (including zero) immediately precede it. An ODD count means the '$' is escaped
 * and is part of the core (process_escapes turns '\$' into the 0x02 literal).
 *   "abc$"     : 0 backslashes => end anchor,  core = "abc"
 *   "abc\\$"    : 1 backslash   => escaped '$',  core = "abc" + 0x02
 *   "abc\\\\$"   : 2 backslashes => end anchor,   core = "abc" + 0x04 (the '\\' -> 0x04)
 *   "abc\\\\\\$"  : 3 backslashes => escaped '$',  core = "abc" + 0x04 + 0x02
 * This is the standard "is the final metacharacter quoted?" test: walk left from
 * the '$' counting consecutive '\\'; even => unquoted.
 *
 * The `end > start` guard rejects degenerate inputs: a lone "^" (start anchor
 * only) leaves start==end after skipping '^', so no end check runs; "^$" still
 * detects both anchors with an empty core (PRD §15: '^$' matches the empty string). */
static parsed_pattern_t parse_pattern(const char *pattern) {
    parsed_pattern_t parsed = {0};   /* all flags false, all pointers NULL */

    if (!pattern) {
        return parsed;
    }

    const char *start = pattern;
    const char *end   = pattern + strlen(pattern);

    /* Start anchor: a leading '^' is always a start anchor (it cannot be an
     * escape target as the first char; '\^' would be processed to 0x01 later). */
    if (*start == '^') {
        parsed.start_anchored = true;
        start++;                     /* skip the '^' */
    }

    /* End anchor: trailing '$' that is NOT escaped (even backslash count). */
    if (end > start && *(end - 1) == '$') {
        int backslash_count = 0;
        const char *check = end - 2;
        while (check >= start && *check == '\\') {
            backslash_count++;
            check--;
        }
        if (backslash_count % 2 == 0) {   /* even (0,2,4,...) => unescaped '$' */
            parsed.end_anchored = true;
            end--;                          /* drop the '$' */
        }
    }

    /* Carve the core (between anchors) and process its escapes. */
    size_t core_len = (size_t)(end - start);
    char *core_pattern = malloc(core_len + 1);
    if (!core_pattern) {
        /* malloc failure: fall back to the raw pattern, no escape processing.
         * processed_pattern stays NULL => free_parsed_pattern() is a no-op. */
        parsed.core_pattern      = pattern;
        parsed.processed_pattern = NULL;
        return parsed;
    }
    strncpy(core_pattern, start, core_len);
    core_pattern[core_len] = '\0';

    parsed.processed_pattern = process_escapes(core_pattern);
    free(core_pattern);              /* temp copy no longer needed */

    if (parsed.processed_pattern) {
        parsed.core_pattern = parsed.processed_pattern;
    } else {
        /* process_escapes failed (its own malloc): fall back to the raw pattern. */
        parsed.core_pattern = pattern;
    }

    return parsed;
}

/* ===== P1.M3.T2.S2: match_with_anchors — the anchor strategy (PRD §7.4) =====
 * Replaces the temporary STUB. Picks the NFA mode (full-match vs reach-any) and
 * whether to loop over start offsets based on the parsed anchor flags:
 *   ^...$ exact -> one full match from offset 0
 *   ^...       prefix -> one reach-any match from offset 0
 *   ...$       suffix -> loop offsets, full match from each
 *   ...        substring -> loop offsets, reach-any from each (empty core only
 *               matches the empty string). */
static bool match_with_anchors(const parsed_pattern_t *parsed, const char *str, bool case_sensitive) {
    if (!parsed || !str) return false;
    const char *core_pattern = parsed->core_pattern;

    if (parsed->start_anchored && parsed->end_anchored) {        /* ^...$ exact */
        return match_reaches_end_with_start(core_pattern, str, str, case_sensitive);
    } else if (parsed->start_anchored) {                         /* ^ prefix */
        return match_string_with_start(core_pattern, str, str, case_sensitive);
    } else if (parsed->end_anchored) {                           /* $ suffix */
        size_t str_len = strlen(str);
        for (size_t i = 0; i <= str_len; i++)
            if (match_reaches_end_with_start(core_pattern, str + i, str, case_sensitive))
                return true;
        return false;
    } else {                                                     /* substring (default) */
        if (strlen(core_pattern) == 0) return strlen(str) == 0;  /* empty core -> only empty string */
        size_t str_len = strlen(str);
        for (size_t i = 0; i <= str_len; i++)
            if (match_string_with_start(core_pattern, str + i, str, case_sensitive))
                return true;
        return false;
    }
}

/* ===== PUBLIC API (declared in pattern_match.h) =====
 * NULL-guard -> parse -> match -> free. Caller frees nothing (PRD §6). */
bool pattern_match(const char *pattern, const char *str, bool case_sensitive) {
    if (!pattern || !str) {
        return false;
    }

    parsed_pattern_t parsed = parse_pattern(pattern);

    bool result = match_with_anchors(&parsed, str, case_sensitive);

    free_parsed_pattern(&parsed);

    return result;
}

/* ===== P1.M2 NFA Engine core definitions (State, ops, sizing) =====
 * Consumed by nfa_compile() [P1.M2.T1.S2] and nfa_addstate()/nfa_match()
 * [P1.M2.T2]. Thompson construction; linear-time simulation, no backtracking.
 * Reference: Russ Cox, "Regular Expression Matching Can Be Simple And Fast",
 * https://swtch.com/~rsc/regexp/regexp1.html  (see PRD §7.5, §7.9). */

/* Pool sizing. nfa_compile() declares `State pool[NFA_MAX_STATES]` on its stack
 * (NOT static — the pool must be fresh each call so lastlist starts at 0).
 * NFA_MAX_PATTERN is a per-target resource knob (PRD §7.9): the host/test build
 * uses a large default so the stress suites' multi-KB patterns fit; low-RAM AVR
 * QMK builds override it (e.g. `#define NFA_MAX_PATTERN 64` before #include in
 * notifier.c) to stay within the §7.9 "~6–8 KB" stack budget. State is ~32–40 B
 * on 64-bit, so each unit of NFA_MAX_PATTERN costs ~64–80 B of stack per call. */
#ifndef NFA_MAX_PATTERN
/* Per-target resource knob (PRD §7.9). Host/test default so the stress
 * suites' multi-KB patterns fit; low-RAM AVR QMK builds `#define NFA_MAX_PATTERN`
 * (e.g. 64 or 128) BEFORE `#include "pattern_match.c"` in notifier.c, and this
 * guard makes that override silent (no macro-redefinition warning). */
#define NFA_MAX_PATTERN 2048                        /* host/test default; QMK overrides via notifier.c */
#endif
#define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)   /* 2 per byte + MATCH + slack (e.g. 4098 at default) */

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
 * Consumed by nfa_addstate (read) and nfa_match (bumped once per phase). */
static int nfa_gen = 0;

/* ===== P1.M2.T1.S2: nfa_compile() — Thompson construction =====
 * Compile a processed-pattern byte string (process_escapes output, P1.M1.T2.S1)
 * into a State pool via Thompson construction. The caller (nfa_match,
 * P1.M2.T2.S2) allocates `State pool[NFA_MAX_STATES]` on its stack and passes it
 * in; we fill it and return the start state.
 *
 * WHY A COMPILED NFA (not backtracking): the previous engine backtracked and went
 * EXPONENTIAL on patterns like a+a+a+...b against a long run of a (PRD §7.8).
 * Thompson construction compiles once and the later simulator runs in guaranteed
 * O(states x input_len). Crucially, X+ compiles to exactly TWO states (OP_CHAR +
 * OP_SPLIT loop-back), so a+a+a+... scales as 2k+1 — never 2^k. See Russ Cox,
 * "Regular Expression Matching Can Be Simple And Fast",
 * https://swtch.com/~rsc/regexp/regexp1.html . */

/* ---- compile: processed pattern -> State pool, returns start state ----
 * Threads `State **tail`: it points at the slot where the NEXT unit's start node
 * must be written (initially `&start`). Each construct writes *tail = <its start>
 * then advances tail to its own "dangling exit" slot (out1 for SPLIT, out for
 * CHAR/ASSERT). At the end we write the OP_MATCH into the final dangling slot. */
static State *nfa_compile(const char *pat, State *pool, int *nstates_out) {
    int n = 0;
    State *start = NULL;
    State **tail = &start;            /* slot to write the next unit's start into */

    /* Bounds-safe state allocator: return &pool[n] and advance n, but clamp at
     * NFA_MAX_STATES so a pathological pattern reuses the last slot instead of
     * overflowing. (2 per byte + MATCH + slack fits any <=128-byte pattern.) */
    #define NEW() (&pool[n < NFA_MAX_STATES ? n++ : (NFA_MAX_STATES - 1)])   /* allocate one state (clamp: reuse last slot, never overflow) */

    for (const char *p = pat; *p; p++) {
        unsigned char b = (unsigned char)*p;

        if (b == 0x2A) {                         /* (a) glob '*' == regex '.*' */
            /* Thompson construction for .*:  SPLIT -> ANY(loop back) -> exit.
             * OP_ANY consumes ANY byte incl. '\n'/'\r' (distinct from the dot,
             * which excludes them). The SPLIT's out1 is the dangled exit. */
            State *any = NEW(); any->op = OP_ANY;
            State *sp  = NEW(); sp->op  = OP_SPLIT; sp->out = any; sp->out1 = NULL;
            any->out = sp;                        /* loop back: ANY -> SPLIT */
            *tail = sp; tail = &sp->out1;         /* entry = SPLIT; exit via out1 */

        } else if (b == 0x0B || b == 0x0C) {      /* (b) \b / \B : zero-width assert */
            /* OP_ASSERT consumes no input; the simulator (nfa_addstate, P1.M2.T2.S1)
             * recurses into `out` only if is_word_boundary(...) matches. arg carries
             * 0x0B (\b, want boundary) or 0x0C (\B, want non-boundary). */
            State *a = NEW(); a->op = OP_ASSERT; a->arg = (char)b; a->out = NULL;
            *tail = a; tail = &a->out;

        } else if (b == 0x0E) {
            /* (c) standalone quantifier marker — should NOT occur: process_escapes
             * only emits 0x0E immediately after a consuming element (handled below).
             * Skip defensively to stay robust if it ever appears alone. */
            continue;

        } else {                                  /* (d) consuming element X */
            /* X is any byte that consumes one input char: an escaped literal
             * (0x01-0x04), a class (0x05-0x0A), the dot (0x0D), a literal '.'
             * (0x2E) / '+' (0x2B), or any ordinary ASCII byte. Compile to OP_CHAR. */
            State *c = NEW(); c->op = OP_CHAR; c->arg = (char)b; c->out = NULL;

            if ((unsigned char)p[1] == 0x0E) {    /* X+ : one-or-more, LINEAR (PRD §7.8) */
                /* Thompson 'plus': after matching one X (c), reach an OP_SPLIT whose
                 * `out` loops BACK to c (match more) and whose `out1` exits. This is
                 * exactly 2 states per X+, so a+a+a+... compiles as 2k+1 — the fix
                 * for the old exponential backtracker. */
                State *sp = NEW(); sp->op = OP_SPLIT; sp->out = c; sp->out1 = NULL;
                c->out = sp;                      /* after one X, reach the split */
                *tail = c; tail = &sp->out1;      /* entry = c; exit via split.out1 */
                p++;                              /* consume the 0x0E marker */
            } else {
                /* Plain single consuming element: entry = c, exit via c->out. */
                *tail = c; tail = &c->out;
            }
        }
    }

    /* (e) End: append the single accepting state into the final dangling slot. */
    State *m = NEW(); m->op = OP_MATCH;           /* accepting state */
    *tail = m;

    /* Zero lastlist on every allocated state. The pool is stack-fresh, but this is
     * mandatory: nfa_gen starts at 0 and the simulator's FIRST epsilon-closure
     * (nfa_addstate) guards on lastlist == nfa_gen (== 0); zeroing guarantees no
     * state is wrongly pre-marked. PRD §7.5: "Zero lastlist on every allocated
     * state (the pool is fresh each call)." */
    for (int i = 0; i < n; i++) pool[i].lastlist = 0;

    *nstates_out = n;

    #undef NEW
    return start;
}

/* ===== P1.M2.T2.S1: nfa_addstate() — epsilon-closure with lastlist guard =====
 * Epsilon-closure primitive for the Thompson-NFA simulator. Given a state `s`,
 * follow all epsilon transitions (OP_SPLIT forks and — conditionally — OP_ASSERT
 * zero-width edges) and collect every OP_CHAR / OP_ANY / OP_MATCH state that is
 * "live" at the current input position into `list[*n .. )`.
 *
 * THE lastlist GUARD IS MANDATORY (PRD §13 invariant #11): without
 * `if (s->lastlist == nfa_gen) return;`, an OP_SPLIT whose two branches converge
 * on a shared successor (and repeated \b/\B at one position) would recurse
 * infinitely. The generation tag `nfa_gen` is bumped ONCE per simulation phase
 * by the caller (nfa_match, P1.M2.T2.S2), so each closure gets a fresh "seen"
 * set at O(1) cost — no memset, no allocation. See Russ Cox, "Regular Expression
 * Matching Can Be Simple And Fast", https://swtch.com/~rsc/regexp/regexp1.html .
 *
 * NOTE on is_word_boundary: the real position-based classifier is implemented
 * immediately below (P1.M3.T1.S1); the signature (size_t pos) is fixed by
 * PRD §7.6 so the call site in nfa_addstate never changes. */

/* ===== P1.M3.T1.S1: character classifiers + real is_word_boundary =====
 * Position-based word-boundary test and the three class predicates consumed by
 * pattern_char_matches (P1.M3.T2.S1). is_word_boundary replaces the STUB that
 * was provided while the classifier was pending; the signature (size_t pos) is
 * fixed by PRD §7.6, so the call site in nfa_addstate is unchanged. */

static bool is_digit_char(char c) { return c >= '0' && c <= '9'; }

static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || (c == '_');
}

static bool is_whitespace_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* Word-boundary test against the ORIGINAL string (PRD §7.6, §13 #10). A boundary
 * exists at `pos` when exactly one of the neighboring characters is a word char.
 * Edge positions use an implicit non-word char on the off-string side. The
 * empty-original-string case is short-circuited inside nfa_addstate's OP_ASSERT
 * branch BEFORE this is called, but we keep the NULL + len guards defensive. */
static bool is_word_boundary(const char *str, size_t pos) {
    if (!str) return false;
    size_t str_len = strlen(str);
    if (pos == 0)        return (str_len > 0 && is_word_char(str[0]));
    if (pos == str_len)  return (str_len > 0 && is_word_char(str[str_len - 1]));
    if (pos > str_len)   return false;
    return is_word_char(str[pos - 1]) != is_word_char(str[pos]);
}

/* ---- epsilon-closure add (follow SPLIT/ASSERT, collect CHAR/ANY/MATCH) ---- */
static void nfa_addstate(State **list, int *n, State *s,
                         const char *string_start, size_t abspos) {
    /* De-dup + NULL-safe: skip if `s` is NULL or already in THIS closure.
     * lastlist == nfa_gen means we have already added/followed `s` during the
     * current simulation phase (nfa_gen is bumped once per phase by nfa_match).
     * This single guard is what makes OP_SPLIT and \b\b terminate (PRD §13 #11). */
    if (!s || s->lastlist == nfa_gen) return;     /* already in this closure */

    /* Mark seen for THIS generation BEFORE dispatching, so a state reached via an
     * OP_SPLIT branch is not re-added when the OTHER branch converges on it. */
    s->lastlist = nfa_gen;

    if (s->op == OP_MATCH) {
        /* Accepting state: collect it; nfa_has_match (P1.M2.T2.S2) reports the
         * match by scanning the list for an OP_MATCH. */
        list[(*n)++] = s;
        return;
    }

    if (s->op == OP_SPLIT) {
        /* Epsilon fork (glob '*', 'X+'): follow BOTH out and out1 WITHOUT
         * consuming input. abspos is forwarded UNCHANGED to both branches
         * (PRD §13 #10: abspos is absolute from string_start; epsilon edges do
         * not advance the input position). */
        nfa_addstate(list, n, s->out,  string_start, abspos);
        nfa_addstate(list, n, s->out1, string_start, abspos);
        return;
    }

    if (s->op == OP_ASSERT) {
        /* Zero-width assertion \b (arg==0x0B, want a boundary) / \B (arg==0x0C,
         * want a NON-boundary). Recurse into `out` ONLY if the boundary
         * condition holds. abspos is absolute (PRD §13 #10) so \b/\B evaluate
         * against the ORIGINAL string, not the per-offset pointer.
         *
         * EMPTY-STRING SPECIAL CASE (legacy semantics the test suite encodes):
         * if the original string is empty (*string_start == '\0'), NEITHER a
         * boundary nor a non-boundary passes, so we do NOT recurse. The empty-
         * string check short-circuits BEFORE calling is_word_boundary, so this
         * behavior is independent of the is_word_boundary implementation. */
        int want_boundary = (s->arg == 0x0B);     /* \b wants a boundary; \B wants none */
        if (*string_start != '\0' &&
            is_word_boundary(string_start, abspos) == want_boundary)
            nfa_addstate(list, n, s->out, string_start, abspos);
        return;                                   /* never collect an ASSERT state itself */
    }

    /* OP_CHAR / OP_ANY: a consuming state. Add it to the list; it is "live" and
     * waiting for the simulator to feed it the next input char (nfa_match,
     * P1.M2.T2.S2). OP_ANY (glob '*') matches any byte incl. '\n'/'\r'; OP_CHAR
     * is tested via pattern_char_matches (P1.M3.T2.S1). */
    list[(*n)++] = s;
}

/* ===== P1.M3.T2.S1: pattern_char_matches — single-byte match predicate =====
 * Test whether a processed-pattern byte `pc` matches an input char `sc`. The
 * processed byte encodes either an escaped literal (0x01-0x04), a character
 * class (0x05-0x0A), or the dot (0x0D); any other byte is an ordinary literal.
 * Matching is case-folded via tolower() unless `case_sensitive` is set (PRD §7.7).
 * Escaped-literal placeholders are decoded via get_escaped_char() FIRST and then
 * folded — never fold the placeholder byte itself. tolower() takes an unsigned
 * char value, so args are cast to (unsigned char) to avoid sign-extension UB. */
static bool pattern_char_matches(char pc, char sc, bool case_sensitive) {
    if (pc >= '\x01' && pc <= '\x04') {                 /* escaped literal */
        char literal = get_escaped_char(pc);
        return case_sensitive ? (literal == sc)
              : (tolower((unsigned char)literal) == tolower((unsigned char)sc));
    }
    switch (pc) {
        case '\x05': return is_digit_char(sc);          /* \d */
        case '\x06': return !is_digit_char(sc);         /* \D */
        case '\x07': return is_word_char(sc);           /* \w */
        case '\x08': return !is_word_char(sc);          /* \W */
        case '\x09': return is_whitespace_char(sc);     /* \s */
        case '\x0A': return !is_whitespace_char(sc);    /* \S */
        case '\x0D': return (sc != '\n' && sc != '\r'); /* .  (dot excludes newline) */
        default:                                        /* ordinary literal */
            return case_sensitive ? (pc == sc)
                  : (tolower((unsigned char)pc) == tolower((unsigned char)sc));
    }
}

/* ===== P1.M2.T2.S2: nfa_has_match + nfa_match — the Thompson simulation =====
 * Two-list simulation of the compiled NFA (Russ Cox). Compile once (nfa_compile),
 * maintain clist (current live states) + nlist (next live states). nfa_gen is
 * bumped once per phase so nfa_addstate's lastlist guard de-dups the closure in
 * O(states) with no allocation — guaranteed O(states x strlen), no backtracking
 * (the fix for the old exponential matcher; PRD §7.8). See
 * https://swtch.com/~rsc/regexp/regexp1.html . */

/* Report whether an accepting OP_MATCH state is on the current state list. */
static int nfa_has_match(State **list, int n) {
    for (int i = 0; i < n; i++) if (list[i]->op == OP_MATCH) return 1;
    return 0;
}

/* full_match=false: MATCH reachable at any point (prefix/substring match).
 * full_match=true:  MATCH reachable only after consuming the WHOLE string. */
static bool nfa_match(const char *pattern, const char *str,
                      const char *string_start, bool case_sensitive,
                      bool full_match) {
    State pool[NFA_MAX_STATES];
    int nstates;
    State *start = nfa_compile(pattern, pool, &nstates);
    if (!start) return full_match ? (*str == '\0') : true;   /* defensive guard (GOTCHA-2) */
    (void)nstates;

    State *clist_buf[NFA_MAX_STATES];
    State *nlist_buf[NFA_MAX_STATES];
    State **clist = clist_buf, **nlist = nlist_buf;
    int cn = 0, nn;
    size_t abspos = (size_t)(str - string_start);            /* absolute offset (PRD §13 #10) */

    nfa_gen++;                                               /* seed closure (GOTCHA-3) */
    nfa_addstate(clist, &cn, start, string_start, abspos);
    if (!full_match && nfa_has_match(clist, cn)) return true;/* empty prefix matched */

    size_t pos = abspos;
    for (const char *p = str; *p; p++, pos++) {
        char c = *p;
        nfa_gen++; nn = 0;                                   /* fresh phase */
        for (int i = 0; i < cn; i++) {
            State *s = clist[i];
            if (s->op == OP_ANY) {                           /* glob '*': ANY byte incl \n/\r (PRD §13 #8) */
                nfa_addstate(nlist, &nn, s->out, string_start, pos + 1);
            } else if (s->op == OP_CHAR &&
                       pattern_char_matches(s->arg, c, case_sensitive)) {
                nfa_addstate(nlist, &nn, s->out, string_start, pos + 1);
            }
        }
        State **tmp = clist; clist = nlist; nlist = tmp; cn = nn;  /* swap lists */
        if (cn == 0) break;                                  /* dead — no live states */
        if (!full_match && nfa_has_match(clist, cn)) return true; /* prefix matched */
    }
    return nfa_has_match(clist, cn) ? true : false;          /* full: accept only at end */
}

/* ===== P1.M3.T2.S2: anchor-strategy wrappers (thin forwarders to nfa_match) =====
 * match_string_with_start     -> reach-any (substring/prefix; full_match=false).
 * match_reaches_end_with_start -> consume-whole-remaining (suffix/exact; full_match=true).
 * Both forward the ORIGINAL string_start so \b/\B compute absolute positions. */
static bool match_string_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive) {
    return nfa_match(pattern, str, string_start, case_sensitive, false);
}
static bool match_reaches_end_with_start(const char *pattern, const char *str,
        const char *string_start, bool case_sensitive) {
    return nfa_match(pattern, str, string_start, case_sensitive, true);
}
