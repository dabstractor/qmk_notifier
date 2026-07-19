# PRP — P1.M1.T1.S1: Add @-literal regression test cases to `test_pattern_match.c`

## Goal

**Feature Goal**: Add four data-driven regression test cases to
`test_pattern_match.c` that lock in the *correct* semantics of a literal `@`
between `\w+` groups in anchored patterns — preventing a future "rebuild to the
PRD spec" from regressing the matcher toward the (wrong) PRD §11.2C expectation.

**Deliverable**: The modified file `test_pattern_match.c` (4 new rows added to
one existing `test_case_t[]` array, no other structural change). After rebuild it
reports **380 tests, 0 failures** (was 376/0).

**Success Definition**:
- `gcc -o test_pattern_match test_pattern_match.c pattern_match.c && ./test_pattern_match`
  prints `Total tests run: 380`, `Tests failed: 0`, exit code 0.
- The critical regression guard is present and passing:
  `pattern_match("^\\w+@\\w+$","user_host",1)` is asserted to be `false`.
- `pattern_match.c` is **untouched** (the matcher is correct — this is a
  test-only change).
- `run_all_tests.sh` aggregate still shows 0 failures (it auto-recompiles this file).

## User Persona (if applicable)

**Target User**: Future maintainers / AI agents rebuilding the matcher "to spec",
and the maintainer running the §11.2C / Definition-of-Done gate.

