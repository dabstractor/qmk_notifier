name: "P3.M1 — Acceptance Gate & Documentation (qmk-notifier)"
description: |
  Validate the full acceptance gate (PRD §11.2A/B/C + invariants §13, no warnings)
  and sync the README.md to the current codebase state. This is the FINAL milestone
  of the rebuild: the matcher/firmware/test corpus are built (P1, P2 complete);
  P3 confirms they pass the acceptance gate and updates the one stale doc
  (README.md) so the repo's user-facing documentation matches reality.
  Gate is currently GREEN (2019/2019, 0 failures). P3 is read-only-validate +
  README-edit only.

---

## Goal

**Feature Goal**: A fully-green acceptance gate (all 9 host test suites at 0
failures, the pathological NFA stress < 50 ms, the realistic-patterns check
returning six `1`s, the notifier stub-compile gate at exit 0, and no new
compiler warnings) — **AND** a `README.md` that accurately reflects the current
codebase: live test counts, the complete pattern syntax, the correct
GS_DELIMITER naming, accurate companion-project roles, and a PRD-§10.1-aligned
setup section.

**Deliverable**:
1. **Captured acceptance-gate evidence** — the gate itself must pass (it is
   infrastructure; no new gate files are authored by P3). The implementer RUNS
   the gate and records the live numbers it prints.
2. **An updated `README.md`** (repo root) — the single file P3 is permitted to
   modify — with every stale figure corrected and every missing construct added.

**Success Definition** (all must hold simultaneously):
- `./run_all_tests.sh` exits 0 and prints `Total tests failed: 0` with an
  aggregate total equal to what the run actually reports (currently **2019**,
  not a hard-coded number).
- 11.2B pathological stress prints `result=0` in **< 50 ms** (measured ~1.8 ms).
- 11.2C realistic patterns print **six `1`s** when test #3 uses input
  `user@host` (the DRIFT-2 correction; the PRD's literal `user_host` correctly
  returns `0` and is a known spec drift — do not "fix" the code).
- `./run_notifier_stub_tests.sh` exits 0 (`notifier dispatch fails=0`).
- `pattern_match.c` and `notifier.c` (stub-compiled) are clean under
  `-Wall -Wextra -std=c99`.
- `README.md` contains **none** of the stale tokens (`1,992`, `2,048`, `97.2%`,
  `0.087`, `383 tests`, `263 tests`, `65/65`, `backward_compatibility_report.md`)
  and **does** document the full §15 construct set, GS_DELIMITER = ASCII 29
  (Group Separator, `0x1D`), and the live aggregate test count.

## User Persona

**Target User**: A QMK keymap author who pulls this repo as a submodule and a
future maintainer/reviewer who reads the README to understand what the module
does and how to trust/test it.

**Use Case**: The user reads the README to integrate the module and to see
whether the test suite is green; a reviewer cross-checks README claims against
`./run_all_tests.sh` output.

**Pain Points Addressed**: The current README states passing figures from an
old codebase state (`1,992/2,048`, `97.2%`), references a non-existent
`backward_compatibility_report.md`, documents only 3 of ~13 pattern constructs,
and never names the wire delimiter — so it actively misleads.

## Why

- **Trust**: A repo whose README advertises a 97.2% pass rate and a phantom
  report file erodes confidence in an otherwise fully-green gate.
- **Discoverability**: The regex features (`\d \w \s \b . +`, escapes, anchors)
  are implemented and tested but invisible to README readers.
- **Ecosystem accuracy**: PRD §1.2 splits the desktop side into `QMKonnect`
  (daemon) + `qmk_notifier` (transport crate); the README still conflates them.
- **Closes the rebuild**: This is the last milestone. Once the gate is validated
  and the README synced, the one-shot rebuild is "Done" per the PRD's
  Definition of Done (§17).

## What

Run the complete acceptance gate exactly as specified in PRD §11.2 (A/B/C),
verify the §13 invariants hold and no new warnings appear, then rewrite the
stale portions of `README.md` so it matches the verified reality. No source
code, test, or build-script file is touched — only `README.md`.

