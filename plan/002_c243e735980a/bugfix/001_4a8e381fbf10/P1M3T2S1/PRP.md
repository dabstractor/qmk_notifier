name: "P1.M3.T2.S1 — Update README.md test counts and cross-cutting documentation"
description: >
  Mode B (changeset-level docs sync). Update exactly four numeric references in
  README.md so the documented test counts match the ACTUAL output of
  `./run_all_tests.sh` and `./run_notifier_stub_tests.sh` after the
  P1.M1.T1.S1 (+4 @-literal cases) and P1.M2.T2.S1 (+3 embedded-NUL dispatch
  cases) changes land. README-only. No code, no PRD, no header JSDoc. Verified
  empirically: aggregate 2019→2023, test_pattern_match 376→380, dispatch 11→14.

---

## Goal

**Feature Goal**: Synchronize README.md's three test-count surfaces — the
"Comprehensive Test Suite" per-suite table, the "test_notifier_dispatch" case
bullet, and the "Current Test Status" overall + notifier-gate tallies — with the
actual post-fix output of the two test runners. No stale count may remain.

**Deliverable**: ONE file modified — `README.md` (repo root). Exactly four
numeric edits (optionally a fifth cosmetic Covers-column word). No other file is
touched.

**Success Definition**:
- `grep -nE '376|2019\b' README.md` → **zero matches** (both stale numbers gone).
- `grep -nE '11 cases|11/11' README.md` → **zero matches** (the dispatch stale
  counts gone).
