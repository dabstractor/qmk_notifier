name: "PRP — P1.M2.T4.S1: Sync README.md overview & test results to the full changeset"
description: |

  Changeset-level documentation sync (Mode B). Runs LAST, after all five
  implementing subtasks (P1.M1.T1.S1 watchdog, P1.M1.T2.S1 adversarial tests,
  P1.M2.T1.S1 README SET_OS fix, P1.M2.T2.S1 NFA-128 fidelity suite,
  P1.M2.T3.S1 has_been_queried comment). The README still carries THREE stale
  references to the *old* pattern-corpus shape: it says "9 suites" (now 10, the
  NFA-128 fidelity suite was registered by P1.M2.T2.S1) and "2023/2023"
  (aggregate is now 2029/2029, +6 from the fidelity suite). P1.M2.T1.S1 already
  landed the host count (64→79) and the SET_OS resolution language, so those are
  out of scope — verified, not restated. This task edits exactly FOUR spots in
  README.md and changes nothing else. Pure documentation; no code/wire/build
  change. The validation loop re-runs BOTH test harnesses to confirm the numbers
  in the README match the live gate byte-for-byte.

---

## Goal

**Feature Goal**: Make `README.md`'s test-coverage description fully consistent
with the shipped acceptance gate after the entire P1 changeset. A reader (user,
maintainer, reviewer) of the README's "Running Tests" + "Overall Test Results"
sections must see the **correct** pattern-corpus shape: **10 suites**
(`test_fidelity_nfa128` included), aggregate **2029/2029**, the fidelity row in
the per-suite table, AND the already-accurate host results (79/79, SET_OS
resolved, watchdog regression covered). No stale claim about the gate remains.

**Deliverable**: ONE file modified — `README.md` (repo root). Exactly **four**
text edits in the "Running Tests" / "Overall Test Results" region:
1. "all 9 host-side suites" → "all 10 host-side suites" (line ~479).
2. "the 9 pattern-matcher suites" → "the 10 pattern-matcher suites" (line ~485).
3. Add a 10th table row for `test_fidelity_nfa128` after the `test_invalid_patterns`
   row (suite table, ~line 497).
4. "9 suites): **2023/2023**" → "10 suites): **2029/2029**" (line ~537).
No other README line changes. No source/build/wire/config change.

**Success Definition**:
- All four edits applied; `grep -nE '9 (host-side|pattern-matcher|suites)' README.md`
  returns NOTHING (the "9 suites" phrasing is gone everywhere).
- `grep -n '2023' README.md` returns NOTHING (the old aggregate is gone).
- `grep -n 'test_fidelity_nfa128' README.md` returns ≥2 hits (the new table row +
  its presence in run_all_tests.sh is reflected; at minimum the new table row).
- `grep -nE '10 (host-side|pattern-matcher)|10 suites' README.md` returns the
  three updated phrases.
- `grep -n '2029' README.md` returns the updated aggregate line.
- The live gate still matches the documented numbers EXACTLY:
  `./run_all_tests.sh` → "Total tests run across all suites: 2029" /
  "Total tests passed: 2029"; `./run_notifier_stub_tests.sh` → host 79/79.
- `git status` shows ONLY `README.md` modified (+ plan/ PRP/research). No
  notifier.c, test_*.c, run_*.sh, or any source file touched.

## User Persona (if applicable)

**Target User**: A maintainer, contributor, or end-user reading the README to
understand (a) how to run the tests and (b) the current test-coverage posture.
Also a reviewer/QA verifying the changeset's Definition of Done ("README
consistent with shipped gate").

**Use Case**: Reader opens the README, reads "Running Tests" + "Overall Test
Results", and the numbers/suite list they see MATCH what `./run_all_tests.sh`
and `./run_notifier_stub_tests.sh` actually print. No surprises, no stale
claims that mislead (e.g. believing SET_OS is broken, or that there are only 9
suites when the gate runs 10).

**User Journey**: clone → read README "Running Tests" → run `./run_all_tests.sh`
→ see 10 suites compile/run including "NFA-128 Fidelity Gate" → see aggregate
2029/2029 → compare to README → they agree.

**Pain Points Addressed**: Post-changeset, the README under-counts the pattern
corpus (says 9 suites / 2023 tests; gate runs 10 / 2029) and omits the new
fidelity suite from the per-suite table. A reader thinks the gate is smaller /
different than it is. This task closes that drift.

## Why

