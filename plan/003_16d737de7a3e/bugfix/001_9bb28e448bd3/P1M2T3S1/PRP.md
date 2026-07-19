name: "PRP — P1.M2.T3.S1: Document has_been_queried as intentionally write-only in notifier.c"
description: |

  Issue 4 (PRD h3.3), documentation-only fix. `has_been_queried` is set to `true`
  on the first `QUERY_INFO` serviced but is NEVER read by any code path. PRD §4.6
  requires only that the firmware SET it (the host enforces "at most once per
  board boot"). This task replaces the two terse existing comments (declaration +
  assignment) with a clear, accurate explanation of the write-only design
  rationale, citing §4.6 and the wire-protocol-compatibility reason for NOT
  exposing it in the QUERY_INFO reply. NO functional code change. The comment IS
  the deliverable.

---

## Goal

**Feature Goal**: Make `notifier.c` self-documenting about why
`has_been_queried` is write-only. A future contributor (or static analyzer)
reading the variable must immediately understand: (a) it is intentionally set but
never read; (b) the PRD §4.6 contract that makes the firmware's job only to SET
it (the host enforces at-most-once-per-boot); (c) why it is NOT exposed in the
QUERY_INFO reply (the reply is a fixed 4-byte payload; a 5th byte would change
the wire-protocol response size and risk host compatibility).

**Deliverable**: ONE file modified — `notifier.c`. Exactly TWO comment lines
replaced:
1. The trailing comment on the declaration at **notifier.c:194**.
2. The trailing comment on the assignment at **notifier.c:714**.
No other line changes. No functional code change.

**Success Definition**:
- The declaration (line 194) carries a multi-line comment stating: write-only by
  design (PRD §4.6); set on first QUERY_INFO; never read; the host enforces
  at-most-once-per-boot; reserved for future firmware-side observability; NOT
  exposed in the 4-byte QUERY_INFO reply to preserve wire compatibility.
- The assignment (line 714) carries a concise comment noting it is write-only
  (§4.6), never read, and points to the declaration comment.
- `has_been_queried` is STILL only declared + assigned (no new read introduced).
- `git diff notifier.c` shows ONLY comment changes — every changed line is inside
  a `/* ... */` comment; no functional statement changed.
- Build still compiles; `./run_all_tests.sh` and `./run_notifier_stub_tests.sh`
  remain green (sanity — comments cannot change behavior, but confirm valid C).

## User Persona (if applicable)

**Target User**: A future contributor or reviewer reading `notifier.c` who notices
`has_been_queried` is written but never read and wonders whether it is a bug or
dead state to delete. Also static analyzers / linters flagging unused-stores.

**Use Case**: Reading the HOST STATE block or the QUERY_INFO handler; the comment
answers "is this dead code I should remove?" — NO, it is an intentional §4.6
write-only contract flag, and here is exactly why it is not read or exposed.

**User Journey**: grep `has_been_queried` → read the declaration comment →
understand the §4.6 contract and the wire-compat reason → leave it alone (or,
for a future feature, know the one constraint: do not enlarge the QUERY_INFO reply
without a protocol bump).