**Use Case**: A future rebuild reads the (now-corrected) PRD and re-implements
`\w`/literal semantics; this test fails loudly if anyone makes `^\w+@\w+$` match
`user_host` (the original typo'd expectation), forcing them to keep `@` a literal
and `\w = [A-Za-z0-9_]`.

**Pain Points Addressed**: Closes the regression-risk gap identified in Issue 1
(the PRD §11.2C string was `"user_host"` but the pattern needs a literal `@`).
The PRD doc was already corrected; this test is the actionable code-side lock-in.

## Why

- The matcher is correct; the bug was in the PRD §11.2C example string (already
  fixed in PRD.md). Without a regression test, nothing in the automated suite
  asserts the *correct* `@`-literal behavior, so a future rebuild could
  reintroduce the wrong expectation and silently break literal-`@` matching.
- This is the lowest-risk, highest-value fix: 4 new assertions, no production
  code change, no API/build/config surface change.
- Keeps the existing data-driven test framework (no new harness needed).

## What

Add exactly four `test_case_t` rows to the existing
`metachar_anchor_tests[]` array inside `test_metacharacters_with_anchors()`,
immediately before its closing `};`. The four rows (exact C source, expected
values empirically verified against `pattern_match.c`):

```c
// @-literal regression guard (Issue 1): @ is an ordinary literal byte (§7.1/§7.7);
// \w = [A-Za-z0-9_] does NOT include @ (§15). Lock in the CORRECT semantics so a
// future "rebuild to spec" cannot regress toward the old wrong §11.2C expectation.
{"^\\w+@\\w+$", "user@host", true, true,  "REG: ^\\w+@\\w+$ matches user@host (literal @ between word groups)"},
{"^\\w+@\\w+$", "user_host", true, false, "REG: ^\\w+@\\w+$ does NOT match user_host (no @; _ is \\w)"},
{"^\\w+_\\w+$", "user_host", true, true,  "REG: ^\\w+_\\w+$ matches user_host (literal _ between word groups)"},
{"\\w+@\\w+",   "user@host", true, true,  "REG: unanchored \\w+@\\w+ matches (substring)"},
```

The `user_host → false` row (2nd) is the **critical regression guard**.

### Success Criteria

- [ ] 4 rows added at the specified insertion point; existing rows untouched.
- [ ] Rebuild reports 380 tests, 0 failures, exit 0.
- [ ] `pattern_match.c` byte-identical to before (`git diff --stat` shows only
  `test_pattern_match.c`).
- [ ] `run_all_tests.sh` aggregate: 0 failures.

## All Needed Context

### Context Completeness Check

_Pass_: The exact 4 rows (with verified expected values), the exact insertion
function/array/anchor line, the build+run command, and the expected totals are
all specified below. An implementer with only this PRP + the repo can make the
single edit and validate it with no guessing.

### Documentation & References

```yaml
# MUST READ — the issue + correct semantics
- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/architecture/system_context.md
  section: "## Major Issues (Should Fix) → Issue 1"
  why: "Establishes @ is a literal byte, \\w excludes @, the matcher is correct,
        and the fix is a regression test (NOT a pattern_match.c change)."
  critical: "DO NOT modify pattern_match.c — that would break literal-@ matching."

- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/architecture/test_infrastructure.md
  why: "Documents the test_case_t data-driven harness and auto-recompile behavior."
  critical: "run_all_tests.sh recompiles test_pattern_match.c — no script change needed."

- file: test_pattern_match.c
  section: "static void test_metacharacters_with_anchors()  (lines 491-567)"
  why: "The target array (metachar_anchor_tests[]) already covers \\w-with-anchors
        and mixed literal+metachar cases — the natural home for the @-literal rows."
  pattern: "Each row: {pattern, input, case_sensitive, expected_result, description};
            a trailing for-loop calls run_test() which tallies pass/fail."
  gotcha: "Insert BEFORE the array's closing }; — after the last existing row
           ({\"^x\\\\sy$\",\"x y\",...}). No dispatcher change: the section fn is
           already called from run_pattern_match_tests()."

# Behavior reference (not modified, for the expected-value rationale)
- file: PRD.md  (repo root)
  section: "## 15. Appendix A — Pattern-Semantics Reference Table"
  why: "\\w = [A-Za-z0-9_]; @ is an ordinary literal. Confirms the 4 expectations."
  gotcha: "PRD §11.2C doc string was ALREADY corrected to \"user@host\"; this task
           adds the test, it does not touch the PRD."
```

### Current Codebase tree (relevant slice)

```bash
test_pattern_match.c   # ← MODIFY (add 4 rows in one array). Currently 843 lines, 376 tests.
pattern_match.c        # DO NOT TOUCH (matcher is correct)
pattern_match.h        # unaffected
run_all_tests.sh       # auto-recompiles test_pattern_match.c — no change needed
PRD.md                 # READ-ONLY (§11.2C already corrected)
plan/002_c243e735980a/bugfix/001_4a8e381fbf10/  # this bugfix plan
```

### Desired Codebase tree with files to be added/changed

```bash
test_pattern_match.c   # MODIFIED: +4 rows in metachar_anchor_tests[] (+ comment block)
# (no new files; no other file changes)
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL: pattern_match.c is CORRECT. Do NOT change it to make ^\w+@\w+$ match
//   user_host. @ is a literal byte (PRD §7.1/§7.7); \w=[A-Za-z0-9_] (§15) excludes @.
//   The only deliverable here is 4 new test rows asserting the correct values.

// GOTCHA (C-string escaping): write "^\\w+@\\w+$" in source → runtime string
//   ^\w+@\w+$  (\\w → \w; the + is the one-or-more quantifier, NOT escaped; @ literal).
//   This matches existing file style e.g. {"^\\w$", "_", ...}, {"^x\\sy$", "x y", ...}.

// GOTCHA (no dispatch change): test_metacharacters_with_anchors() is ALREADY called
//   from run_pattern_match_tests(); its for-loop + run_test() counter pick up new rows
//   automatically. Do not add a new section function or a new call.

// GOTCHA (totals): 376 existing + 4 new = 380. The other 8 suites are unaffected.
//   run_all_tests.sh recompiles this file itself (grep its gcc lines).

// GOTCHA (the guard): the 2nd row (user_host → false) is the load-bearing assertion.
//   If it ever flips to true, the matcher has been regressed — fail the build.
```

## Implementation Blueprint

### Data models and structure

No new types. Reuse the existing `test_case_t` (lines 8-14) and `run_test()`
(lines 25-36). The new rows are plain struct-literal initializers in an existing
array.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY test_pattern_match.c — add 4 rows to metachar_anchor_tests[]
  - LOCATE: function `test_metacharacters_with_anchors()` (grep -n it; ~line 491).
  - LOCATE within it: the `test_case_t metachar_anchor_tests[] = { … };` array,
    whose LAST existing row is:
        {"^x\\sy$", "x y", true, true, "Mixed literal + metachar + literal with full anchor"},
    immediately followed by the closing  };
  - INSERT directly BEFORE that closing `};`:
      (1) a // comment block (see "Exact insertion block" below);
      (2) the 4 rows from the "What" section, verbatim.
  - NAMING/ORDER: keep the REG: description prefix as given (matches item contract);
    place the 4 rows as a contiguous trailing block after the existing rows.
  - PRESERVE: all existing rows, all other section functions, the dispatcher, main.
  - DO NOT: touch pattern_match.c, run_all_tests.sh, any other test_*.c, PRD.md.
  - DEPENDENCIES: none new — run_test()/pattern_match() already in scope.
  - PLACEMENT: single contiguous edit in one array of one function.
