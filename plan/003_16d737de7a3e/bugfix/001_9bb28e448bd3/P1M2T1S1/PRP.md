# PRP — P1.M2.T1.S1: Update README.md "Overall Test Results" block to reflect host cases passing

## Goal

**Feature Goal**: Replace the stale "Overall Test Results" block in `README.md`
(lines ~540-545) that falsely reports the four `SET_OS` typed-command test blocks
as "pending an upstream ETX-collision framing fix" with accurate text: all
`test_notifier_host` cases pass, `SET_OS` is functional, and no fixes are pending.
The `SET_OS` `cmd_id` (`0x03`) / `ETX` collision is already resolved by the
length-aware typed-reassembly path in `notifier.c` (`typed_literal_remaining`).

**Deliverable**: The modified file `README.md` at the repo root — two surgical
text edits: (1) the "Overall Test Results" `test_notifier_host` bullet (the stale
SET_OS-pending prose → accurate), and (2) the `(64 cases)` count token on the
coverage-description line (→ the live count, for internal consistency). No other
file changes.

**Success Definition**:
- The README's results block accurately states all `test_notifier_host` cases pass,
  including the four `SET_OS` blocks, with no pending fixes.
- The count shown matches the LIVE gate (verified via `run_notifier_stub_tests.sh`).
- The README is internally consistent (the count on the coverage line == the count
  in the results block == the live gate number).
- The pattern-match corpus result (2023/2023), dispatch (14/14), and OS (31/31)
  results are UNCHANGED (they are correct).
- A reader correctly understands `SET_OS` — the host-authoritative OS seam (§4.7) —
  is functional.

> **⚠️ KEY DEVIATION FROM THE LITERAL CONTRACT (read first):** the item contract
> says "reflect 64/64 host cases passing." That number is **stale**: the parallel
> task **P1.M1.T2.S1 (adversarial typed-command tests) is LANDED** and brought
> `test_notifier_host` from 64 → **79** cases (verified: `git status` shows
> `M test_notifier_host.c`; the adv-A..adv-D section is present; the live gate
> reports `Total tests run: 79 / passed: 79 / failed: 0`). P1.M1.T2.S1 does NOT
> touch README. Writing "64" would recreate the exact defect this task exists to
> fix (a stale, wrong count). Per the parallel-execution mandate to "consume/build
> upon the outputs of the previous PRP," this PRP writes the **live count (79)** and
> instructs the implementer to verify the count at edit time. This is documented
> throughout (research/findings.md + the deviation note below).

## User Persona (if applicable)

**Target User**: (1) A maintainer or contributor reading the README to gauge
project health — they must NOT be misled into thinking `SET_OS` (a core v0.3.0
host-rules feature, §4.7) is broken. (2) An end user evaluating whether the
host-rules/typed-command feature is usable.

**Use Case**: Reader opens README, scans "Overall Test Results", and sees an
accurate, internally-consistent status: matcher 2023/2023, dispatch 14/14, OS
31/31, host 79/79, no pending fixes, `SET_OS` resolved by length-aware reassembly.

**User Journey**: reader → "Overall Test Results" block → sees host = N/N (N=live
count) passing → understands the typed-command namespace (incl. `SET_OS`) is fully
functional → no false bug reports / re-investigation of a non-issue.

**Pain Points Addressed**: The current block (lines 540-545) says "the four SET_OS
blocks are currently pending an upstream ETX-collision framing fix … the remaining
57 assertions pass." This is false (the fix ships; all 79 pass). A reader would
incorrectly believe `SET_OS` is non-functional and might re-investigate or avoid
the feature.

## Why

- **Corrects a false-negative about a core feature.** `SET_OS` is the
  host-authoritative OS seam central to the v0.3.0 host-rules feature (§4.7). The
  README falsely reports it as broken/pending. Issue 2 (PRD §2.3/h3.1) calls this
  out as a Minor doc defect about a core feature.