- Every count in README.md that can be checked against a runner output now MATCHES:
  - "Comprehensive Test Suite" table row `test_pattern_match` → **380**
    (= `./test_pattern_match`'s `Total tests run: 380`).
  - `test_notifier_dispatch` bullet → **(14 cases)** (matches
    `./run_notifier_stub_tests.sh`'s `Total tests run: 14 / passed: 14`).
  - "Current Test Status" pattern-match corpus line → **2023/2023** (matches
    run_all_tests.sh's `Total tests run across all suites: 2023`).
  - "Current Test Status" notifier-gate line → `test_notifier_dispatch`
    **14/14** + `test_notifier_os` **31/31** (matches runner's
    `notifier dispatch fails=0` + `notifier os fails=0`).
- `./run_all_tests.sh` and `./run_notifier_stub_tests.sh` are UNCHANGED and still
  exit 0 / report 0 failures (README-only edit cannot affect them; this is a
  consistency re-check, not a gate).
- No edits to `PRD.md`, `pattern_match.h`, any `*.c`/`*.sh`/`*.h`, `tasks.json`,
  `prd_snapshot.md`, `rules.mk`, `.gitignore`. Only `README.md`.

## User Persona (if applicable)

**Target User**: Any reader of the GitHub README (contributors, downstream
keymap authors) who wants to know how thoroughly the matcher/dispatcher is
tested and what the current passing-test counts are.

**Use Case**: A contributor reads the "Comprehensive Test Suite" table and
"Current Test Status" section to gauge confidence before relying on the module;
the numbers must reflect reality (what `run_all_tests.sh` actually prints), not a
pre-fix snapshot.

**User Journey**: reader opens README → sees the per-suite table + status block →
the counts are internally consistent with each other AND with the runner output
they can reproduce by cloning + running the scripts.

**Pain Points Addressed**: Stale documentation. After Issues 1 & 2 landed new
test cases, the README still advertised the old counts (376 / 11 / 2019), which
would mislead readers and look neglected. This sync removes the drift.

## Why

- **Single source of truth.** The README is the public face of the repo; its
  test counts must agree with `run_all_tests.sh` / `run_notifier_stub_tests.sh`.
  This task is the changeset-level (Mode B) docs sync that the three test-adding
  PRPs explicitly deferred: P1.M1.T1.S1 ("README test-count sync is a SEPARATE
  task (P1.M3.T2.S1)"), P1.M2.T2.S1 ("README test-count sync is a separate doc
  task — out of scope"), and P1.M3.T1.S1 ("owned by P1.M3.T2.S1 — do NOT do it
  here").
- **Zero risk.** A Markdown-only edit cannot change build behavior, test
  outcomes, the matcher, the dispatcher, or the wire protocol. The only effect is
  on human readers.
- **Closes the changeset's documentation loop.** Per the item contract (DOCS §5):
  "This IS the docs sync task (Mode B). All cross-cutting documentation changes
  for this changeset are handled here."

## What

Four mandatory numeric edits in `README.md`. Each is a unique, grep-findable
string. All counts below were captured by RUNNING the gates (see "Current
Codebase tree" / Validation) — they are not estimated.

**(M1) "Comprehensive Test Suite" table — `test_pattern_match` row count: 376 → 380**
```diff
-| `test_pattern_match` | 376 | anchors, escapes, wildcards, case sensitivity, parsing, edge cases, metachars |
+| `test_pattern_match` | 380 | anchors, escapes, wildcards, case sensitivity, parsing, edge cases, metachars, @-literal regression guards |
```
(The count `376 → 380` is mandatory. Appending `, @-literal regression guards` to
the Covers column is a recommended, accurate polish that documents the 4 new
rows from P1.M1.T1.S1 — keep it unless your reviewer prefers a count-only edit.)

**(M2) `test_notifier_dispatch` bullet — case count: 11 → 14**
```diff
-- **`test_notifier_dispatch`** (11 cases) — F4 delimiter matching, dispatcher
+- **`test_notifier_dispatch`** (14 cases) — F4 delimiter matching, dispatcher
```
(The description already says "sanitization", which covers the new embedded-NUL
case — no wording change needed beyond the count.)

**(M3) "Current Test Status" — pattern-match corpus aggregate: 2019 → 2023**
```diff
-- Pattern-match corpus (`./run_all_tests.sh`, 9 suites): **2019/2019** tests passing.
+- Pattern-match corpus (`./run_all_tests.sh`, 9 suites): **2023/2023** tests passing.
```

**(M4) "Current Test Status" — notifier-gate dispatch tally: 11/11 → 14/14**
```diff
-- Notifier stub gate (`./run_notifier_stub_tests.sh`): `test_notifier_dispatch`
-  **11/11** + `test_notifier_os` **31/31** cases passing.
+- Notifier stub gate (`./run_notifier_stub_tests.sh`): `test_notifier_dispatch`
+  **14/14** + `test_notifier_os` **31/31** cases passing.
```
(`test_notifier_os` stays **31/31** — P1.M2.T2.S1 added no OS-suite cases.)

### Contract point (d) keyword review — ALREADY SATISFIED (no edits)

The item contract (LOGIC §d) asks to review the "Running Tests" and "Current Test
Status" sections for references to `sanitize_string`, `CONSOLE_ENABLE` debug, or
`@-literal` matching. **Empirically verified (grep during research): NONE of these
terms appear anywhere in README.md.** The new behavior is already reflected by the
count changes (M1–M4). Do NOT invent new prose about NUL-stripping or
CONSOLE_ENABLE — the contract's OUTPUT §4 ("No stale counts remain") is about
counts, and point (e) forbids touching PRD/header docs. If you feel prose is
warranted, it is OUT OF SCOPE for this task.

### Success Criteria

- [ ] M1 applied: `test_pattern_match` table count = 380.
- [ ] M2 applied: `test_notifier_dispatch` bullet = (14 cases).
- [ ] M3 applied: "Current Test Status" pattern-match corpus = 2023/2023.
- [ ] M4 applied: "Current Test Status" notifier-gate dispatch = 14/14; os = 31/31.
- [ ] `grep -nE '\b376\b|\b2019\b|11 cases|11/11' README.md` returns nothing.
- [ ] The two runner scripts are byte-for-byte unchanged and still exit 0.
- [ ] No file other than `README.md` is modified.

## All Needed Context

### Context Completeness Check

**Pass.** This PRP gives (a) the EXACT old→new text of every edit (copy-paste
ready), (b) the empirically verified counts (captured by running both runners
during research), (c) the grep verification commands, and (d) the list of files
that must NOT change. An implementer who has never seen this repo can apply the
four edits with a single multi-edit call and verify with the one grep above.

### Documentation & References

```yaml
# MUST READ — the file being edited (the ONLY file this task touches)
- file: README.md
  section: "### Comprehensive Test Suite" (table, ~lines 322-334) AND
            "- **`test_notifier_dispatch`** (11 cases)" bullet (~line 349) AND
            "### Current Test Status" -> "Overall Test Results" (~lines 369-372)
  why: "Contains all four stale counts to update. Anchored on TEXT (the exact
        table row / bullet / status lines), NOT line numbers — README line
        numbers shift if any earlier edit lands first."
  pattern: "Pure Markdown: keep the table pipe alignment, the bold (**...**),
            and the backtick-fenced code spans exactly as the neighbors use them."
  gotcha: "Line numbers in this PRP (~320-372) were accurate at research time but
           the parallel P1.M3.T1.S1 task does NOT touch README, so they should be
           stable — STILL anchor edits on the unique text strings, not numbers."

# MUST READ — the authoritative count sources (run these; copy their numbers)
- file: run_all_tests.sh
  section: "OVERALL TEST SUMMARY -> 'Total tests run across all suites: N'"
  why: "The aggregate N is what 'Current Test Status' cites as 'NNNN/NNNN tests
        passing'. Empirically N=2023 now (was 2019; +4 from P1.M1.T1.S1)."
  critical: "The aggregate sums only the 7 suites that print 'Total tests run:' /
        'Tests run:'. test_metachar_verification (smoke) and
        test_comprehensive_integration (categories) are NOT in the running total —
        so the 9-row table's numeric cells sum to ~2033 while the aggregate is
        2023. This is INTENTIONAL and pre-existing; do NOT 'reconcile' it."

- file: run_notifier_stub_tests.sh
  section: "links test_notifier_dispatch + test_notifier_os, runs both"
  why: "test_notifier_dispatch prints 'Total tests run: 14 / passed: 14 / failed:
        0' (was 11; +3 from P1.M2.T2.S1). test_notifier_os is 31/31 (unchanged)."
  critical: "Do NOT edit this script. Run it only to read the counts."

# MUST READ — why the counts changed (the PRPs that deferred THIS docs task)
- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/P1M1T1S1/PRP.md
  section: "## Goal" + "Success Definition"
  why: "Added 4 @-literal regression rows to test_pattern_match.c -> 376 -> 380.
        Its DOCS section explicitly defers the README count sync to THIS task."
  critical: "pattern_match.c is UNTOUCHED by this changeset; only the .c TEST
        file grew. The README count update reflects that test growth."

- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/P1M2T2S1/PRP.md
  section: "## Goal" + "Success Definition"
  why: "Added 3 dispatch assertions to test_notifier_dispatch.c -> 11 -> 14 (1
        nul_cmd_fired callback check + 2 ck() discrimination calls). Its DOCS
        section explicitly defers the README count sync to THIS task."

# MUST READ — parallel task boundary (no overlap, no conflict)
- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/P1M3T1S1/PRP.md
  section: "## Goal"
  why: "P1.M3.T1.S1 modifies notifier.c + creates qmk_stubs/print.h ONLY. It does
        NOT change any test count (its success gate is a clean -DCONSOLE_ENABLE
        compile, not new assertions) and does NOT touch README.md. Zero overlap
        with this docs task."
  critical: "If P1.M3.T1.S1 has landed, README line numbers are STILL ~the same
        (that task touches notifier.c / print.h, not README). Anchor on text."

# CONTRACT — what NOT to touch (item contract point e)
- file: PRD.md
  why: "Human-owned. The §11.2C doc fix was already applied at commit 4d49460.
        This task must NOT edit PRD.md."
- file: pattern_match.h
  why: "No API change in this changeset; the JSDoc is correct as-is. Do NOT edit."
```

### Current Codebase tree (relevant slice — POST all test changes)

```bash
README.md                   # ← MODIFY (this task): 4 count edits. The ONLY file changed.
test_pattern_match.c        # LANDED (P1.M1.T1.S1): +4 rows -> binary reports 380. DO NOT TOUCH.
test_notifier_dispatch.c    # LANDED (P1.M2.T2.S1): +3 cases -> binary reports 14. DO NOT TOUCH.
notifier.c                  # LANDED (P1.M2.T1.S1 + being edited by P1.M3.T1.S1): sanitize fix +
                            #   optional CONSOLE print. DO NOT TOUCH (this is a docs task).
qmk_stubs/print.h           # (being created by P1.M3.T1.S1). DO NOT TOUCH.
run_all_tests.sh            # authoritative aggregate source. READ-ONLY (run it; do not edit).
run_notifier_stub_tests.sh  # authoritative dispatch/os source. READ-ONLY.
PRD.md                      # human-owned. DO NOT TOUCH.
pattern_match.{c,h}         # unaffected by docs task. DO NOT TOUCH.
```

### Desired Codebase tree with files to be changed

```bash
README.md   # MODIFIED: 4 count updates (test_pattern_match 376->380; dispatch 11->14
            #   cases; aggregate 2019->2023; dispatch 11/11->14/14). Optionally a 5th
            #   cosmetic Covers-column word. No other file changes.
```

### Known Gotchas of our codebase & Library Quirks

```markdown
<!-- CRITICAL: anchor every edit on the UNIQUE TEXT, not line numbers. README
     line numbers (~320-372 at research time) are stable because the parallel
     P1.M3.T1.S1 task does not touch README, but text anchoring is robust to
     any future reflow. -->

<!-- CRITICAL: the aggregate (2023) is NOT the sum of all 9 table rows. The
     table lists per-suite counts including test_metachar_verification (smoke,
     not counted) and test_comprehensive_integration (10 CATEGORIES, not in the
     script's running total). run_all_tests.sh's counter only adds suites that
     print "Total tests run:" or "Tests run:" (7 of the 9). This 9-row-vs-
     2023-aggregate gap is INTENTIONAL and pre-existed this changeset (old table
     numerics summed to ~2029 while the README cited 2019 — same structure).
     DO NOT try to "fix" the table to sum to 2023. Only update numbers that
     CHANGED: 376->380, 11->14, 2019->2023, 11/11->14/14. -->

<!-- GOTCHA: test_notifier_os stays 31/31. P1.M2.T2.S1 added dispatch cases only.
     Do NOT change 31/31. -->

<!-- GOTCHA: the parallel P1.M3.T1.S1 (CONSOLE_ENABLE print) does NOT add test
     cases — its gate is a clean compile, not assertions. So NO count in README
     reflects it, and it is correctly absent from the test tables. Do not invent
     a row for CONSOLE_ENABLE. -->

<!-- GOTCHA: contract point (d) keywords (sanitize_string, CONSOLE_ENABLE,
     @-literal) appear NOWHERE in README.md (verified by grep). Do NOT add prose
     about them. The count changes are the entire deliverable. -->

<!-- GOTCHA: keep Markdown formatting exact — the table's pipe `|` columns, the
     `**bold**` for NNNN/NNNN, and the backticks around `test_notifier_dispatch`
     must match the surrounding style. A stray space breaks the table render. -->

<!-- GOTCHA: the count "9 host-side suites" / "9 pattern-matcher suites" /
     "9 suites" is STILL correct (no suite was added or removed — only cases
     within existing suites changed). Do NOT change the 9s to anything else. -->
```

## Implementation Blueprint

### Data models and structure

**None.** This is a Markdown documentation edit. No code, no types, no config.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: VERIFY the current counts (read-only, no edits)
  - RUN: ./run_all_tests.sh 2>&1 | grep -E 'Total tests run across all suites'
    EXPECT: "Total tests run across all suites: 2023"
  - RUN: ./run_notifier_stub_tests.sh 2>&1 | grep -E 'Total tests run'
    EXPECT (two lines):
      "Total tests run: 14 / passed: 14 / failed: 0"   (dispatch)
      "Total tests run: 31 / passed: 31 / failed: 0"   (os)
  - RUN (per-suite, confirms the table): for each suite run its binary and read
    its summary; test_pattern_match MUST print "Total tests run: 380".
  - WHY: lock the exact numbers BEFORE editing, so the edits are empirically
    grounded (not transcribed from this PRP). If any number differs from this
    PRP, TRUST THE LIVE OUTPUT and adjust the edit accordingly.

Task 2: EDIT README.md — apply the 4 (optionally 5) edits in ONE multi-edit call
  - EDIT M1 (table row): oldText unique string ->
        | `test_pattern_match` | 376 | anchors, escapes, wildcards, case sensitivity, parsing, edge cases, metachars |
    newText ->
        | `test_pattern_match` | 380 | anchors, escapes, wildcards, case sensitivity, parsing, edge cases, metachars, @-literal regression guards |
    (If you prefer count-only, drop ", @-literal regression guards" from newText.)
  - EDIT M2 (dispatch bullet): oldText ->
        - **`test_notifier_dispatch`** (11 cases) — F4 delimiter matching, dispatcher
    newText ->
        - **`test_notifier_dispatch`** (14 cases) — F4 delimiter matching, dispatcher
  - EDIT M3 (aggregate): oldText ->
        - Pattern-match corpus (`./run_all_tests.sh`, 9 suites): **2019/2019** tests passing.
    newText ->
        - Pattern-match corpus (`./run_all_tests.sh`, 9 suites): **2023/2023** tests passing.
  - EDIT M4 (notifier-gate tally): oldText (2 lines) ->
        - Notifier stub gate (`./run_notifier_stub_tests.sh`): `test_notifier_dispatch`
          **11/11** + `test_notifier_os` **31/31** cases passing.
    newText (2 lines) ->
        - Notifier stub gate (`./run_notifier_stub_tests.sh`): `test_notifier_dispatch`
          **14/14** + `test_notifier_os` **31/31** cases passing.
  - CONSTRAINT: each oldText must be unique in README.md (they are — verified).
    Do NOT include large surrounding regions; each edit is a single line (M4 spans
    two adjacent lines because the **11/11** sits on the wrapped continuation).
  - PRESERVE: the table header, all other table rows, all prose, the "9 host-side
    suites" / "9 suites" wording, the `test_notifier_os` 31/31, the Performance
    Impact line, and everything outside these four locations.
  - DO NOT: edit any file other than README.md.

Task 3: VERIFY the edits
  - RUN: grep -nE '\b376\b|\b2019\b|11 cases|11/11' README.md
    EXPECT: no output (all stale counts gone).
  - RUN: grep -nE '380|2023/2023|14 cases|14/14' README.md
    EXPECT: 4 matches (one per edit) — confirm they are at the intended spots.
  - RUN (optional): diff the rendered table mentally — pipe alignment intact.
```

### Implementation Patterns & Key Details

```markdown
<!-- PATTERN: one multi-edit call with 4 entries. Each oldText is a single,
     unique line (M4 is two adjacent lines). This matches the project's "use one
     edit call with multiple entries for multiple separate locations" rule. -->

<!-- PATTERN: trust the runner over the PRP. Task 1 reads the live counts first;
     if (e.g.) a future changeset bumps test_pattern_match again before this
     task runs, the live number wins. Transcribe LIVE OUTPUT into the edits. -->

<!-- ANTI-PATTERN: do NOT "reconcile" the 9-row table to sum to the 2023
     aggregate. The aggregate excludes 2 suites by design (see Known Gotchas). -->

<!-- ANTI-PATTERN: do NOT add prose about sanitize_string / CONSOLE_ENABLE /
     @-literal. Contract point (d) verified those keywords are absent from
     README; point (e) forbids PRD/header edits. The deliverable is counts only. -->

<!-- ANTI-PATTERN: do NOT touch run_all_tests.sh or run_notifier_stub_tests.sh.
     They are the SOURCE of the counts you are transcribing; editing them would
     be circular and out of scope. -->

<!-- ANTI-PATTERN: do NOT round or pretty-print the aggregate. It is an exact
     integer from the script (2023), presented as "2023/2023". -->
```

### Integration Points

```yaml
NO database / config / route / build / migration changes. This is a pure
Markdown documentation edit. The only "integration" is that the README numbers
now agree with the two runner scripts' output.

DEPENDENCY (consumed, already complete):
  - P1.M1.T1.S1: test_pattern_match.c grew 376 -> 380 (LANDED, verified).
  - P1.M2.T2.S1: test_notifier_dispatch.c grew 11 -> 14 (LANDED, verified).
  - P1.M2.T1.S1: sanitize_string fix (LANDED) — enables the +3 dispatch cases.

PARALLEL (no overlap):
  - P1.M3.T1.S1: edits notifier.c + qmk_stubs/print.h. Does NOT touch README
    and does NOT change any test count. Safe to land in any order relative to
    this task.
```

## Validation Loop

> Toolchain: no compiler here — this is a Markdown edit. Validation = grep checks
> + re-running the gates to confirm the README numbers match live output. The
> README edit itself cannot affect the gates (it is not compiled/linked).

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# Confirm no stale counts remain anywhere in README.
grep -nE '\b376\b|\b2019\b|11 cases|11/11' README.md
# Expected: NO output (all four stale tokens are gone).

# Confirm the new counts are present.
grep -nE '\b380\b|2023/2023|14 cases|14/14' README.md
# Expected: 4 matching lines, each at an intended location:
#   - table row (380)
#   - dispatch bullet (14 cases)
#   - Current Test Status corpus line (2023/2023)
#   - Current Test Status notifier-gate line (14/14)

# Confirm the Markdown table still parses (pipe count on the header + rows is
# consistent — quick sanity, not a strict test):
awk '/^\| Suite \| Count \|/,/^\| `test_invalid_patterns`/' README.md | head
# Expected: header + 9 data rows, each with 3 pipes (4 columns). No row merged.
```

### Level 2: "Unit Test" (the count transcriptions match live runner output)

```bash
cd /home/dustin/projects/qmk-notifier

# 2a. README aggregate == run_all_tests.sh aggregate.
AGG=$(./run_all_tests.sh 2>&1 | grep 'Total tests run across all suites' | grep -oE '[0-9]+' | tail -1)
grep -q "**${AGG}/${AGG}** tests passing" README.md && echo "✓ aggregate matches ($AGG)" \
  || echo "MISMATCH: README != $AGG"
# Expected: "✓ aggregate matches (2023)".

# 2b. README dispatch case count == runner dispatch summary.
DISP=$(./run_notifier_stub_tests.sh 2>&1 | grep -m1 'Total tests run' | grep -oE 'run: [0-9]+' | grep -oE '[0-9]+')
grep -q "test_notifier_dispatch\*\* (${DISP} cases)" README.md && echo "✓ dispatch bullet matches ($DISP)" \
  || echo "MISMATCH (bullet) != $DISP"
grep -q "**${DISP}/${DISP}** + \`test_notifier_os\`" README.md && echo "✓ dispatch tally matches ($DISP/$DISP)" \
  || echo "MISMATCH (tally) != $DISP/$DISP"
# Expected: "✓ dispatch bullet matches (14)" and "✓ dispatch tally matches (14/14)".

# 2c. README test_pattern_match table count == binary's own count.
TPM=$(gcc -o /tmp/tpm test_pattern_match.c pattern_match.c && /tmp/tpm 2>&1 | grep 'Total tests run:' | grep -oE '[0-9]+' | head -1); rm -f /tmp/tpm
grep -q "| \`test_pattern_match\` | ${TPM} |" README.md && echo "✓ table count matches ($TPM)" \
  || echo "MISMATCH (table) != $TPM"
# Expected: "✓ table count matches (380)".
```

### Level 3: Integration (the two gates still pass — README edit cannot break them)

```bash
cd /home/dustin/projects/qmk-notifier

# Re-run both gates purely as a consistency re-check (a README edit does not
# affect them; this guards against an accidental edit to a script).
./run_all_tests.sh >/dev/null 2>&1; echo "run_all_tests exit=$?  (expect 0)"
./run_notifier_stub_tests.sh 2>&1 | grep -E 'fails=|PASSED|FAILED'
# Expected:
#   run_all_tests exit=0
#   notifier dispatch fails=0  (exit=0)
#   notifier os fails=0        (exit=0)
#   ✓ notifier stub-compile gate PASSED
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Scope guard: ONLY README.md changed in source (plan/ artifacts excluded).
git status --porcelain | grep -vE '^\?\? plan/' | grep -vE 'M README.md' || true
# Expected: empty (nothing outside README.md and plan/ changed).

# 4b. Forbidden-files guard: PRD.md and pattern_match.h untouched.
git diff --stat -- PRD.md pattern_match.h pattern_match.c notifier.c notifier.h \
                   run_all_tests.sh run_notifier_stub_tests.sh '*.c' rules.mk .gitignore
# Expected: empty (no changes to any forbidden file).

# 4c. Contract point (d) re-check: none of the flagged keywords leaked into README
#     as new prose (they were absent before; they must remain absent — we only
#     changed counts).
grep -nEi 'sanitize_string|CONSOLE_ENABLE' README.md
# Expected: NO output (we did not add such prose).

# 4d. Read the two edited sections end-to-end for rendering sanity.
sed -n '/### Comprehensive Test Suite/,/### Current Test Status/p' README.md
sed -n '/### Current Test Status/,/## Contributing/p' README.md | head -40
# Expected: coherent, internally consistent numbers; table renders; bolding intact.
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `grep -nE '\b376\b|\b2019\b|11 cases|11/11' README.md` → empty.
- [ ] Level 1: `grep -nE '\b380\b|2023/2023|14 cases|14/14' README.md` → 4 intended matches.
- [ ] Level 1: table still renders (header + 9 rows, 4 columns each).
- [ ] Level 2: README aggregate == `run_all_tests.sh` aggregate (2023).
- [ ] Level 2: README dispatch bullet + tally == runner dispatch summary (14).
- [ ] Level 2: README `test_pattern_match` table count == binary output (380).
- [ ] Level 3: both runners still exit 0 / 0 failures (README edit didn't touch them).

### Feature Validation

- [ ] M1: `test_pattern_match` table row = 380 (optionally + "@-literal regression guards" in Covers).
- [ ] M2: `test_notifier_dispatch` bullet = (14 cases).
- [ ] M3: "Current Test Status" pattern-match corpus = 2023/2023.
- [ ] M4: "Current Test Status" notifier gate = dispatch 14/14 + os 31/31.
- [ ] No stale count of 376, 2019, 11 cases, or 11/11 remains in README.

### Code Quality Validation

- [ ] Markdown formatting preserved (pipes, bold, backticks match neighbors).
- [ ] "9 host-side suites" / "9 suites" / "9 pattern-matcher suites" left as 9 (correct).
- [ ] No new prose about sanitize_string / CONSOLE_ENABLE / @-literal added
      (contract point d verified absent; point e forbids scope creep).
- [ ] Did not attempt to reconcile the 9-row table with the 2023 aggregate
      (they differ by design — 2 suites are non-numeric).

### Documentation & Deployment

- [ ] README is the only modified file.
- [ ] PRD.md, pattern_match.{c,h}, notifier.{c,h}, all test_*.c, run_*.sh,
      rules.mk, .gitignore are byte-for-byte unchanged.
- [ ] The changeset's documentation loop is closed (this is the Mode B docs task).

---

## Anti-Patterns to Avoid

- ❌ Don't anchor edits on line numbers — use the unique text strings (README
  numbers shift if anything above is edited).
- ❌ Don't "reconcile" the 9-row table to sum to 2023 — the aggregate excludes 2
  non-numeric suites by design; only update numbers that actually CHANGED.
- ❌ Don't change `test_notifier_os` (31/31) or the "9 suites" wording — neither
  changed in this changeset.
- ❌ Don't add prose about sanitize_string / CONSOLE_ENABLE / @-literal — contract
  point (d) verified those keywords are absent from README; the deliverable is
  counts only. (Point e also forbids PRD/header edits.)
- ❌ Don't invent a README row or status line for the P1.M3.T1.S1 CONSOLE_ENABLE
  print — it adds NO test cases, so it is correctly absent from the test tables.
- ❌ Don't touch `run_all_tests.sh` or `run_notifier_stub_tests.sh` — they are the
  SOURCE of the numbers you transcribe; editing them is circular and out of scope.
- ❌ Don't edit PRD.md (human-owned; §11.2C fix already at commit 4d49460) or
  pattern_match.h JSDoc (no API change).
- ❌ Don't round or alter the aggregate format — it is an exact integer (2023)
  presented as "2023/2023".
- ❌ Don't guess the counts — run the gates (Task 1) and transcribe live output.
  If a live number differs from this PRP, trust the live output.

---

## Confidence Score: 10/10

A single-file, four-line Markdown edit whose target values were all captured by
**running the actual gates** during research: `run_all_tests.sh` prints aggregate
2023 (verified: 380+179+74+189+161+32+1008 = 2023) and `test_pattern_match`
prints `Total tests run: 380`; `run_notifier_stub_tests.sh` prints
`Total tests run: 14 / passed: 14 / failed: 0` for dispatch and `31/31` for OS.
Every old→new string is given verbatim and each oldText is unique in README.md.
The contract's keyword-review point (d) was empirically confirmed to require NO
edits (grep found no `sanitize_string`/`CONSOLE_ENABLE`/`@-literal` in README),
and the forbidden-files list (point e) is explicit. The only "risk" — touching a
non-README file — is fenced off by the Level 4 scope guards. No external
dependencies, no build step, no behavior change: the deliverable is purely
documentation accuracy.