- **This IS the changeset-level documentation sync (Mode B).** Per
  `architecture/findings_and_risks.md`: "Mode B (changeset-level): A final task
  syncs README.md overview/features to reflect the entire changeset." Every
  implementing subtask is complete (or, for P1.M2.T3.S1, in progress with
  notifier.c-only scope that doesn't affect README counts). This is that final
  task.
- **The fidelity suite changed the gate's shape.** P1.M2.T2.S1 added
  `test_fidelity_nfa128` (6 cases) as the 10th suite in `run_all_tests.sh`. The
  README still describes the gate as "9 suites / 2023 tests" — that is now
  factually wrong by one suite and six tests.
- **Don't restate P1.M2.T1.S1's work.** That task already fixed the SET_OS
  claim and bumped the host count to 79. Re-touching those lines is scope creep
  and risks re-introducing inconsistency. This task touches ONLY the
  pattern-corpus spots that P1.M2.T1.S1 did NOT (because the fidelity suite
  wasn't registered yet at that point) plus verifying P1.M2.T1.S1's edits held.
- **Zero risk.** README text only — no behavior, wire, build, or test change.

## What

Four targeted text edits in `README.md`'s "Running Tests" → "Overall Test
Results" region. Each replaces a stale number/phrase with the verified-live
value, and one adds a single table row. The exact oldText/newText are in
"Implementation Tasks".

### Success Criteria

- [ ] README says "10 host-side suites" (was 9) at the "Comprehensive Test
      Suite" intro.
- [ ] README says "10 pattern-matcher suites" (was 9) at the table intro.
- [ ] The per-suite table has a 10th row: `test_fidelity_nfa128` | 6 | <desc>.
- [ ] README "Overall Test Results" says "10 suites): **2029/2029**" (was
      "9 suites: 2023/2023").
- [ ] NO other README change — `git diff README.md` shows exactly the 4 edits
      (the table-row add + 3 inline number changes); the host-count line, the
      SET_OS language, and the watchdog-regression mention are UNCHANGED.
- [ ] Live gate matches the README: `./run_all_tests.sh` prints 2029/2029 over
      10 suites; `./run_notifier_stub_tests.sh` prints host 79/79.

## All Needed Context

### Context Completeness Check

**Pass.** Every stale spot is identified by `grep` with its exact current text
(captured via `sed -n` and verified in the research notes). Every replacement
number is verified by RUNNING the actual test suites on the current tree
(`./run_all_tests.sh` → 2029/2029, 10 suites; `./run_notifier_stub_tests.sh` →
host 79/79; `test_fidelity_nfa128` alone → 6/6). The already-accurate lines
(host 79, SET_OS, watchdog) are listed so the implementer does not touch them.
An implementer with only this PRP + repo access can apply the four edits with
no further research.

### Documentation & References

```yaml
# MUST READ — the changeset shape (what changed, what the README must reflect)
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/architecture/findings_and_risks.md
  section: "Documentation Sync" (Mode A vs Mode B) + "Research Summary"
  why: "Defines THIS task as the Mode B final sweep: 'sync README.md overview/
        features to reflect the entire changeset.' Confirms all 4 issues' fixes
        and that this task owns the cross-cutting README consistency."
  critical: "Mode B means: do NOT re-do per-subtask doc work (P1.M2.T1.S1 already
        did the SET_OS/host-count fix). Touch ONLY what is still stale."

- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/architecture/system_context.md
  section: "Test Infrastructure (two independent harnesses)"
  why: "Authoritative description of the two gates: Harness A = run_all_tests.sh
        (pattern corpus, was 9 suites now 10); Harness B = run_notifier_stub_tests.sh
        (3 notifier stub drivers: dispatch/os/host). The README must match THIS."
  critical: "The fidelity suite was added to Harness A (run_all_tests.sh), NOT to
        Harness B. The README's 'run_all_tests.sh, N suites' line is the one to bump."

# MUST READ — the source of the new 10th suite (what to put in the table row)
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/P1M2T2S1/PRP.md
  section: "Goal" + "Success Definition"
  why: "Defines test_fidelity_nfa128: compiled with -DNFA_MAX_PATTERN=128 (the
        firmware budget); #error-guarded so it cannot drift to 2048; verifies
        (guard) build at 128, (smoke) short patterns match, (i) 128-byte boundary
        fits, (ii) over-budget clamps safely. 6 cases (verified by running it)."
  critical: "The table-row description must convey: NFA-budget fidelity gate,
        firmware NFA_MAX_PATTERN=128 budget, #error-guarded against drift to 2048,
        boundary+over-budget coverage. Count = 6 (live-verified, not assumed)."

# MUST READ — what P1.M2.T1.S1 ALREADY fixed (DO NOT restate / re-touch)
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/P1M2T1S1/PRP.md
  section: "Goal" + "Implementation Tasks"
  why: "P1.M2.T1.S1 already: (1) fixed the SET_OS 'pending fix' claim to 'resolved
        by length-aware typed reassembly'; (2) bumped test_notifier_host count
        64→79; (3) added the 'adversarial typed-command framing gate (Issue 1
        watchdog regression)' coverage line. Those README lines are ALREADY correct."
  critical: "Do NOT edit the host-count line (79), the SET_OS resolution sentences,
        or the watchdog-regression mention. Re-touching them is scope creep and
        risks breaking P1.M2.T1.S1's carefully-worded prose. This task is strictly
        the pattern-corpus suite-count + aggregate-count sync."

# MUST READ — the file being edited (the 4 exact stale spots)
- file: README.md
  section: "Running Tests → Comprehensive Test Suite" (lines ~478-498) +
           "Overall Test Results" (line ~537)
  why: "The three stale '9' references (lines 479, 485, 537) and the missing 10th
        table row (after the test_invalid_patterns row at ~line 497). Exact
        current text captured in Implementation Tasks oldText anchors."
  pattern: "The table is a markdown table: `| Suite | Count | Covers |`. Add the
            new row as one line `| \`test_fidelity_nfa128\` | 6 | <desc> |` immediately
            after the `test_invalid_patterns` row, before the blank line."
  gotcha: "Line numbers are research-time (~479/485/497/537) and may drift if
           P1.M2.T3.S1 or any other task touches README.md. It shouldn't (P1.M2.T3.S1
           is notifier.c-only), but ANCHOR ON THE EXACT oldText STRINGS, not line
           numbers. Re-grep if anything seems off."

# Confirms the live counts (the numbers the README must state)
- file: (live execution, not a file)
  why: "`./run_all_tests.sh` prints 'Total tests run across all suites: 2029' and
        'Total tests passed: 2029'; the runner compiles+runs the 10th suite
        'NFA-128 Fidelity Gate' (6 cases). `./run_notifier_stub_tests.sh` prints
        'Total tests run: 79 / passed: 79 / failed: 0' for host. These are the
        exact values the README must contain."
  critical: "Do NOT round, abbreviate, or approximate. The README must say exactly
        '10 suites', '2029/2029', and (already present) '79 cases'. Re-run both
        scripts during validation to confirm the README still matches."

# Scope boundary — the parallel in-progress task (no README overlap)
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/P1M2T3S1/PRP.md
  why: "P1.M2.T3.S1 (in progress, parallel) edits ONLY notifier.c comments. It
        does NOT touch README.md. No file overlap with this task."
  critical: "Do NOT touch notifier.c. Do NOT add has_been_queried content to the
        README — that is an in-source comment, not a README concern (its PRP
        explicitly says 'No README change')."

# The item's own conditional that is NOT met (so it's a no-op)
- item: "RESEARCH NOTE point (C): robustness/guarantees section"
  why: "The item says 'IF the README has a robustness/guarantees section discussing
        firmware behavior under malformed input, ensure it reflects the watchdog
        recovery.' Verified by grep: the README has NO such dedicated section. The
        only malformed-input-robustness mention is the watchdog-regression line
        P1.M2.T1.S1 already added (line ~547), which is accurate."
  critical: "Do NOT invent a new 'Robustness' marketing section. That is scope
        creep beyond 'sync docs to changeset'. Point (C) is satisfied by the
        existing accurate watchdog line — verify it (grep 'watchdog'), don't expand it."
```

### Current Codebase tree (relevant slice)

```bash
README.md                  # ← MODIFY (4 text edits in Running Tests / Overall Test Results). ONLY file changed.
notifier.c                 # P1.M1.T1.S1 (watchdog) + P1.M2.T3.S1 (comment). DO NOT TOUCH.
notifier.h                 # unaffected. DO NOT TOUCH.
pattern_match.{c,h}        # unaffected. DO NOT TOUCH.
qmk_stubs/*                # unaffected. DO NOT TOUCH.
test_notifier_host.c       # P1.M1.T2.S1 (adversarial cases). DO NOT TOUCH.
test_fidelity_nfa128.c     # P1.M2.T2.S1 (the new 10th suite). DO NOT TOUCH (only DOCUMENTED here).
run_all_tests.sh           # P1.M2.T2.S1 (registered the 10th suite). DO NOT TOUCH (only run for validation).
run_notifier_stub_tests.sh # unaffected (run only for validation). DO NOT TOUCH.
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be changed

```bash
README.md                  # MODIFIED: 4 edits —
                           #   L~479 "9 host-side" -> "10 host-side"
                           #   L~485 "9 pattern-matcher" -> "10 pattern-matcher"
                           #   L~497 add test_fidelity_nfa128 table row (6 cases)
                           #   L~537 "9 suites): **2023/2023**" -> "10 suites): **2029/2029**"
# (no other files change)
```

### Known Gotchas of our codebase & Library Quirks

```markdown
<!-- CRITICAL: anchor on exact text, not line numbers. Line numbers are research-
     time (~479/485/497/537) and the README is 571 lines; any prior edit could
     shift them. The oldText strings in Implementation Tasks are the stable
     anchors. Re-run `grep -n '9 host-side\|9 pattern-matcher\|9 suites' README.md`
     to locate them if needed. -->

<!-- CRITICAL: the aggregate is 2029, NOT 2023. The fidelity suite adds 6 cases
     (380+179+74+189+smoke+10+161+32+1008 = 2023 for the old 9; +6 = 2029 for 10).
     Do NOT write 2023 anywhere. Verified live: ./run_all_tests.sh -> 2029. -->

<!-- CRITICAL: there are now 10 suites, NOT 9. test_fidelity_nfa128 is the 10th.
     Do NOT write "9" anywhere in the pattern-corpus description. The THREE
     stale "9"s are at lines ~479, ~485, ~537 — fix all three. -->

<!-- GOTCHA: the fidelity suite is compiled with -DNFA_MAX_PATTERN=128, NOT the
     default 2048. The table-row description should note the firmware budget +
     the #error guard (that's the whole point of the suite, per P1.M2.T2.S1). -->

<!-- GOTCHA: the per-suite table uses backticks around the suite name
     (`test_fidelity_nfa128`) and pipes. Match the existing row style exactly:
     `| \`test_<name>\` | <count> | <description> |`. Count column = 6. -->

<!-- GOTCHA: do NOT touch the host-count line (79), the SET_OS resolution
     sentences, or the watchdog-regression mention. Those are P1.M2.T1.S1's
     correct work. This task is the pattern-corpus count sync ONLY. -->

<!-- GOTCHA: markdown table row must be a single line (no embedded newline) and
     placed immediately after the test_invalid_patterns row, before the blank
     line that precedes "A second, separate gate validates...". -->

<!-- GOTCHA: this is a doc-only change. Comments/numbers only. No code, wire,
     build, config, or test change. Validation = re-run both test scripts and
     confirm the README numbers match their live output byte-for-byte. -->
```

## Implementation Blueprint

### Data models and structure

None. This task edits README markdown text.

### Implementation Tasks (ordered by dependencies)

Four independent edits in `README.md`. Apply with the `edit` tool, matching the
EXACT oldText (verified current text via `sed -n` + `grep`). Re-run
`grep -n '9 host-side\|9 pattern-matcher\|9 suites\|2023' README.md` first to
confirm the four stale spots still exist at the expected text (they will unless
another task touched README.md — P1.M2.T3.S1 does NOT).

```yaml
Task 1: FIX the "Comprehensive Test Suite" intro count (README.md, ~line 479)
  - oldText (EXACT current text — the whole line):
        To build and run all 9 host-side suites plus a performance micro-benchmark:
  - newText:
        To build and run all 10 host-side suites plus a performance micro-benchmark:
  - PRESERVE: everything except the digit (9 -> 10).
  - WHY: P1.M2.T2.S1 added the 10th suite (test_fidelity_nfa128); the gate now
    builds/runs 10 host-side suites, not 9.

Task 2: FIX the table intro count (README.md, ~line 485)
  - oldText (EXACT current text — the whole line):
        This builds and runs the 9 pattern-matcher suites (all linking `pattern_match.c`) and prints an aggregate summary. The live per-suite breakdown:
  - newText:
        This builds and runs the 10 pattern-matcher suites (all linking `pattern_match.c`) and prints an aggregate summary. The live per-suite breakdown:
  - PRESERVE: everything except the digit (9 -> 10).
  - WHY: same as Task 1 — the corpus now has 10 suites.

Task 3: ADD the 10th table row for test_fidelity_nfa128 (README.md, ~line 497)
  - oldText (EXACT current text — the test_invalid_patterns row + the blank line
    after it; anchor on the last existing row so the new row lands in the right
    place):
        | `test_invalid_patterns` | 1008 | 46 pathological patterns × many inputs |

        A second, separate gate validates the **receiver/dispatcher** side of the module:
  - newText (insert the fidelity row between them; keep the blank line + the
    "A second, separate gate" line verbatim):
        | `test_invalid_patterns` | 1008 | 46 pathological patterns × many inputs |
        | `test_fidelity_nfa128` | 6 | NFA-budget fidelity gate: matcher exercised at the firmware `NFA_MAX_PATTERN=128` budget (128-byte boundary fit + over-budget safe-clamp); `#error`-guarded so the gate cannot silently drift back to 2048 |

        A second, separate gate validates the **receiver/dispatcher** side of the module:
  - PRESERVE: the test_invalid_patterns row verbatim; the blank line; the "A
    second, separate gate" sentence verbatim. Only ONE new line is added.
  - WHY: the table lists every suite in run_all_tests.sh; the 10th suite was
    missing. Count=6 is live-verified (./run_all_tests.sh -> "NFA-128 Fidelity
    Gate" 6/6). Description conveys the firmware budget + #error guard (the
    suite's reason for existing, per P1.M2.T2.S1).

Task 4: FIX the "Overall Test Results" aggregate count + suite count (~line 537)
  - oldText (EXACT current text — the whole bullet line):
        - Pattern-match corpus (`./run_all_tests.sh`, 9 suites): **2023/2023** tests passing.
  - newText:
        - Pattern-match corpus (`./run_all_tests.sh`, 10 suites): **2029/2029** tests passing.
  - PRESERVE: the bullet structure, the backtick code span, the bold. Only TWO
    tokens change: "9 suites" -> "10 suites" and "2023/2023" -> "2029/2029".
  - WHY: the aggregate is now 2029/2029 over 10 suites (2023 from the original 9
    + 6 from the fidelity suite). Live-verified: ./run_all_tests.sh ->
    "Total tests run across all suites: 2029" / "Total tests passed: 2029".

Task 5: VERIFY (no edit) — no stale "9"/"2023" remain; live gate matches the
        README; host/SET_OS/watchdog lines untouched. (See Validation Loop.)
```

### Implementation Patterns & Key Details

```markdown
<!-- PATTERN: anchor on exact text, not line numbers. Line numbers are
     research-time (~479/485/497/537) and may drift if any prior task touched
     README.md. The oldText strings above are the stable anchors; re-grep to
     locate them. -->

<!-- PATTERN: minimal-surface edit. Each task changes ONLY the stale token(s).
     Tasks 1, 2, 4 are single-digit or two-token swaps. Task 3 adds exactly one
     table row. No surrounding prose is rewritten — keeps the diff trivially
     reviewable (every changed line is obviously a count fix). -->

<!-- PATTERN: match existing table style. The fidelity row uses the same
     `| \`name\` | count | covers |` shape as the other 9 rows, with backticks
     around the suite name and a concise "Covers" cell. Count column = 6. -->

<!-- CRITICAL: the numbers are live-verified, not assumed.
       10 suites   <- run_all_tests.sh compiles+runs 10 (incl. test_fidelity_nfa128)
       2029/2029   <- run_all_tests.sh aggregate (2023 old + 6 fidelity)
       6           <- test_fidelity_nfa128 standalone count
       79 (host)   <- run_notifier_stub_tests.sh (UNCHANGED, already in README)
     Re-run both scripts during validation to reconfirm. -->

<!-- CRITICAL: do NOT touch P1.M2.T1.S1's lines. The host-count line (79), the
     four SET_OS-resolution sentences, and the watchdog-regression coverage line
     are ALREADY correct. Editing them is scope creep and risks re-breaking
     carefully-worded prose. This task = pattern-corpus count sync ONLY. -->

<!-- CRITICAL: point (C) is a NO-OP. The README has no dedicated robustness
     section; the only malformed-input mention (watchdog line ~547) is accurate.
     Do NOT add a new "Robustness" section — that is scope creep beyond "sync
     docs". -->

<!-- ANTI-PATTERN: do NOT change any source file (notifier.c, test_*.c,
     run_*.sh, pattern_match.*, qmk_stubs/*). This is README-only. -->

<!-- ANTI-PATTERN: do NOT "round" or paraphrase the counts. The README must say
     exactly 10 / 2029 / (79 unchanged) to match the gate byte-for-byte. -->

<!-- ANTI-PATTERN: do NOT edit the 9 existing table rows. Their counts (380,
     179, 74, 189, smoke, 10, 161, 32, 1008) are unchanged; only the aggregate
     (2029) and the new 10th row change. -->
```

### Integration Points

```yaml
FILE MODIFIED:
  - README.md (repo root). The ONLY file this task touches.
EDITS (4):
  - ~L479: "9 host-side" -> "10 host-side"
  - ~L485: "9 pattern-matcher" -> "10 pattern-matcher"
  - ~L497: +1 table row (test_fidelity_nfa128 | 6 | <desc>)
  - ~L537: "9 suites): **2023/2023**" -> "10 suites): **2029/2029**"
PRESERVED (unchanged — P1.M2.T1.S1's correct work):
  - The test_notifier_host count line (79 cases).
  - The four SET_OS-resolution sentences.
  - The "adversarial typed-command framing gate (Issue 1 watchdog regression)" line.
  - All 9 existing per-suite table rows and their counts.
  - Everything outside the "Running Tests" / "Overall Test Results" region.
CONSUMES (context only, unchanged):
  - P1.M2.T2.S1: test_fidelity_nfa128.c + its run_all_tests.sh registration (the
    source of the 10th suite / +6 count this task documents).
  - architecture/findings_and_risks.md: the Mode B mandate for this task.
  - P1.M2.T1S1 PRP: confirmation of what is already accurate (don't restate).
BUILD / WIRE / CONFIG / DATABASE / ROUTES:
  - none. No build-system, wire-protocol, config, or code change.
```

## Validation Loop

> This is a documentation-only change — no code can break. Validation confirms
> (1) the four stale spots are gone and replaced with the live values, (2)
> P1.M2.T1.S1's lines are untouched, (3) the LIVE gate still matches the README
> numbers byte-for-byte, and (4) only README.md changed. All commands run from
> the repo root.

### Level 1: Stale spots are gone; live values are present (the primary gate)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. NO stale "9 suites" / "9 host-side" / "9 pattern-matcher" remain anywhere.
grep -nE '9 (suites|host-side|pattern-matcher)' README.md
# Expected: NO output. (Any hit = a stale "9" survived — fix it.)

# 1b. NO stale aggregate "2023" remains.
grep -n '2023' README.md
# Expected: NO output.

# 1c. The new values ARE present.
grep -nE '10 (host-side|pattern-matcher) suites|10 suites' README.md
# Expected: 3 hits — the ~L479 "10 host-side suites", the ~L485 "10 pattern-
#           matcher suites", and the ~L537 "10 suites".

grep -n '2029' README.md
# Expected: 1 hit — the ~L537 aggregate line "**2029/2029**".

# 1d. The new fidelity table row is present.
grep -n 'test_fidelity_nfa128' README.md
# Expected: ≥1 hit — the new table row (| `test_fidelity_nfa128` | 6 | ... |).
#           Inspect the row: backticked name, Count=6, mentions the firmware
#           NFA_MAX_PATTERN=128 budget and the #error guard.
grep -n 'NFA_MAX_PATTERN=128' README.md
# Expected: ≥1 hit inside the fidelity row's description.
```

### Level 2: P1.M2.T1.S1's correct lines are UNCHANGED (the no-regression gate)

```bash
cd /home/dustin/projects/qmk-notifier

# 2a. The host count is STILL 79 (not reverted to 64, not bumped to a guess).
grep -n 'test_notifier_host.*(79 cases)' README.md
grep -nE 'test_notifier_host` \(\d+ cases\)' README.md
# Expected: the line reads "test_notifier_host (79 cases)" — UNCHANGED.

# 2b. The SET_OS resolution language is intact.
grep -niE 'SET_OS.*resolved|length-aware typed-reassembly|ETX.*collision.*resolved' README.md
# Expected: ≥1 hit — P1.M2.T1.S1's sentence is still there, verbatim.

# 2c. The watchdog-regression coverage line is intact.
grep -n 'watchdog regression' README.md
# Expected: 1 hit — "the adversarial typed-command framing gate (Issue 1
#           watchdog regression)" — UNCHANGED.

# 2d. git diff shows EXACTLY the 4 edits (3 count swaps + 1 table-row add);
#     every other changed line belongs to one of those edits.
git diff -- README.md
# Expected: 4 changed regions:
#   - "9 host-side" -> "10 host-side"
#   - "9 pattern-matcher" -> "10 pattern-matcher"
#   - +1 line: the test_fidelity_nfa128 table row
#   - "9 suites): **2023/2023**" -> "10 suites): **2029/2029**"
# NO other README line changed. (If the diff touches the host-count line, the
# SET_OS sentences, or any prose outside Running Tests/Overall Test Results,
# that is scope creep — revert it.)
```

### Level 3: LIVE gate matches the README byte-for-byte (the truth gate)

```bash
cd /home/dustin/projects/qmk-notifier

# 3a. Pattern corpus: 10 suites, 2029/2029.
./run_all_tests.sh >/tmp/ra.out 2>&1; echo "run_all_tests exit=$?  (expect 0)"
grep -E 'Total tests run across all suites|Total tests passed|Total tests failed|ALL TESTS PASSED' /tmp/ra.out
# Expected:
#   Total tests run across all suites: 2029
#   Total tests passed: 2029
#   Total tests failed: 0
#   ✓ ALL TESTS PASSED - BACKWARD COMPATIBILITY VERIFIED
grep -E 'NFA-128 Fidelity Gate' /tmp/ra.out
# Expected: the 10th suite compiled + ran (and "ALL TESTS PASSED" for it).
# Compare these numbers to README.md — they MUST agree (10 suites, 2029/2029).
rm -f /tmp/ra.out

# 3b. Notifier stub gate: dispatch 14, os 31, host 79 (unchanged).
./run_notifier_stub_tests.sh >/tmp/rn.out 2>&1; echo "run_notifier_stub exit=$?  (expect 0)"
grep -E 'Total tests run: \d+ / passed: \d+|notifier (dispatch|os|host) fails|PASSED' /tmp/rn.out
# Expected:
#   Total tests run: 14 / passed: 14 / failed: 0
#   Total tests run: 31 / passed: 31 / failed: 0
#   Total tests run: 79 / passed: 79 / failed: 0     <- host matches README "79 cases"
#   notifier dispatch fails=0 ... notifier os fails=0 ... notifier host fails=0
#   ✓ notifier stub-compile gate PASSED
rm -f /tmp/rn.out

# 3c. The fidelity suite's standalone count matches the table row (6).
gcc -o /tmp/tn128 test_fidelity_nfa128.c pattern_match.c -I. -DNFA_MAX_PATTERN=128 -std=c99 && \
  /tmp/tn128 | grep -E 'Total tests run|Tests passed|Tests failed'
# Expected: Total tests run: 6 / Tests passed: 6 / Tests failed: 0.
#           The table row must say Count=6 — it does.
rm -f /tmp/tn128
```

### Level 4: Diff hygiene (ONLY README.md changed)

```bash
cd /home/dustin/projects/qmk-notifier

git status --porcelain
# Expected: ` M README.md`, plus `?? plan/003_.../P1M2T4S1/` (this PRP/research),
#           plus whatever P1.M2.T3.S1 left ( M notifier.c + ?? .../P1M2T3S1/).
#           NOTHING else. In particular NO change to: notifier.h, pattern_match.*,
#           qmk_stubs/*, test_*.c, run_all_tests.sh, run_notifier_stub_tests.sh,
#           PRD.md, tasks.json, prd_snapshot.md, .gitignore.

# Confirm this task touched ONLY README.md (filter out P1.M2.T3.S1's notifier.c).
git status --porcelain | grep -E '^ M' | grep -vE ' M (README.md|notifier.c)$' \
  && echo "ERROR: unexpected modified files" || echo "scope clean: source files limited to README.md (+ P1.M2.T3.S1's notifier.c)"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: `grep -nE '9 (suites|host-side|pattern-matcher)' README.md` →
      empty; `grep -n '2023' README.md` → empty; `grep -n '2029' README.md` →
      the aggregate line; `grep -n 'test_fidelity_nfa128' README.md` → the new
      table row (Count=6, mentions NFA_MAX_PATTERN=128 + #error guard).
- [ ] Level 2: host line still "79 cases"; SET_OS resolution sentence intact;
      watchdog-regression line intact; `git diff README.md` shows ONLY the 4
      edits (3 count swaps + 1 table-row add).
- [ ] Level 3: `./run_all_tests.sh` → 2029/2029 over 10 suites (incl. NFA-128
      Fidelity Gate), exit 0; `./run_notifier_stub_tests.sh` → host 79/79, exit 0;
      the README's numbers match the live output exactly.
- [ ] Level 4: `git status` shows `README.md` as the only source-file change from
      this task (notifier.c may be modified by the parallel P1.M2.T3.S1 — that is
      not this task's concern).

### Feature Validation

- [ ] A reader of README "Running Tests" sees "10 host-side suites" and a
      per-suite table with 10 rows including `test_fidelity_nfa128`.
- [ ] A reader of README "Overall Test Results" sees "10 suites): **2029/2029**".
- [ ] The numbers in the README match `./run_all_tests.sh` and
      `./run_notifier_stub_tests.sh` output exactly (no drift).
- [ ] P1.M2.T1.S1's prior fixes (host 79, SET_OS resolved, watchdog) are intact.

### Code Quality Validation

- [ ] Edits follow the existing README markdown style (table row shape, bold,
      backtick code spans).
- [ ] No prose outside the 4 stale spots was rewritten (minimal-surface diff).
- [ ] No anti-patterns (see below): no source files touched, no counts rounded,
      no new robustness section invented, no P1.M2.T1.S1 lines re-edited.

### Documentation & Deployment

- [ ] The README is now the single accurate source for the test-coverage shape
      (matches the live gate).
- [ ] No new env vars / config / build-system / wire changes (doc-only).

---

## Anti-Patterns to Avoid

- ❌ Don't touch any file other than `README.md`. notifier.c (P1.M1.T1.S1 +
  P1.M2.T3.S1), test_notifier_host.c (P1.M1.T2.S1), test_fidelity_nfa128.c /
  run_all_tests.sh (P1.M2.T2.S1), run_notifier_stub_tests.sh, pattern_match.*,
  qmk_stubs/* are ALL other tasks' scope or read-only.
- ❌ Don't restate or re-edit P1.M2.T1.S1's work. The host-count line (79), the
  SET_OS resolution sentences, and the watchdog-regression line are ALREADY
  correct. Re-touching them is scope creep and risks breaking the prose.
- ❌ Don't write the old numbers. The aggregate is **2029/2029** (NOT 2023), the
  corpus has **10** suites (NOT 9), the fidelity suite has **6** cases. All
  live-verified.
- ❌ Don't round, abbreviate, or paraphrase the counts — the README must match
  the gate output byte-for-byte.
- ❌ Don't invent a new "Robustness" section. Item point (C) is conditional
  ("IF the README has a robustness section"); it does NOT. The only malformed-
  input mention is the watchdog line (already accurate). Adding a marketing
  section is scope creep.
- ❌ Don't anchor on line numbers — they're research-time (~479/485/497/537).
  Anchor on the EXACT oldText strings in Implementation Tasks; re-grep first.
- ❌ Don't rewrite the 9 existing table rows or any prose around them. Only the
  aggregate line and the new 10th row change.
- ❌ Don't add `has_been_queried` content to the README — that's an in-source
  notifier.c comment (P1.M2.T3.S1), explicitly "No README change".

---

## Confidence Score: 10/10

The deliverable is four exact text edits in `README.md`, all stale spots
pre-located by `grep` with their verbatim current text captured (and re-read via
`sed -n`), and every replacement value live-verified by RUNNING both test suites
on the current tree (`./run_all_tests.sh` → 2029/2029 over 10 suites including
"NFA-128 Fidelity Gate" 6/6; `./run_notifier_stub_tests.sh` → host 79/79;
`test_fidelity_nfa128` standalone → 6/6). The exact oldText anchors and exact
newText for all four edits are given verbatim in Implementation Tasks. The
already-accurate lines (host 79, SET_OS resolution, watchdog regression — all
P1.M2.T1.S1's work) are explicitly listed as do-not-touch with grep checks to
confirm they survive. The item's conditional robustness point (C) is verified as
not-applicable (no such section exists). No conflict with the parallel
P1.M2.T3.S1 (notifier.c-only, no README overlap) or any prior task. The task is
README text-only, so risk is NONE; the validation re-runs both gates to confirm
the README matches the live output byte-for-byte and that only README.md changed.
No external dependencies; no code/build/wire/config change.