- **Keeps the README internally consistent.** Two spots say "(64 cases)" — the
  coverage line (516) and the results block (540). After this fix they must both
  reflect the live count, else the README contradicts itself.
- **Closes the §11 acceptance-gate / Definition-of-Done documentation gap.** The
  PRD §11 gate is green (host 79/79); the README must say so.
- **Low risk.** Pure documentation (Markdown) — no code, build, or wire change.

## What

Two text edits to `README.md`:

1. **Results block (lines 540-545)** — replace the stale SET_OS-pending bullet
   with accurate text: all `test_notifier_host` cases pass (the live count, e.g.
   79), including the four `SET_OS` blocks; the `SET_OS` `0x03`/`ETX` collision is
   resolved by the length-aware typed reassembly (`typed_literal_remaining` in
   `notifier.c`); list the coverage categories (including the adversarial
   framing gate that explains the count > 64).
2. **Count token on the coverage line (line 516)** — `(64 cases)` → `(N cases)`
   where N is the live host count (currently 79), for internal consistency. The
   coverage PROSE (lines 517-523) is left unchanged.

### Success Criteria

- [ ] The results-block bullet (540-545) states all host cases pass (live count),
      including the four `SET_OS` blocks, with no pending fixes.
- [ ] The results block cites the resolution mechanism (length-aware typed
      reassembly / `typed_literal_remaining` in `notifier.c`).
- [ ] The `(64 cases)` count on line 516 is updated to the live count.
- [ ] The two count mentions (line 516 + results block) are equal to each other
      AND to the live `run_notifier_stub_tests.sh` host count.
- [ ] Pattern-match 2023/2023, dispatch 14/14, OS 31/31 results are UNCHANGED.
- [ ] No code/build/wire change; only README.md is edited.

## All Needed Context

### Context Completeness Check

**Pass.** The exact OLD text (both stale spots, verified verbatim against the
working README at lines 516 and 540-545) and the exact NEW text are specified
inline below. The live gate count (79) was **empirically verified** (ran
`run_notifier_stub_tests.sh` → `test_notifier_host` 79/79, stable). The
resolution mechanism (`typed_literal_remaining`, notifier.c:115) was confirmed by
grep. The cross-task state (P1.M1.T2.S1 LANDED, count 64→79; P1.M2.T4.S1 owns the
overview/features sync) was verified via git status + tasks.json. An implementer
with only this PRP + repo can make the two edits with no guessing.

### Documentation & References