```

**Exact insertion block (copy verbatim, before the `};`):**

```c
        
        // @-literal regression guard (Issue 1): @ is an ordinary literal byte (§7.1/§7.7);
        // \w = [A-Za-z0-9_] does NOT include @ (§15). Lock in the CORRECT semantics so a
        // future "rebuild to spec" cannot regress toward the old wrong §11.2C expectation.
        {"^\\w+@\\w+$", "user@host", true, true,  "REG: ^\\w+@\\w+$ matches user@host (literal @ between word groups)"},
        {"^\\w+@\\w+$", "user_host", true, false, "REG: ^\\w+@\\w+$ does NOT match user_host (no @; _ is \\w)"},
        {"^\\w+_\\w+$", "user_host", true, true,  "REG: ^\\w+_\\w+$ matches user_host (literal _ between word groups)"},
        {"\\w+@\\w+",   "user@host", true, true,  "REG: unanchored \\w+@\\w+ matches (substring)"},
```

> The leading blank line + 8-space indent match the surrounding array style.
> If the editor auto-strips the leading blank line that is fine; the 4 rows +
> comment are what matter.

### Implementation Patterns & Key Details

```c
// The edit is purely additive — one new comment block + 4 struct-literal rows
// appended inside an existing test_case_t[] array. The harness does the rest:
//   for (i=0; i<sizeof(arr)/sizeof(arr[0]); i++) run_test(arr[i]);
// sizeof grows by 4 → loop runs 4 more times → tests_run goes 376→380.

// ANTI-PATTERN: do NOT "also fix" pattern_match.c. The matcher is correct.
// ANTI-PATTERN: do NOT add a brand-new section function or dispatcher call —
//   the target section is already wired up; just extend its array.
// ANTI-PATTERN: do NOT change the description strings' REG: prefix or wording —
//   the item contract specifies them exactly (they aid grep-based auditing).
```

### Integration Points

```yaml
BUILD:
  - none changed. Existing gcc line:
    "gcc -o test_pattern_match test_pattern_match.c pattern_match.c"
  - run_all_tests.sh contains that line and recompiles automatically.
CONFIG/ROUTES/DATABASE:
  - none (test-only; no API/config/runtime surface).
DOCS:
  - none here. README.md test-count sync is a SEPARATE task (P1.M3.T2.S1, Mode B).
```

## Validation Loop

> Toolchain: gcc (no ruff/mypy/pytest — C project). Validate by build + run.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# Compile with the project's ACTUAL build command (plain gcc — this is what
# run_all_tests.sh uses; it does NOT pass -Wall/-Wextra).
gcc -o test_pattern_match test_pattern_match.c pattern_match.c
echo "compile exit=$?   (expect 0)"

# NOTE on -Wall -Wextra: if you DO add those flags, the EXISTING file already
# floods ~16 pre-existing `-Wsign-compare` warnings from its idiom
# `for (int i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)`. Those are NOT caused by
# this task and are out of scope to fix. Do NOT "fix" them — judge success by
# exit code 0 + the Level 2 totals, not by a warning-free -Wextra build.

# Confirm the 4 new rows landed in the right array (4 REG: descriptions present).
grep -c 'REG: ' test_pattern_match.c          # expect: 4
grep -c 'user_host' test_pattern_match.c      # expect: >=2 (the false-guard + the \w+_\w+ row)
grep -n 'literal @ between word groups' test_pattern_match.c
# Expected: compile exit 0; grep counts 4 and >=2; the literal-@ line prints its location.
```

### Level 2: Unit Tests (Component Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# Build + run the suite directly.
gcc -o test_pattern_match test_pattern_match.c pattern_match.c
./test_pattern_match > /tmp/tpm.out 2>&1; echo "exit=$?"
grep -E 'Total tests run|Tests passed|Tests failed|All tests PASSED|Some tests FAILED' /tmp/tpm.out
# Expected: exit 0; "Total tests run: 380"; "Tests passed: 380"; "Tests failed: 0";
#           "All tests PASSED! ✓".

# Confirm the critical regression guard specifically ran and passed.
grep -F 'REG: ^\w+@\w+$ does NOT match user_host' /tmp/tpm.out | grep -q '^PASS:' \
  && echo "CRITICAL GUARD PASS" || echo "CRITICAL GUARD MISSING/FAIL"