### Success Criteria

- [ ] `./run_all_tests.sh` → exit 0, `Total tests failed: 0`, aggregate total
      = the live printed number (currently 2019).
- [ ] Every one of the 9 suites reports `fails=0` (11.2A per-suite loop).
- [ ] 11.2B `/tmp/nfa_stress`: `result=0`, `< 50 ms` (measured ~1.8 ms).
- [ ] 11.2C `/tmp/nfa_real`: six `1`s (input `user@host` for test #3).
- [ ] `./run_notifier_stub_tests.sh`: exit 0, `notifier dispatch fails=0`.
- [ ] `pattern_match.c` / `notifier.c` (stub) compile clean with
      `-Wall -Wextra -std=c99` (11.2D, no new warnings).
- [ ] PRD §13 invariants spot-checked (magic header `0x81 0x9F`; GS `0x1D`;
      ETX `0x03`; `RAW_REPORT_SIZE=32`; `LAYER_UNSET=255`).
- [ ] `README.md` has **zero** stale tokens (see Goal) and documents the full
      construct set, GS_DELIMITER, live counts, and corrected companion projects.

## All Needed Context

### Context Completeness Check

_Pass_: A developer with no prior knowledge of this repo can (a) run the gate
from the exact commands below, (b) read the live numbers, and (c) edit the six
named README sections using the PRD cross-references given. No inference about
the matcher internals is required — this is run + document.

### Documentation & References

```yaml
# MUST READ — the contract this PRP enforces
- docfile: PRD.md  # repo root (the merged spec; see also plan/.../prd_snapshot.md)
  why: Source of truth for the acceptance gate (§11.2A/B/C), test inventory
       (§11.3), pattern semantics (§15), constants (§16), setup (§10.1), and
       ecosystem roles (§1.2). NOTE: PRD §11.3's "1826" total is itself stale —
       the PRD says the LIVE `./run_all_tests.sh` totals are authoritative.
  sections:
    - §11.1 exact gcc build flags for the 9 suites (copy/paste)
    - §11.2A per-suite fails=0 loop
    - §11.2B pathological NFA stress (a+a+a+...+b vs 199×a, <50ms)
    - §11.2C realistic patterns (6×'1' — but see DRIFT-2)
    - §11.3 test inventory table
    - §13 key invariants to spot-check
    - §15 Appendix A construct reference table (for README pattern syntax)
    - §16 Appendix B constants (GS=0x1D, ETX=0x03, RAW_REPORT_SIZE=32)
    - §10.1 user integration steps (for README Setup section)
    - §1.2 ecosystem table (QMKonnect vs qmk_notifier)

- file: README.md   # repo root — THE file this PRP modifies
  why: The sole deliverable edit. Six sections are stale (see Implementation
       Tasks T2). All stale tokens must go; all missing constructs must be added.

- file: run_all_tests.sh   # builds + runs 9 suites + perf micro-benchmark
  why: The 11.2A gate runner. Prints "Total tests run across all suites: N" and
       "Overall success rate: X%". Its printed N (currently 2019) is the number
       the README must state — do NOT hard-code PRD's 1826.

- file: run_notifier_stub_tests.sh   # P2 RISK-1 closure gate
  why: Compiles notifier.c against qmk_stubs/ and runs test_notifier_dispatch.c
       (11 cases: F4 delimiter matching, dispatcher ordering, hid_notify
       reassembly, sanitize, ack, NULL safety). Shipped infra to mention in README.

- file: plan/001_e329fbe4ae4d/P3M1/research/notes.md   # THIS PRP's research
  why: Live-measured gate results + exact README defect inventory with line refs.

- file: plan/001_e329fbe4ae4d/architecture/findings_and_risks.md
  why: DRIFT-2 (PRD §11.2C test #3 annotation wrong), DRIFT-1 (now fixed), BUG-1
       (now fixed), NFA/QMK validation. Confirms code is correct; only docs lag.
```

### Current Codebase tree

```bash
qmk-notifier/
├── notifier.{c,h}            # P2 — receiver/reassembler/dispatcher (DONE)
├── pattern_match.{c,h}       # P1 — Thompson-NFA matcher (DONE)
├── rules.mk                  # P2 — 2-line QMK integration (DONE)
├── run_all_tests.sh          # 11.2A gate runner (9 suites + perf)
├── run_notifier_stub_tests.sh# notifier.c host-side gate (RISK-1 closure)
├── test_*.c                  # 9 host test programs + test_notifier_dispatch.c
├── qmk_stubs/                # minimal QMK stubs for host-side notifier tests
├── README.md                 # ← P3.M1 MODIFIES THIS (sole edit target)
├── PRD.md                    # READ-ONLY spec (do not edit)
└── plan/001_e329fbe4ae4d/    # orchestrator-owned (do not edit)
```

### Desired Codebase tree with file responsibilities

```bash
# No new files are created by P3.M1. The ONLY modified file:
README.md   # Updated: live test counts, full pattern syntax, GS_DELIMITER,
            # corrected companion projects, PRD-§10.1-aligned setup.
# Everything else is READ-ONLY / infrastructure that the gate exercises.
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — LIVE COUNT IS 2019, NOT 1826.
// PRD §11.3 lists "~1 826 assertions" but then says the LIVE run_all_tests.sh
// totals are authoritative. The live aggregate today is 2019 (0 failures).
// The README MUST state the live number it actually measures, NOT 1826.

// CRITICAL — DRIFT-2: PRD §11.2C test #3 annotation is WRONG.
// pattern_match("^\\w+@\\w+$", "user_host", 1) returns 0 (correct — no '@').
// The PRD's "/* 1 */" annotation is a spec error. The acceptance gate 11.2C
// MUST use input "user@host" (which returns 1). Do NOT change code to match
// the wrong annotation.

// QUIRK — per-suite summary formats differ (run_all_tests.sh parses both):
//   test_pattern_match/char_classification/error_handling/memory_stress/
//   invalid_patterns  -> "Total tests run: N"
//   test_word_boundary_basic/integration                  -> "Tests run: N"
//   test_metachar_verification                            -> smoke (no number)
//   test_comprehensive_integration                        -> "Test categories passed: 10"
// The aggregate (2019) = sum of the numeric "Total/Tests run:" lines.

// QUIRK — two test contexts exist:
//   ./run_all_tests.sh         -> 9 suites linking ONLY pattern_match.c
//   ./run_notifier_stub_tests.sh -> notifier.c + qmk_stubs (closes RISK-1)
// Both are part of the shipped acceptance surface; document both in README.

// SCOPE GUARD — P3 may edit ONLY README.md. The gate is green; do not touch
// pattern_match.{c,h}, notifier.{c,h}, rules.mk, test_*.c, run_all_tests.sh,
// PRD.md, or anything under plan/. If any gate is red, HALT and escalate
// (do NOT edit code, do NOT write green numbers into a red README).
```

## Implementation Blueprint

### Data models and structure

_None._ This milestone touches no data models — it validates existing code and
rewrites prose/markdown documentation.

### Implementation Tasks (ordered by dependencies)

```yaml
# ============================================================
# TASK GROUP T1 — VALIDATE THE ACCEPTANCE GATE (read-only)
# Do this FIRST so the live numbers are known before editing the README.
# ============================================================

Task T1.A: RUN the 9-suite gate (PRD §11.2A)
  - COMMAND: ./run_all_tests.sh   (from repo root)
  - VERIFY:  exit code == 0
  - CAPTURE: the "Total tests run across all suites: N" line (N is currently 2019)
             and "Overall success rate: 100.0%"
  - THEN run the PRD §11.2A per-suite fails loop and confirm fails=0 for ALL 9:
      for t in test_pattern_match test_char_classification test_word_boundary_basic \
               test_word_boundary_integration test_metachar_verification \
               test_comprehensive_integration test_error_handling test_memory_stress \
               test_invalid_patterns; do
        printf "%-36s fails=%s\n" "$t" "$(./$t 2>&1 | grep -c '^FAIL:')"
      done
    # every line must read fails=0
  - NOTE: binaries must be rebuilt fresh (run_all_tests.sh rebuilds them); if
          running a suite standalone, use the exact PRD §11.1 flags per suite
          (test_comprehensive_integration needs -std=c99 -DNOTIFIER_STUB;
           test_error_handling/memory_stress/invalid_patterns need -I.).

Task T1.B: RUN the pathological NFA stress (PRD §11.2B)
  - BUILD: gcc -O2 -w /tmp/nfa_stress.c pattern_match.c -I. -o /tmp/nfa_stress
           (source: the §11.2B heredoc — pattern a+a+a+a+a+a+a+a+a+a+b, input 199×'a')
  - RUN:   timeout 5 /tmp/nfa_stress
  - VERIFY: prints "result=0" AND time "< 50 ms" (measured ~1.8 ms / ~1800 us)

Task T1.C: RUN the realistic-patterns check (PRD §11.2C, DRIFT-2-corrected)
  - BUILD: gcc -w /tmp/nfa_real.c pattern_match.c -I. -o /tmp/nfa_real
           (source: the §11.2C heredoc, BUT test #3 uses input "user@host")
  - RUN:   /tmp/nfa_real
  - VERIFY: prints SIX lines, each "1"
  - DRIFT-2: the PRD's literal input "user_host" correctly returns 0 (no '@');
             the gate uses "user@host" which returns 1. Record this drift.

Task T1.D: RUN the notifier stub gate (RISK-1 closure)
  - COMMAND: ./run_notifier_stub_tests.sh
  - VERIFY: exit 0, "notifier dispatch fails=0", "✓ ... PASSED"
  - This exercises notifier.c host-side (F4, dispatcher ordering, hid_notify,
    sanitize, ack, NULL safety) via qmk_stubs/.

Task T1.E: VERIFY no new compiler warnings (11.2D)
  - gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o      # expect clean
  - gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
        -Iqmk_stubs -I. -c notifier.c -o /tmp/n.o                    # expect clean

Task T1.F: SPOT-CHECK §13 invariants (read source, no edits)
  - grep/confirm: magic header 0x81 0x9F guard in hid_notify (notifier.c)
  - GS_DELIMITER "\x1D" (notifier.h, already says "Group Separator")
  - ETX 0x03 handling; RAW_REPORT_SIZE 32; LAYER_UNSET 255; raw_hid_send(.., 32)
  - disable-before-scan / deactivate-before-activate in process_full_message
  - weak default maps present (__attribute__((weak)) get_command_map etc.)
  - Record any drift (DRIFT-1/BUG-1 are already fixed in code — confirm + note).

# GATE-GREEN GATE: if T1.A–T1.F are ALL green, PROCEED to T2.
#                    If ANY is red, STOP — do not edit code (out of scope) and
#                    do not sync README numbers (honesty guard). Escalate.

# ============================================================
# TASK GROUP T2 — SYNC README.md  (the ONLY file P3 may modify)
# Use the LIVE numbers captured in T1.A. Edit README.md in place.
# ============================================================

Task T2.1: FIX "Running Tests" section — accurate per-suite counts + both gates
  - REPLACE the stale bullet list. State the live aggregate (the N captured in
    T1.A, currently 2019) and enumerate all suites with their live counts:
      test_pattern_match ........ 376
      test_char_classification ... 179
      test_word_boundary_basic ... 74
      test_word_boundary_integration 189
      test_metachar_verification .. smoke (boolean PASS/FAIL)
      test_comprehensive_integration 10 categories
      test_error_handling ........ 161
      test_memory_stress ........ 32
      test_invalid_patterns ..... 1008
  - ADD a bullet for the notifier host-side gate:
      ./run_notifier_stub_tests.sh  (11 cases; validates notifier.c via qmk_stubs)
  - KEEP the "Quick Test" + "./run_all_tests.sh" instructions.

Task T2.2: REWRITE "Current Test Status" section — live numbers, no phantom file
  - STATE: 2019/2019 (or live N) tests passing, 100% success rate, 0 failures.
  - STATE perf: ~0.1 µs per pattern_match call (live ~0.104 µs).
  - REMOVE: "1,992/2,048", "97.2%", "65/65", "0.087 microseconds".
  - REMOVE the line referencing `backward_compatibility_report.md` (does not exist)
    and the "⚠️ Some advanced feature edge cases" line (no longer true — all green).
  - UPDATE the feature bullet list to include the `+` (one-or-more) quantifier
    alongside \d \D \w \W \s \S \b \B .

Task T2.3: REWRITE "Pattern Matching Syntax" — full PRD §15 construct set
  - List EVERY construct (copy semantics from PRD §15, do not paraphrase loosely):
      *            wildcard — any sequence (incl. empty, incl. \n/\r)
      ^            anchor to start of string
      $            anchor to end of string
      ^...$        exact full-string match
      (no anchors) substring match (backward-compatible default)
      .            any char EXCEPT \n / \r
      X+           one or more of element X (linear-time, no backtracking)
      \d  \D       digit / non-digit          [0-9]
      \w  \W       word / non-word char        [A-Za-z0-9_]
      \s  \S       whitespace / non-whitespace [ \t\n\r\f\v]
      \b  \B       word boundary / non-boundary (zero-width)
      \^ \$ \* \\  literal escaped metacharacters
      \. \+        literal dot / literal plus
  - KEEP the existing WT(class,title) note; optionally clarify that a bare
    pattern matches the class part only (PRD §4.1 / F4.2).

Task T2.4: ADD wire-format / delimiter documentation (GS_DELIMITER)
  - In "How It Works" (or a short new subsection) state:
      The companion app sends "<app_class>\x1D<window_title>" where
      GS_DELIMITER = "\x1D" = ASCII 29 = GROUP SEPARATOR (NOT "Unit Separator",
      which is 0x1F). The module first checks for the magic header bytes
      0x81 0x9F and ignores non-matching reports (coexistence guard).
  - Reference PRD §4.1 / §5.3 / §16.

Task T2.5: ALIGN "Setup" with PRD §10.1
  - submodule: add the target dir —
      git submodule add https://github.com/dabstractor/qmk-notifier.git qmk-notifier
  - keep the keymap.c pattern (#include QMK_KEYBOARD_H; #include "./qmk-notifier/notifier.h";
    raw_hid_receive → hid_notify) and the rules.mk include line (already correct).

Task T2.6: CORRECT "Companion Projects" per PRD §1.2 ecosystem
  - qmk_notifier (dabstractor/qmk_notifier) = Rust TRANSPORT CRATE — owns the
    wire framing (magic header, 32-byte chunking, ETX terminator, device cache).
  - QMKonnect (dabstractor/qmkonnect) = Rust cross-platform DESKTOP DAEMON —
    detects the foreground window and sends class\x1Dtitle. (Currently missing
    from the README; ADD it.)
  - KEEP hyprland-qmk-window-notifier and zigotica/active-app-qmk-layer-updater
    as legitimate third-party / community alternatives.

Task T2.7: FINAL consistency sweep of README.md
  - grep -nE "1,992|2,048|97.2|0.087|383 tests|263 tests|65/65|backward_compatibility_report|Unit Separator"
    README.md   # must return NOTHING
  - confirm GS_DELIMITER / "Group Separator" / 0x1D appears at least once.
  - confirm all §15 constructs appear in the pattern-syntax section.
```

### Implementation Patterns & Key Details

```bash
# PATTERN: capture-live-then-write. The README numbers MUST come from an actual
# gate run in THIS working tree, never from the PRD's (stale) 1826 figure.
AGG=$(./run_all_tests.sh 2>&1 | grep 'Total tests run across all suites' | grep -oE '[0-9]+')
# then write "$AGG/$AGG tests passing (100% success rate)" into README.md.

# PATTERN: build the two §11.2 probes from the PRD heredocs verbatim, except
# /tmp/nfa_real.c test #3 uses input "user@host" (DRIFT-2 correction).

# ANTI-PATTERN (forbidden): if ./run_all_tests.sh reports ANY failure, do NOT
# edit code (out of P3 scope) and do NOT write a green number into README.md.
# Halt and escalate. (The gate is currently green, so this should not trigger.)
```

### Integration Points

```yaml
FILES:
  - modify: README.md           # THE only file P3 touches
  - read-only: PRD.md, pattern_match.{c,h}, notifier.{c,h}, rules.mk,
               run_all_tests.sh, run_notifier_stub_tests.sh, test_*.c, qmk_stubs/
  - read-only: plan/001_e329fbe4ae4d/**  (orchestrator-owned)

CONFIG: none
ROUTES: none
DATABASE: none
```

## Validation Loop

### Level 1: Syntax & Style (Immediate Feedback)
```bash
# Markdown has no compiler; validate structurally instead.
# Confirm no stale tokens survive and required tokens are present:
grep -nE "1,992|2,048|97\.2|0\.087|383 tests|263 tests|65/65|backward_compatibility_report|Unit Separator" README.md
# Expected: NO output (all stale tokens removed).
grep -nE "Group Separator|0x1D|GS_DELIMITER" README.md   # Expected: ≥1 hit
grep -nE "\\\\d|\\\\w|\\\\s|\\\\b|X\+|one or more" README.md  # Expected: the new constructs present
```

### Level 2: Acceptance Gate (the deliverable validation)
```bash
# 11.2A — 9 suites
./run_all_tests.sh > /tmp/gate.log 2>&1; echo "exit=$?"
grep -E "Total tests run across all suites|Total tests failed|Overall success rate" /tmp/gate.log
# Expected: exit=0, "Total tests failed: 0", "Overall success rate: 100.0%"

# 11.2A per-suite fails
for t in test_pattern_match test_char_classification test_word_boundary_basic \
         test_word_boundary_integration test_metachar_verification \
         test_comprehensive_integration test_error_handling test_memory_stress \
         test_invalid_patterns; do
  printf "%-36s fails=%s\n" "$t" "$(./$t 2>&1 | grep -c '^FAIL:')"
done
# Expected: fails=0 on every line.

# 11.2B — pathological NFA (< 50 ms)
gcc -O2 -w /tmp/nfa_stress.c pattern_match.c -I. -o /tmp/nfa_stress && timeout 5 /tmp/nfa_stress
# Expected: "result=0  <NNNN> us"  (measured ~1800 us; must be < 50000 us)

# 11.2C — realistic patterns (DRIFT-2: input is user@host)
gcc -w /tmp/nfa_real.c pattern_match.c -I. -o /tmp/nfa_real && /tmp/nfa_real
# Expected: six lines each "1"

# notifier stub gate (RISK-1 closure)
./run_notifier_stub_tests.sh; echo "exit=$?"
# Expected: exit=0, "notifier dispatch fails=0", "✓ ... PASSED"

# 11.2D — no new warnings
gcc -Wall -Wextra -std=c99 -c pattern_match.c -o /tmp/pm.o 2>&1 | head   # expect empty
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c -o /tmp/n.o 2>&1 | head  # expect empty
```

### Level 3: README ↔ Reality Cross-Check
```bash
# The aggregate number stated in README MUST equal the live gate aggregate.
README_AGG=$(grep -oE "[0-9,]+/[0-9,]+ tests passing" README.md | head -1)
LIVE_AGG=$(grep -oE "Total tests run across all suites: [0-9]+" /tmp/gate.log | grep -oE "[0-9]+$")
echo "README says: $README_AGG   ; live gate: $LIVE_AGG"
# Expected: the numerator/denominator in README equals LIVE_AGG (currently 2019).

# Confirm README no longer references a non-existent report file.
test ! grep -q backward_compatibility_report README.md && echo "OK: phantom file ref removed"
```

### Level 4: Invariant Spot-Check (PRD §13, read-only)
```bash
grep -n "0x81" notifier.c | head -1     # magic header guard
grep -n "GS_DELIMITER" notifier.h       # 0x1D
grep -n "RAW_REPORT_SIZE 32\|RAW_REPORT_SIZE  32" notifier.c
grep -n "LAYER_UNSET" notifier.c | head -2
grep -n "raw_hid_send" notifier.c       # must pass 32
grep -n "__attribute__((weak))" notifier.c   # weak default maps
# Expected: all present and consistent with PRD §13/§16. (DRIFT-1/BUG-1 already fixed.)
```

## Final Validation Checklist

### Technical Validation
- [ ] `./run_all_tests.sh` exit 0, `Total tests failed: 0`, aggregate = live N (2019).
- [ ] All 9 suites `fails=0` (Level 2 per-suite loop).
- [ ] 11.2B `result=0` in < 50 ms.
- [ ] 11.2C six `1`s (input `user@host`).
- [ ] `./run_notifier_stub_tests.sh` exit 0.
- [ ] No new `-Wall -Wextra` warnings on `pattern_match.c` / `notifier.c` (stub).
- [ ] §13 invariants spot-checked (Level 4).

### Feature (Documentation) Validation
- [ ] `README.md` contains NO stale token (Level 1 grep returns empty).
- [ ] README "Running Tests" lists all 9 suites with live counts + the stub gate.
- [ ] README "Current Test Status" states the live aggregate (2019/2019, 100%, 0 fails).
- [ ] README "Pattern Matching Syntax" lists the full §15 construct set.
- [ ] README documents GS_DELIMITER = ASCII 29 (0x1D) Group Separator.
- [ ] README Setup uses `git submodule add … qmk-notifier` (target dir).
- [ ] README Companion Projects names QMKonnect (daemon) + qmk_notifier (transport crate).
- [ ] README aggregate number EQUALS the live gate aggregate (Level 3).

### Code Quality / Scope Validation
- [ ] `git diff --name-only` shows ONLY `README.md` changed (no source/test/script/PRD edits).
- [ ] Nothing under `plan/` was modified.

### Documentation & Deployment
- [ ] README is internally consistent (counts, perf, feature list all agree with the gate).
- [ ] DRIFT-2 noted (PRD §11.2C test #3 annotation is a spec error; code is correct).

---

## Anti-Patterns to Avoid

- ❌ **Don't hard-code "1826" into the README.** The PRD's 1826 is itself stale;
  the live aggregate is 2019. Always capture from `./run_all_tests.sh`.
- ❌ **Don't use the PRD's literal `user_host` for 11.2C test #3** — it correctly
  returns 0 (DRIFT-2). The gate uses `user@host` (returns 1).
- ❌ **Don't edit code to "fix" a passing gate.** The matcher is correct; only
  docs lag. BUG-1 and DRIFT-1 are already fixed in the source.
- ❌ **Don't write green numbers into a red README.** If the gate is red, HALT
  and escalate — do not edit code (out of P3 scope) and do not misrepresent.
- ❌ **Don't touch any file except README.md.** test_*.c, pattern_match.{c,h},
  notifier.{c,h}, rules.mk, run_all_tests.sh, PRD.md, and plan/** are all off-limits.
- ❌ **Don't paraphrase pattern semantics loosely.** Copy §15 semantics exactly
  (e.g. `.` excludes `\n`/`\r` but `*` includes them; `+` is one-or-more).
- ❌ **Don't drop the legitimate community companion tools** (hyprland, zigotica)
  when correcting the QMKonnect/qmk_notifier roles.

---

## Confidence Score

**9 / 10** for one-pass success.

Rationale: the gate is already GREEN (verified by running it: 2019/2019, 0
failures; 11.2B ~1.8 ms; 11.2C six `1`s with the DRIFT-2 correction; stub gate
exit 0; clean `-Wall -Wextra`; BUG-1/DRIFT-1 already fixed in code). The
remaining work is deterministic: run the gate, capture live numbers, and edit six
named README sections against explicit PRD cross-references. The only residual
risk is mis-copying the live aggregate — mitigated by the Level 3 cross-check
that the README number must equal the gate's printed total.