```yaml
# MUST READ — the bug this fixes (Issue 2)
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/prd_snapshot.md  (also the bugfix PRD)
  section: "### Issue 2: README falsely reports the SET_OS typed-command tests as broken / pending a fix (h3.1)"
  why: "The canonical statement of the defect: README lines ~540-545 say SET_OS is
        'pending an upstream ETX-collision framing fix'; the live gate says 79/79
        (the fix ships). Suggested fix: 'Update the README block to state all 64
        test_notifier_host cases pass and that the SET_OS 0x03/ETX collision is
        resolved by the length-aware typed reassembly.'"
  critical: "The PRD's '64' was the count BEFORE P1.M1.T2.S1 added adversarial
        cases. The live count is now 79 (see DEVIATION). Write the live count."

# MUST READ — the contract this consumes (P1.M1.T2.S1 is LANDED)
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/P1M1T2S1/PRP.md
  why: "P1.M1.T2.S1 appended the adversarial typed-command section (adv-A..adv-D,
        15 CK assertions) to test_notifier_host.c, bringing the count 64 -> 79
        ('79/79 pass (existing 64 + 15 new assertions)'). It does NOT touch README."
  critical: "Because P1.M1.T2.S1 LANDED before this task, the host count is 79, not
        64. The README must reflect 79. (Verified: git status shows M test_notifier_host.c;
        the adv section is present at lines 414+.)"

# The resolution mechanism (cite it accurately in the README prose)
- file: notifier.c
  section: "lines 98-127 (Typed-path length-aware reassembly) + line 115 (typed_literal_remaining)"
  why: "The length-aware typed-reassembly path tracks how many literal payload bytes
        remain, so the SET_OS cmd_id 0x03 is consumed as a literal arg byte and NOT
        mistaken for the ETX terminator. This is what makes the four SET_OS blocks pass."
  critical: "Name it correctly in the README: 'length-aware typed reassembly'
        (the symbol is typed_literal_remaining). Do NOT confuse it with the separate
        typed_awaiting_terminator watchdog (Issue 1) at notifier.c:116-123."

# The file being edited
- file: README.md
  section: "lines 516 (coverage count token) + 536-548 (Overall Test Results block)"
  why: "516 holds the '(64 cases)' count in the coverage list; 540-545 hold the stale
        SET_OS-pending bullet. Both must be updated."
  pattern: "The results block is a bulleted list of suite results. Keep the bullet
            style; only the test_notifier_host bullet's prose + count change."
  gotcha: "Lines 538-540 (matcher 2023/2023, dispatch 14/14, OS 31/31) are CORRECT —
           do not touch them. The Performance Impact line (~546) is unchanged."

# The gate to verify the live count
- file: run_notifier_stub_tests.sh
  why: "Run it; the 'Total tests run: N' line for the host driver gives the live count.
        Currently 79. The implementer writes THAT number (robust to further drift)."
  critical: "Do NOT hardcode a number without verifying — run the gate first. The count
        may shift if other cases land between research and implementation."

# Scope boundary — don't overlap the later broad sync task
- file: plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/tasks.json
  section: "P1.M2.T4 (Sync changeset-level documentation)"
  why: "P1.M2.T4.S1 owns the README OVERVIEW/FEATURES sections + the broad cross-cutting
        sweep (watchdog fix, corrected test results, NFA-128 fidelity suite,
        has_been_queried). THIS task (P1.M2.T1.S1) owns ONLY the test-results block fix."
  critical: "Do NOT expand into overview/features prose — that is P1.M2.T4.S1's scope.
        Keep edits to the results block + the count token."
```

### Current Codebase tree (relevant slice)

```bash
README.md                 # ← MODIFY (2 regions: count token at L516, results block L540-545). NOTHING ELSE.
notifier.c                # LANDED fix (typed_literal_remaining at :115). DO NOT TOUCH (read-only reference).
notifier.h                # DO NOT TOUCH.
pattern_match.{c,h}       # unaffected.
qmk_stubs/                # unaffected.
test_notifier_host.c      # LANDED at 79 cases (P1.M1.T2.S1). DO NOT TOUCH.
test_notifier_dispatch.c  # DO NOT TOUCH.
test_notifier_os.c        # DO NOT TOUCH.
run_notifier_stub_tests.sh# the gate — RUN to read the live count. DO NOT EDIT.
run_all_tests.sh          # unaffected. DO NOT TOUCH.
PRD.md / tasks.json / prd_snapshot.md / .gitignore  # READ-ONLY / orchestrator-owned.
```

### Desired Codebase tree with files to be changed

```bash
README.md                 # MODIFIED: test_notifier_host results bullet + coverage count token.
# (no new files; no code change)
```

### Known Gotchas of our codebase & Library Quirks