**Pain Points Addressed**: The current comments ("set on first QUERY_INFO service
(§4.6 handshake-timing rule)") describe *when* it is set but not *why it is never
read* or *why it is not exposed*. Issue 4 (h3.3) explicitly calls this out as a
"dead state / code smell" to resolve by documenting the intent.

## Why

- **Resolves Issue 4 (h3.3) the recommended way.** The bugfix PRD lists two
  options: document it as intentionally write-only, OR expose it via QUERY_INFO.
  The architecture `findings_and_risks.md` and the item RESEARCH NOTE both
  recommend DOCUMENTATION (exposing would change the 4-byte reply to 5 bytes,
  risking host compatibility — NOT appropriate for a Minor issue). This task is
  that documentation.
- **Prevents a future regression.** Without the rationale, a contributor may
  "helpfully" expose the flag (breaking the wire protocol) or delete it (dropping
  the §4.6-required firmware-side set). The comment locks in the intent.
- **Zero risk.** Comment-only; no behavior, wire, build, or test change.
- **The in-source comment IS the documentation** (item DOCS point 5) — no separate
  docs subtask, no README change (that is P1.M2.T4.S1's scope).

## What

Replace the two existing terse trailing comments on `has_been_queried` with
accurate, §4.6-cited explanations. Use the `edit` tool with the EXACT oldText
anchors given in "Implementation Tasks". Do NOT change any functional line.

### Success Criteria

- [ ] notifier.c:194 declaration comment states: write-only by design (§4.6);
      set on first QUERY_INFO; never read; host enforces at-most-once-per-boot;
      reserved for future observability; NOT exposed in the 4-byte QUERY_INFO
      reply (wire-compat).
- [ ] notifier.c:714 assignment comment notes write-only (§4.6), never read,
      points to the declaration comment.
- [ ] `grep -rn 'has_been_queried' *.c *.h qmk_stubs/*.c` still returns exactly
      the declaration (194) + the assignment (714) — NO new read site.
- [ ] `git diff notifier.c` changes ONLY comment lines (every `+`/`-` line is
      inside `/* ... */` or is the unchanged `static bool ... = false;` /
      `has_been_queried = true;` statement).
- [ ] `./run_notifier_stub_tests.sh` → still compiles notifier.c clean + all
      binaries green (dispatch/os/host); `./run_all_tests.sh` → still green.
- [ ] `git status` shows ONLY `notifier.c` modified (+ plan/ PRP/research).

## All Needed Context

### Context Completeness Check

**Pass.** The exact current text of both comment lines (verified by reading
notifier.c), the exact replacement text (given verbatim in "Implementation
Tasks"), the PRD §4.6 rationale (PRD.md:466-475, quoted below), the 4-byte
QUERY_INFO payload layout (notifier.c:715-721), and the confirmation that
has_been_queried is read NOWHERE (grep) are all captured. An implementer with
only this PRP + repo access can apply the two comment edits with no further
research.

### Documentation & References

```yaml
# MUST READ — the issue this resolves + the recommended fix
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/prd_snapshot.md   (also bugfix PRD)
  section: "### Issue 4: has_been_queried is written but never read (h3.3)"
  why: "States the variable is set on first QUERY_INFO but never read; PRD only requires
        the firmware SET it (host enforces 'at most once per board boot'); recommends
        documenting it as intentionally write-only OR exposing via QUERY_INFO."
  critical: "The item RESEARCH NOTE + architecture findings RECOMMEND documentation, NOT
        exposure: 'Exposing via QUERY_INFO would change the 4-byte response payload to 5
        bytes, risking host compatibility — NOT recommended for a Minor issue.' This task
        IS the documentation option."

# MUST READ — the exact §4.6 rationale to cite/quote in the comment
- file: PRD.md   (the merged PRD)
  section: "### 4.6 ... Handshake timing & has_been_queried (correctness)"  (lines 466-475)
  why: "The authoritative rationale: 'The firmware therefore sets a has_been_queried bool
        on the first QUERY_INFO it services, and the host handshakes AT MOST ONCE PER BOARD
        BOOT (never on a mere HID re-enumeration/reconnect), so a mid-session reconnect
        against legacy firmware cannot clear an active board layer.' This is why the firmware
        only SETS it and never READS it."
  critical: "The comment must convey: (1) firmware SETS it; (2) HOST enforces at-most-once;
        (3) therefore a firmware read site is dead state by design."

# MUST READ — the wire-compat constraint (why NOT to expose it)
- file: notifier.c
  section: "QUERY_INFO case body, lines 710-721"
  why: "The QUERY_INFO reply is built as `uint8_t payload[4]` and sent via
        send_typed_response(NOTIFY_CMD_QUERY_INFO, payload, 4). The 4 bytes are:
        [0]=proto_ver, [1]=feature_flags, [2]=callback_count, [3]=board_rules_present.
        Exposing has_been_queried would require a 5th byte => changes the wire response
        size => host-compat risk. This is the concrete reason the comment must state."
  critical: "Do NOT change payload[4] or the send_typed_response(..., 4) call. This task
        is comment-only; the 4-byte layout is the REASON documented in the comment, not
        something to alter."

# The two exact sites to edit (verbatim current text — anchor the edits on THIS)
- file: notifier.c
  section: "line 194 (declaration) + line 714 (assignment)"
  why: "The declaration (in the HOST STATE block) and the assignment (in handle_typed_command's
        NOTIFY_CMD_QUERY_INFO case) are the only two places has_been_queried appears."
  pattern: "Both currently carry a terse one-line trailing comment. Replace each trailing
            comment with the fuller rationale given verbatim in Implementation Tasks. The
            C statement itself (static bool ... = false; / has_been_queried = true;) is
            UNCHANGED."
  gotcha: "Line numbers shifted from the item spec (186/706) to the ACTUAL 194/714 because
           P1.M1.T1.S1's watchdog landed above both sites. Anchor on the EXACT TEXT, not
           line numbers (line numbers will drift again)."

# Confirms write-only (no production read site exists) — the fact being documented
- file: (whole repo)
  why: "`grep -rn has_been_queried *.c *.h qmk_stubs/*.c` returns ONLY notifier.c:194 (decl)
        + notifier.c:714 (assignment). No production read. (test_notifier_host.c:13-15,126-127
        reference it in COMMENTS and already describe it as write-only — do NOT modify the
        test; it is correct supporting context.)"
  critical: "After the edit, re-run that grep and confirm STILL only 2 production hits. The
        edit must not accidentally introduce a read."

# Scope boundary — the parallel task (no notifier.c overlap)
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/P1M2T2S1/PRP.md
  why: "P1.M2.T2.S1 (parallel) modifies run_all_tests.sh + creates test_fidelity_nfa128.c.
        It does NOT touch notifier.c. No file overlap with this task."
  critical: "Do NOT touch run_all_tests.sh or any test_*.c — that is P1.M2.T2.S1's scope.
        This task is notifier.c comments only."

# Scope boundary — the later README sync
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/tasks.json
  section: "P1.M2.T4 (Sync changeset-level documentation)"
  why: "P1.M2.T4.S1 owns the README overview/features broad sweep. THIS task owns the
        in-source notifier.c comment (item DOCS point 5: 'the comment in notifier.c IS
        the doc')."
  critical: "Do NOT touch README.md — that is P1.M2.T1.S1 (already complete) and P1.M2.T4.S1.
        This task is notifier.c only."
```

### Current Codebase tree (relevant slice)

```bash
notifier.c                 # ← MODIFY (2 comment lines: decl at L194, assignment at L714). ONLY file changed.
notifier.h                 # unaffected. DO NOT TOUCH.
pattern_match.{c,h}        # unaffected. DO NOT TOUCH.
qmk_stubs/*                # unaffected. DO NOT TOUCH.
test_notifier_host.c       # already documents has_been_queried as write-only (comments only). DO NOT TOUCH.
run_notifier_stub_tests.sh # unaffected (run as a validation sanity check only). DO NOT TOUCH.
run_all_tests.sh           # unaffected (P1.M2.T2.S1 owns it). DO NOT TOUCH.
README.md                  # unaffected (P1.M2.T1.S1 / P1.M2.T4.S1 scope). DO NOT TOUCH.
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be changed

```bash
notifier.c                 # MODIFIED: decl comment (L194) + assignment comment (L714) replaced with
                           #   the §4.6 write-only rationale. No functional line changed.
# (no other files change)
```

### Known Gotchas of our codebase & Library Quirks

```c
/* CRITICAL: line numbers shifted. The item spec says lines 186 (decl) and 706
 * (assignment); the ACTUAL current lines are 194 and 714 (P1.M1.T1.S1's watchdog
 * landed above both). Anchor edits on the EXACT oldText strings, not line numbers.
 * Re-run `grep -n has_been_queried notifier.c` to locate them before editing. */

/* CRITICAL: this is COMMENT-ONLY. The C statements
 *   `static bool    has_been_queried = false;`   (L194)
 *   `has_been_queried = true;`                   (L714)
 * are UNCHANGED. Only the trailing /* ... */ comment text changes. Do NOT alter
 * the type, the initializer, the assignment, or anything else. */

/* GOTCHA: the declaration sits in the "HOST STATE" block (architecture invariant
 * 21) between host_cb_enabled[] and the per-OS weak accessors. Keep the comment
 * focused on has_been_queried; do not re-document the whole HOST STATE block. */

/* GOTCHA: do NOT introduce a read of has_been_queried. The whole point is that
 * it is write-only. After editing, grep must still show only decl + assignment. */

/* GOTCHA: do NOT enlarge the QUERY_INFO payload. The comment must STATE that the
 * 4-byte reply ([proto_ver][feature_flags][callback_count][board_rules_present])
 * is why the flag is not exposed — but you must NOT change payload[4] or the
 * send_typed_response(..., 4) call. That is out of scope (wire-protocol change). */

/* GOTCHA: keep §4.6 references. The existing comments cite §4.6; the new ones
 * must too (it is the authoritative rationale). Use the PRD's phrasing
 * ("at most once per board boot", "handshake timing"). */

/* GOTCHA: C comment syntax. Multi-line comments use /* ... */ with a leading /*
 * on the first line and a closing */ on its own line. Ensure every opened
 * comment is closed (an unterminated comment breaks the build). */
```

## Implementation Blueprint

### Data models and structure

None. This task edits two C comments.

### Implementation Tasks (ordered by dependencies)

Two independent edits in `notifier.c`. Apply with the `edit` tool, matching the
EXACT oldText (verified current text). Re-run `grep -n has_been_queried notifier.c`
first to confirm the current line numbers (they are 194 and 714 at research time
but may drift if other tasks land first — the oldText anchors are stable).

```yaml
Task 1: REPLACE the declaration comment (notifier.c:194)
  - oldText (EXACT current text — the whole line):
        static bool    has_been_queried = false;                       /* set on first QUERY_INFO service (§4.6 handshake-timing rule) */
  - newText (the statement UNCHANGED; comment expanded to the §4.6 rationale):
        /* has_been_queried — WRITE-ONLY BY DESIGN (PRD §4.6 handshake-timing rule).
         * Set to true on the first QUERY_INFO (0x01) serviced; NEVER read by any
         * code path. The PRD requires only that the firmware SET it — the HOST
         * enforces the "at most once per board boot" handshake semantics itself
         * (§4.6: a mid-session reconnect against legacy firmware must not clear an
         * active board layer), so a read site here would be dead state. The flag is
         * reserved for future firmware-side observability/debugging and is
         * intentionally NOT exposed in the QUERY_INFO reply: that payload is a fixed
         * 4 bytes ([proto_ver][feature_flags][callback_count][board_rules_present]),
         * and adding a 5th byte would change the wire-protocol response size and
         * risk host compatibility. */
        static bool    has_been_queried = false;
  - PRESERVE: the `static bool    has_been_queried = false;` statement verbatim.
  - WHY: this is the primary site a reader/reviewer greps; the full rationale
    belongs here.

Task 2: REPLACE the assignment comment (notifier.c:714)
  - oldText (EXACT current text — the whole line):
            has_been_queried = true;   /* §4.6 handshake-timing: set on first QUERY_INFO service */
  - newText (the assignment UNCHANGED; comment notes write-only + points to decl):
            has_been_queried = true;   /* WRITE-ONLY (§4.6): host enforces at-most-once-per-boot; never read here — see declaration comment. */
  - PRESERVE: the `has_been_queried = true;` statement verbatim; the leading
    whitespace (it is inside a nested switch/case block — keep indentation).
  - WHY: the assignment site should not silently contradict the declaration's
    "write-only" framing; keep it concise and point readers to the decl.

Task 3: VERIFY (no edit) — comment presence, no new read, no functional diff,
        build + full gate still green. (See Validation Loop.)
```

### Implementation Patterns & Key Details

```c
/* PATTERN: anchor on exact text, not line numbers. Line numbers drift across
 *   parallel/landed tasks (186/706 -> 194/714 after the watchdog). The oldText
 *   strings above are the stable anchors. Re-grep to locate them. */

/* PATTERN: comment-only edit keeps the C statement byte-identical. The
 *   declaration statement and the assignment statement are reproduced verbatim
 *   in newText; only the surrounding /* */ comment changes. This makes the diff
 *   trivially reviewable (every changed line is a comment line). */

/* PATTERN: cite §4.6 (the authoritative PRD section) and use the PRD's exact
 *   phrasing ("at most once per board boot", "handshake timing") so the in-source
 *   comment matches the spec a reader will cross-reference. */

/* CRITICAL: the comment must explain BOTH why it is write-only (host enforces
 *   at-most-once) AND why it is not exposed (4-byte QUERY_INFO payload, wire
 *   compat). Issue 4 is fully resolved only when both questions are answered. */

/* ANTI-PATTERN: do NOT change the C statement, the type, the initializer, the
 *   assignment, the payload[4] size, or the send_typed_response(..., 4) call.
 *   This is a comment edit; anything else is scope creep / a wire change. */

/* ANTI-PATTERN: do NOT add a read of has_been_queried "to make it useful".
 *   Reading it would change behavior and is out of scope; the task is to
 *   DOCUMENT that it is intentionally unread. */

/* ANTI-PATTERN: do NOT re-document the entire HOST STATE block or the QUERY_INFO
 *   handler. Edit ONLY the two has_been_queried comments. */

/* ANTI-PATTERN: do NOT touch any other file (test_notifier_host.c already
 *   describes it as write-only; run_*.sh, README.md, notifier.h, etc. are other
 *   tasks' scope). */
```

### Integration Points

```yaml
FILE MODIFIED:
  - notifier.c (repo root). The ONLY file this task touches.
EDITS:
  - L194 declaration: trailing comment -> multi-line §4.6 write-only rationale.
  - L714 assignment: trailing comment -> concise write-only note + pointer to decl.
PRESERVED (unchanged):
  - The `static bool has_been_queried = false;` statement.
  - The `has_been_queried = true;` statement.
  - The QUERY_INFO `uint8_t payload[4]` + send_typed_response(..., 4) (the 4-byte reply).
  - Every other line of notifier.c.
CONSUMES (context only, unchanged):
  - PRD §4.6 (PRD.md:466-475): the handshake-timing / at-most-once rationale.
  - notifier.c:715-721: the 4-byte QUERY_INFO payload layout.
BUILD / WIRE / CONFIG / DATABASE / ROUTES:
  - none. No build-system, wire-protocol, or config change.
```

## Validation Loop

> This is a comment-only change — no logic can break. Validation confirms (1) the
> comment text is present and accurate, (2) no functional line changed and no read
> was introduced, (3) the build still compiles (valid C comment syntax), and (4)
> the full test gate remains green. All commands run from the repo root.

### Level 1: Comment presence & accuracy (the primary gate)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Locate the two sites (confirm they still exist; note current line numbers).
grep -n 'has_been_queried' notifier.c
# Expected: exactly 2 hits — the declaration (research-time L194) and the
# assignment inside NOTIFY_CMD_QUERY_INFO (research-time L714).

# 1b. The declaration comment carries the §4.6 write-only rationale + the
#     wire-compat reason (each phrase should match once near the declaration).
for phrase in 'WRITE-ONLY' '§4.6' 'at most once per board boot' 'NEVER read' \
              'reserved for future' 'NOT exposed' 'payload is a fixed' \
              '4 bytes' 'risk host compatibility'; do
  # search the declaration region (the comment block immediately above the decl)
  awk '/has_been_queried = false/{exit} {print}' notifier.c | tail -12 | grep -qF "$phrase" \
    && echo "decl-comment present: $phrase" || echo "MISSING decl-comment: $phrase"
done
# Expected: every line prints "present:".

# 1c. The assignment comment notes write-only + points to the declaration.
grep -n 'has_been_queried = true;' notifier.c | head -1
# Inspect that line; expect a comment containing "WRITE-ONLY", "§4.6", and a
# pointer like "see declaration comment" or "See declaration".

# 1d. §4.6 is still referenced (the existing references are preserved/expanded).
grep -c '§4.6' notifier.c   # expect >= the prior count (the 2 comments both cite it)
```

### Level 2: No functional change / no new read (the safety gate)

```bash
cd /home/dustin/projects/qmk-notifier

# 2a. has_been_queried is STILL only declared + assigned — no read introduced.
grep -rn 'has_been_queried' *.c *.h qmk_stubs/*.c
# Expected: exactly 2 production hits — notifier.c:<decl> and notifier.c:<assignment>.
# (test_notifier_host.c may appear in COMMENTS only — that is pre-existing, fine.)
# FAIL if any new line READS it (e.g. `if (has_been_queried)`, `return has_been_queried`).

# 2b. git diff shows ONLY comment lines changed (no functional statement touched).
git diff -- notifier.c
# Expected: every `-`/`+` line is inside a /* ... */ comment OR is the unchanged
# `static bool    has_been_queried = false;` / `has_been_queried = true;` line
# (moved because the comment above it changed length). NO other statement changed.
git diff -- notifier.c | grep -E '^[+-]' | grep -vE '^[+-]\s*/?\*|^[+-]\s*\*|^[+-]\s*$|has_been_queried = (false|true);'
# Expected: NO output (every changed line is a comment line or the unchanged
# statement). Any output here is an accidental functional change — revert it.

# 2c. The 4-byte QUERY_INFO reply is UNCHANGED (the documented reason, not altered).
grep -n 'uint8_t payload\[4\];' notifier.c | head -1   # still present
grep -n 'send_typed_response(NOTIFY_CMD_QUERY_INFO, payload, 4)' notifier.c   # still 4
# Expected: both present; the payload[4] and the `..., 4)` are byte-identical.
```

### Level 3: Build & full gate remain green (sanity — comments don't change behavior)

```bash
cd /home/dustin/projects/qmk-notifier

# 3a. notifier.c still compiles clean under the stub harness (valid C comment syntax).
./run_notifier_stub_tests.sh 2>/dev/null | grep -E '^\[|fails=|PASSED|FAILED|warning:'
# Expected: [1/4]..[4/4] all succeed; "notifier dispatch fails=0", "notifier os fails=0",
#           "notifier host fails=0" (or whatever the runner prints), "✓ notifier stub-compile
#           gate PASSED"; NO 'warning:' lines from the notifier.c compile.
# (If the compile step shows a 'warning:' or error, the comment syntax is broken — fix it.)

# 3b. The pattern-match corpus is unaffected (run as a regression sanity check).
./run_all_tests.sh >/tmp/ra.out 2>&1; echo "run_all_tests exit=$?  (expect 0)"
grep -E 'Total tests failed across all suites|ALL TESTS PASSED|SOME TESTS FAILED' /tmp/ra.out
# Expected: "Total tests failed: 0" (or the runner's equivalent) + "✓ ALL TESTS PASSED".
rm -f /tmp/ra.out
```

### Level 4: Diff hygiene (ONLY notifier.c changed)

```bash
cd /home/dustin/projects/qmk-notifier

git status --porcelain
# Expected: ` M notifier.c` and `?? plan/003_.../P1M2T3S1/`. NOTHING else.
# (notifier.h, pattern_match.*, qmk_stubs/*, test_*.c, run_*.sh, README.md,
#  PRD.md, tasks.json, prd_snapshot.md, .gitignore all untouched.)

# Confirm no other source file changed.
git status --porcelain | grep -vE '^\?\? plan/|^ M notifier.c$' && echo "ERROR: unexpected changes" || echo "scope clean: only notifier.c"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: both `has_been_queried` sites carry the §4.6 write-only rationale;
      the decl comment includes all 9 phrases (WRITE-ONLY, §4.6, at-most-once,
      NEVER read, reserved for future, NOT exposed, fixed 4 bytes, risk host
      compat); the assignment comment points to the decl.
- [ ] Level 2: `grep -rn has_been_queried *.c *.h qmk_stubs/*.c` → still exactly
      decl + assignment (NO new read); `git diff notifier.c` changes ONLY comment
      lines; the QUERY_INFO `payload[4]` + `send_typed_response(..., 4)` unchanged.
- [ ] Level 3: `./run_notifier_stub_tests.sh` compiles notifier.c clean (0
      warnings) + gate PASSED; `./run_all_tests.sh` → 0 failures, exit 0.
- [ ] Level 4: `git status` shows only ` M notifier.c` + plan/.

### Feature Validation

- [ ] The declaration (L194) comment answers all of Issue 4's open questions:
      write-only by design; §4.6 host-enforced at-most-once; reserved for future
      observability; NOT exposed (4-byte reply, wire compat).
- [ ] The assignment (L714) comment is consistent (write-only) and points to decl.
- [ ] No behavior change (the flag is still set on first QUERY_INFO, still unread).

### Code Quality Validation

- [ ] The C statements (`= false;` / `= true;`) are byte-identical to before.
- [ ] Comments are valid C (every `/*` closed with `*/`); no build warning.
- [ ] §4.6 references preserved/expanded (matches the PRD's phrasing).
- [ ] No anti-patterns (see below): no functional change, no read introduced, no
      payload enlargement, no edits outside the two comments.

### Documentation & Deployment

- [ ] The in-source comment IS the documentation (item DOCS point 5) — complete.
- [ ] No README change (that is P1.M2.T4.S1's scope).
- [ ] No new env vars / config / build-system / wire changes.

---

## Anti-Patterns to Avoid

- ❌ Don't change any functional line — the `static bool has_been_queried = false;`
  declaration, the `has_been_queried = true;` assignment, the `uint8_t payload[4]`,
  and `send_typed_response(..., 4)` are ALL unchanged. Only comment text changes.
- ❌ Don't introduce a read of `has_been_queried`. The whole point is that it is
  intentionally write-only. After the edit, grep must still show only decl + assignment.
- ❌ Don't expose `has_been_queried` in the QUERY_INFO reply (no 5th payload byte).
  The comment must explain this is deliberately NOT done (wire-compat); do not do it.
- ❌ Don't anchor on line numbers — they drifted (186/706 → 194/714) and will drift
  again. Anchor on the EXACT oldText strings in Implementation Tasks; re-grep first.
- ❌ Don't edit any file other than `notifier.c`. test_notifier_host.c already
  documents the variable correctly; run_*.sh, README.md, notifier.h, etc. are other
  tasks' scope.
- ❌ Don't re-document the whole HOST STATE block or QUERY_INFO handler — edit ONLY
  the two `has_been_queried` comments.
- ❌ Don't drop the §4.6 reference or the "at most once per board boot" phrasing —
  they are the authoritative rationale and match the PRD a reader will cross-check.
- ❌ Don't leave an unterminated `/* */` comment — it breaks the build (Level 3
  catches it, but check before committing).

---

## Confidence Score: 10/10

The deliverable is a two-line comment replacement in `notifier.c` (declaration at
the current line 194, assignment at 714 — both verified by reading the file; note
the item spec's 186/706 are stale post-watchdog). The exact oldText anchors (the
verbatim current trailing comments) and the exact replacement text (a §4.6-cited,
multi-line write-only rationale for the declaration; a concise pointer for the
assignment) are given verbatim in Implementation Tasks. The authoritative PRD §4.6
rationale is quoted (PRD.md:466-475: "the host handshakes at most once per board
boot"), and the concrete wire-compat constraint is confirmed by reading the
QUERY_INFO handler (notifier.c:715-721: `uint8_t payload[4]` +
`send_typed_response(..., 4)`). The grep confirming `has_been_queried` is read
NOWHERE (only decl + assignment) was run during research. The task is
comment-only, so risk is NONE; the validation confirms no functional line changes,
no read is introduced, the 4-byte reply is untouched, and the build + full gate
stay green. No conflict with the parallel P1.M2.T2.S1 (run_all_tests.sh + new
test file — no notifier.c overlap) or the later P1.M2.T4.S1 (README). No external
dependencies; no code/build/wire change beyond the two comments.