rm -f /tmp/tpm.out
# Expected: "CRITICAL GUARD PASS".
```

### Level 3: Integration Testing (System Validation)

```bash
cd /home/dustin/projects/qmk-notifier

# Aggregate gate: run_all_tests.sh recompiles test_pattern_match.c and runs all 9 suites.
./run_all_tests.sh > /tmp/all.out 2>&1; echo "exit=$?"
tail -n 30 /tmp/all.out
# Expected: exit 0; aggregate 0 failures; test_pattern_match suite shows 380/380.
rm -f /tmp/all.out
# NOTE: other suites' totals are unchanged by this task.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# Regression-intent check: assert pattern_match.c was NOT modified (the matcher is correct).
git diff --stat -- pattern_match.c pattern_match.h
# Expected: empty (no changes to the matcher).

# Confirm only test_pattern_match.c changed in source (besides plan/ artifacts).
git diff --stat -- ':!plan'
# Expected: only `test_pattern_match.c` listed.

# (Optional) Re-derive the 4 expected values independently to be sure they match reality:
cat > /tmp/at_probe.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  printf("a=%d(want1) b=%d(want0) c=%d(want1) d=%d(want1)\n",
    pattern_match("^\\w+@\\w+$","user@host",1),
    pattern_match("^\\w+@\\w+$","user_host",1),
    pattern_match("^\\w+_\\w+$","user_host",1),
    pattern_match("\\w+@\\w+","user@host",1));
  return 0;
}
EOF
gcc -w /tmp/at_probe.c pattern_match.c -I. -o /tmp/at_probe && /tmp/at_probe
rm -f /tmp/at_probe /tmp/at_probe.c
# Expected: a=1 b=0 c=1 d=1  (matches the 4 expected_result booleans in the new rows).
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: plain `gcc` build succeeds (exit 0); 4 `REG: ` rows + `user_host` present. (Pre-existing `-Wsign-compare` warnings under `-Wextra` are unrelated/out-of-scope.)
- [ ] Level 2: suite reports `Total tests run: 380`, `Tests failed: 0`, exit 0.
- [ ] Level 2: critical guard (`user_host → false`) prints PASS.
- [ ] Level 3: `run_all_tests.sh` aggregate exit 0, 0 failures, this suite 380/380.
- [ ] Level 4: `pattern_match.c`/`.h` unchanged; only `test_pattern_match.c` modified.

### Feature Validation

- [ ] 4 rows added at the exact insertion point (end of `metachar_anchor_tests[]`).
- [ ] Expected values match the empirically verified matcher output (1,0,1,1).
- [ ] `user_host → false` regression guard present and passing.
- [ ] No production code, build, config, or API surface changed.

### Code Quality Validation

- [ ] Follows existing `test_case_t` / `run_test()` data-driven pattern.
- [ ] Indentation/comment style matches surrounding array rows.
- [ ] Description strings keep the `REG: ` prefix per the item contract.
- [ ] No modification to pattern_match.c, run_all_tests.sh, PRD.md, tasks.json.

### Documentation & Deployment

- [ ] Inline `//` comment explains the @-literal / `\w` rationale (Mode: test-only).
- [ ] README test-count sync is explicitly deferred to P1.M3.T2.S1 (not done here).

---

## Anti-Patterns to Avoid

- ❌ Don't modify `pattern_match.c` — the matcher is correct (Issue 1 is a spec defect).
- ❌ Don't add a new section function or dispatcher call — extend the existing array.
- ❌ Don't change the expected values to match the old wrong §11.2C (`user_host`→true).
- ❌ Don't alter the `REG: ` description strings — they are the audit/grep contract.
- ❌ Don't touch `run_all_tests.sh` — it already recompiles this file.
- ❌ Don't update README.md here — that's a separate changeset-level docs task.
- ❌ Don't reformat existing rows or other sections — additive, surgical edit only.

---

## Confidence Score: 10/10

A single, surgical, additive edit: 4 `test_case_t` rows appended to one existing
array whose dispatcher is already wired. All four `expected_result` values were
**empirically verified** by building and running a probe against `pattern_match.c`
(1, 0, 1, 1 — including the critical `user_host → false` guard). The current total
(376, all passing) was confirmed by running the binary; 376 + 4 = 380. No
production code, build system, or config is touched. The only risk — accidentally
modifying the correct matcher — is explicitly fenced off throughout.