```markdown
CRITICAL — the contract's "64" is STALE; the live count is 79. P1.M1.T2.S1
  (adversarial tests) LANDED before this task, bringing test_notifier_host
  64 -> 79 (git status shows M test_notifier_host.c; the adv section is at lines
  414+). Writing "64" recreates the exact defect this task fixes. ALWAYS read the
  live count from run_notifier_stub_tests.sh before writing the number.

CRITICAL — keep the two count mentions CONSISTENT. Line 516 (coverage list) and
  the results block both currently say "(64 cases)". After this fix they must
  BOTH say the live count (79). A README that says "64 cases" in one place and
  "79/79 pass" in another is a new, worse defect (internal contradiction).

CRITICAL — the coverage PROSE (lines 517-523: QUERY_INFO/QUERY_CALLBACK/SET_OS/
  APPLY_HOST_CONTEXT/...) is ACCURATE and must NOT be changed (the contract says
  so). Only the COUNT TOKEN "(64 cases)" on line 516 changes. The adversarial
  coverage is reflected in the RESULTS BLOCK prose (not the coverage list) so the
  count bump is explained without touching the coverage description.

GOTCHA — do NOT touch lines 538-540 (matcher 2023/2023, dispatch 14/14, OS 31/31).
  Those are correct. Only the test_notifier_host bullet changes.

GOTCHA — name the resolution mechanism correctly: "length-aware typed reassembly"
  (symbol typed_literal_remaining, notifier.c:115). This is what resolves the
  SET_OS 0x03/ETX collision. Do NOT conflate it with typed_awaiting_terminator
  (the Issue 1 watchdog at notifier.c:116-123) — that is a separate fix.

GOTCHA — P1.M2.T4.S1 (LATER) owns the README overview/features broad sync. Do NOT
  expand this task into overview/features prose; keep it to the results block +
  count token. (The adversarial mention goes IN the results block, not a new
  overview section.)

GOTCHA — the line numbers (~516, ~540-545) may have drifted slightly by
  implementation time. Anchor on the TEXT ("(64 cases)", "the four `SET_OS` blocks
  are currently pending"), not the line numbers.
```

## Implementation Blueprint

### Data models and structure

None. Pure Markdown text edits.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 0: VERIFY the live host count (do this FIRST — the number is load-bearing)
  - RUN: ./run_notifier_stub_tests.sh 2>&1 | grep -A1 'notifier host fails'
         (or: look for the 'Total tests run: N' line for the host driver)
  - READ the count N (currently 79). Use N in both edits below.
  - If N != 79 (further drift), use the actual N — never a hardcoded guess.

Task 1: MODIFY README.md — REPLACE the stale results bullet (lines ~540-545)
  - LOCATE the bullet starting:
      `- \`test_notifier_host\` (64 cases): the four \`SET_OS\` blocks are currently pending`
    through the line ending `... multi-report reassembly) pass.`
    (anchor on this exact prose; line numbers may drift).
  - REPLACE with the "Exact code — results block" below (using N for the count).
  - PRESERVE: the preceding two bullets (matcher 2023/2023; dispatch 14/14 + OS
    31/31) and the following "**Performance Impact**" line — unchanged.

Task 2: MODIFY README.md — UPDATE the coverage-list count token (line ~516)
  - LOCATE: `- **\`test_notifier_host\`** (64 cases) — the typed-command / host-rules`
  - CHANGE ONLY the count token: `(64 cases)` -> `(N cases)` (N = live count, 79).
  - PRESERVE: the rest of the line and the coverage PROSE body (lines 517-523)
    UNCHANGED (it is accurate; the contract says don't touch the description).

Task 3: VERIFY (no edit) — consistency + accuracy
  - Re-read the two edited regions; confirm both counts == N (the live count).
  - Confirm matcher/dispatch/OS results untouched; coverage prose untouched.
  - Confirm no "(64 cases)" / "57 assertions" / "pending ... fix" remnants remain.
```

**Exact code — results block (Task 1).** OLD (verified verbatim, README ~L540-545):

```markdown
- `test_notifier_host` (64 cases): the four `SET_OS` blocks are currently pending
  an upstream ETX-collision framing fix in `notifier.c` (the `SET_OS` `cmd_id`
  `0x03` collides with the `ETX` terminator during reassembly); the remaining 57
  assertions (`QUERY_INFO` / `QUERY_CALLBACK` / `APPLY_HOST_CONTEXT` STACK vs
  REPLACE / callback-diff ordering / host-layer clear / legacy-typed coexistence
  / non-magic discard / multi-report reassembly) pass.
```

NEW (N = the live host count, currently **79**):

```markdown
- `test_notifier_host` (N cases): all categories pass — including the four `SET_OS`
  blocks. The `SET_OS` `cmd_id` (`0x03`) / `ETX`-terminator collision during typed
  reassembly is resolved by the length-aware typed-reassembly path in `notifier.c`
  (`typed_literal_remaining`). Coverage spans `QUERY_INFO` / `QUERY_CALLBACK` /
  `SET_OS` / `APPLY_HOST_CONTEXT` STACK vs REPLACE (`clear_board`) / callback-diff
  ordering (disable-before-enable) / host-layer clear (`0xFF`) / legacy-typed
  coexistence / non-magic discard / multi-report typed reassembly / and the
  adversarial typed-command framing gate (Issue 1 watchdog regression).
```

**Exact code — count token (Task 2).** OLD: `- **\`test_notifier_host\`** (64 cases) — the typed-command / host-rules`
NEW: `- **\`test_notifier_host\`** (N cases) — the typed-command / host-rules`
(only `(64 cases)` → `(N cases)`; the rest of the line + the prose body unchanged.)

> Replace **N** with the live count from Task 0 (currently **79**).

### Implementation Patterns & Key Details

```markdown
PATTERN: read the live count from the gate, then write it. The host count has
  already shifted once (64 -> 79 via P1.M1.T2.S1) and may shift again. A hardcoded
  number is fragile; verifying it is robust.

PATTERN: keep the two count mentions consistent. The coverage list (L516) and the
  results block (L540) both show the test_notifier_host count; they must agree.

PATTERN: explain the count bump in the results block (the "adversarial typed-
  command framing gate" phrase) so a reader who remembers "64" understands why it
  is now higher — without touching the coverage-description prose (P1.M2.T4.S1
  can expand that later).

ANTI-PATTERN: do NOT write "64" because the contract says so — the contract's 64
  predates P1.M1.T2.S1's 15 adversarial cases. Writing 64 recreates Issue 2.

ANTI-PATTERN: do NOT change the coverage PROSE (L517-523) — only the count token.
  The prose accurately describes what test_notifier_host covers.

ANTI-PATTERN: do NOT touch the matcher (2023/2023), dispatch (14/14), or OS (31/31)
  results — they are correct.

ANTI-PATTERN: do NOT conflate typed_literal_remaining (the SET_OS 0x03/ETX fix)
  with typed_awaiting_terminator (the Issue 1 watchdog). Name the former.

ANTI-PATTERN: do NOT expand into README overview/features — that is P1.M2.T4.S1.
```

### Integration Points

```yaml
README.MD:
  - edit 1: results bullet (L540-545) — stale SET_OS-pending prose -> accurate.
  - edit 2: coverage count token (L516) — (64 cases) -> (N cases).
CONSISTENCY:
  - L516 count == L540 count == run_notifier_stub_tests.sh host count (N).
DOWNSTREAM (NOT this task):
  - P1.M2.T4.S1: README overview/features broad sync (watchdog, NFA-128 suite,
    has_been_queried). This task does not touch those.
BUILD/CONFIG/ROUTES/DATABASE:
  - none. Pure Markdown documentation.
```

## Validation Loop

> No compiler/tests are affected (Markdown-only). Validation is: (1) the gate
> confirms the live count, (2) the edited README is internally consistent and
> free of stale phrases, (3) no source/build change. All commands were sanity-run
> during research.

### Level 1: Verify the live count (the load-bearing number)

```bash
cd /home/dustin/projects/qmk-notifier

# Run the gate and read the host count N. This is the number to write.
./run_notifier_stub_tests.sh 2>&1 | grep -E 'Total tests run|notifier host fails'
# Expected: a 'Total tests run: N / passed: N / failed: 0' line for the host
# driver (currently N=79), and 'notifier host fails=0 (exit=0)'.
# Record N. Use it in both README edits.
```

### Level 2: Apply the edits + check consistency

```bash
cd /home/dustin/projects/qmk-notifier

# (Apply Task 1 + Task 2 with the edit tool, using N from Level 1.)

# 2a. No stale remnants remain.
! grep -qE 'currently pending|57 assertions|ETX-collision framing fix' README.md \
  && echo "OK: stale SET_OS-pending phrases removed" \
  || echo "FAIL: stale phrase still present"
! grep -q '(64 cases)' README.md \
  && echo "OK: no '(64 cases)' remains" \
  || echo "FAIL: a '(64 cases)' token remains"

# 2b. The two count mentions are equal and match the live gate.
echo "count mentions in README (expect 2, both == N):"
grep -oE 'test_notifier_host.{0,20}\([0-9]+ cases\)' README.md
# Manually confirm both show N (the Level-1 count).

# 2c. The resolution mechanism is named correctly.
grep -q 'length-aware typed-reassembly' README.md && echo "OK: cites length-aware typed-reassembly"
grep -q 'typed_literal_remaining' README.md && echo "OK: cites the symbol"

# 2d. Unchanged results are intact.
grep -q '2023/2023' README.md && echo "OK: matcher 2023/2023 intact"
grep -q '14/14' README.md && grep -q '31/31' README.md && echo "OK: dispatch/os intact"
```

### Level 3: Integration Testing (no regression / no scope creep)

```bash
cd /home/dustin/projects/qmk-notifier

# Only README.md changed in source (besides plan/ artifacts).
git status --porcelain | grep -vE 'plan/003_16d737de7a3e/bugfix/001_9bb28e448bd3/P1M2T1S1/' \
  | grep -E 'README\.md|\.c|\.h|\.sh|\.mk' | grep -v '^M  plan/' || true
git diff --stat -- README.md
# Expected: ONLY README.md listed (1 file changed); nothing else in source.

# The diff is confined to the two regions (results block + count token).
git diff -- README.md
# Expected: hunks ONLY around the test_notifier_host results bullet and the
# coverage count token. No matcher/dispatch/OS/overview/features change.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. A reader correctly understands SET_OS is functional.
grep -q 'all categories pass — including the four `SET_OS` blocks' README.md \
  && echo "reader-facing: SET_OS is functional (good)" \
  || echo "WARN: SET_OS-functional statement missing"

# 4b. The count bump is explained (adversarial mention in the results block).
grep -q 'adversarial typed-command framing gate' README.md \
  && echo "count bump explained (good)" || echo "WARN: adversarial mention missing"

# 4c. The coverage-description prose is UNCHANGED (only the count token moved).
git diff -- README.md | grep -E '^[+-].*QUERY_INFO|^[+-].*APPLY_HOST_CONTEXT|^[+-].*multi-report' \
  && echo "WARN: coverage prose was changed (should be untouched)" \
  || echo "OK: coverage prose unchanged (only count token + results block)"
# Expected: "OK: coverage prose unchanged".

# 4d. Live count and README count agree (the authoritative cross-check).
N=$(./run_notifier_stub_tests.sh 2>&1 | grep 'Total tests run' | tail -1 | grep -oE '[0-9]+' | head -1)
echo "live host count = $N"
grep -q "($N cases)" README.md && echo "README count == live count (good)" \
  || echo "FAIL: README count != live count"
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: live host count N read from `run_notifier_stub_tests.sh` (currently 79).
- [ ] Level 2: no stale phrases ("currently pending", "57 assertions", "ETX-collision framing fix", "(64 cases)") remain.
- [ ] Level 2: both count mentions == N; resolution mechanism named (typed_literal_remaining).
- [ ] Level 2: matcher 2023/2023 + dispatch 14/14 + OS 31/31 results intact.
- [ ] Level 3: only README.md changed; diff confined to the two regions.
- [ ] Level 4: reader-facing "SET_OS functional" statement present; count bump explained; coverage prose untouched; README count == live count.

### Feature Validation

- [ ] The results block accurately states all host cases pass, including SET_OS, no pending fixes.
- [ ] The README is internally consistent (coverage count == results count == live count).
- [ ] A reader correctly understands SET_OS is functional.

### Code Quality Validation

- [ ] Only Markdown text changed; no code/build/wire/protocol change.
- [ ] Edits confined to the two regions (results bullet + count token).
- [ ] No anti-patterns (see below): no hardcoded "64", no coverage-prose change, no overview/features expansion.

### Documentation & Deployment

- [ ] This IS the documentation fix (item §5: no separate docs subtask).
- [ ] The adversarial count bump is explained in the results block (not a silent jump).
- [ ] P1.M2.T4.S1 (overview/features broad sync) is left to do its own sweep.

---

## Anti-Patterns to Avoid

- ❌ Don't write "64" because the contract literally says so — P1.M1.T2.S1 (LANDED) brought the count to 79; writing 64 recreates Issue 2. Always verify the live count first.
- ❌ Don't leave the two count mentions inconsistent (line 516 vs results block). They must both equal the live count.
- ❌ Don't change the coverage PROSE (lines 517-523) — only the count token. The prose is accurate (contract: don't touch the coverage description).
- ❌ Don't touch the matcher (2023/2023), dispatch (14/14), or OS (31/31) results — they are correct.
- ❌ Don't conflate `typed_literal_remaining` (the SET_OS 0x03/ETX fix) with `typed_awaiting_terminator` (the Issue 1 watchdog). Name the former as the SET_OS resolution.
- ❌ Don't expand into README overview/features — that is P1.M2.T4.S1's scope. Keep edits to the results block + count token.
- ❌ Don't edit notifier.c, notifier.h, test_notifier_host.c, run_notifier_stub_tests.sh, or any code/build file. Only README.md.
- ❌ Don't hardcode the count without running the gate — read N from `run_notifier_stub_tests.sh` and use it.

---

## ⚠️ Deviation note (for the human)

The item contract specifies "64/64 host cases passing." That number is stale: the
**parallel task P1.M1.T2.S1 (adversarial typed-command tests) is LANDED** and grew
`test_notifier_host` from 64 → **79** cases (verified: `git status` shows
`M test_notifier_host.c`; the adv-A..adv-D section is present; the live gate reports
`Total tests run: 79 / passed: 79 / failed: 0`). P1.M1.T2.S1 does not touch README.
Writing "64" would recreate the exact defect this task exists to fix (a stale, wrong
count). Per the parallel-execution mandate to consume the previous PRP's outputs,
this PRP writes the **live count (79)** and instructs the implementer to verify the
count at edit time. This is the faithful interpretation of the task's actual goal
("a reader will correctly understand SET_OS is functional" with an accurate,
internally-consistent README).

## Confidence Score: 10/10

The deliverable is two surgical Markdown text edits to `README.md` (the stale
SET_OS-pending results bullet, and the coverage-list count token), whose exact OLD
text (verified verbatim at lines 516 and 540-545) and exact NEW text are specified
inline. The live gate count (**79/79**) was **empirically verified** (ran
`run_notifier_stub_tests.sh`, stable across runs). The resolution mechanism
(`typed_literal_remaining`, notifier.c:115) was confirmed by grep. The cross-task
state was verified: P1.M1.T2.S1 is LANDED (count 64→79, README untouched) and
P1.M2.T4.S1 owns the later overview/features broad sync (clean division). The one
deviation from the literal contract (writing 79, not the stale "64") is documented
prominently and is the correct call (writing 64 would re-create Issue 2). No
external dependencies; no code/build/wire change.