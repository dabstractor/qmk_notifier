# PRP — P2: QMK Firmware Module (`notifier.{c,h}`, `rules.mk`) + Stub-Compile Harness

## Goal

**Feature Goal**: Stand up the on-keyboard **receiver / reassembler /
delimiter-aware matcher / rule dispatcher** (`notifier.c`), its public API
(`notifier.h`), and the QMK build glue (`rules.mk`) — and **close RISK-1** (this
file has **zero** host-side test coverage because it `#include`s `QMK_KEYBOARD_H`
and cannot be compiled by the 9-suite gate) by shipping a **QMK-stub compile +
host-link validation harness** that exercises the F4 delimiter logic, the
dispatcher ordering invariants, the `hid_notify` reassembly/ack path, and the
BUG-1 NULL-robustness fix — all with plain `gcc` on a host.

**Deliverable**:
1. `notifier.h` — verified against PRD §5; **DRIFT-1** comment fixed (GS = ASCII 29).
2. `notifier.c` — verified against PRD §8; **BUG-1** NULL-deref fixed (2-line guard
   reorder); **DRIFT-1** comments fixed; (optional) `-Wsign-compare` cleanup.
3. `rules.mk` — verified identical to PRD §9 (2 lines; no change).
4. **NEW stub-compile harness** (5 files): `qmk_stubs/qmk_keyboard_stub.h`,
   `qmk_stubs/raw_hid.h`, `qmk_stubs/qmk_stubs.c`, `test_notifier_dispatch.c`,
   `run_notifier_stub_tests.sh` — the P2 validation gate.

**Success Definition**:
- `./run_notifier_stub_tests.sh` compiles `notifier.c` against the QMK stubs with
  zero errors (warnings ≤ pre-existing), links the test driver, and reports
  **0 failures** across the F4 matrix + dispatcher ordering + hid_notify + NULL.
- `match_pattern(NULL, "x", 0)` returns `false` (no segfault) — BUG-1 fixed.
- The 9 existing suites (`./run_all_tests.sh`) are unaffected (P2 touches only
  `notifier.{c,h}`, never `pattern_match.{c,h}`) — confirms the BUG-1 reorder
  changes nothing for non-NULL inputs.
- A reference-keymap integration driver (PRD §10.2 representative rules) feeds
  realistic `class\x1Dtitle` messages and asserts the exact layer/callback
  transitions.

## User Persona (if applicable)

**Target User**: (1) The end user's QMK `keymap.c`, which `#include`s `notifier.h`,
   wires `raw_hid_receive → hid_notify`, and uses `DEFINE_SERIAL_COMMANDS` /
   `DEFINE_SERIAL_LAYERS` + `WT(...)`. (2) The P2 implementer/CI, which runs the
   stub harness. (3) P3's acceptance gate, which re-runs both the 9 suites and the
   stub harness together.

**Use Case**: The desktop (QMKonnect) sends `class\x1Dtitle` over Raw HID; the
firmware reassembles, sanitizes, matches against both user maps, and on a match
switches a QMK layer and/or fires a callback — exactly one notifier layer active
at a time, exactly clean enable/disable transitions.

**User Journey**: `raw_hid_receive(data,32)` → `hid_notify` (coexistence guard
`0x81 0x9F`) → strip header → accumulate into `msg_buffer` until ETX →
`sanitize_string` → `process_full_message` → `disable_command()` → scan
`command_map` (first-match-wins via `match_pattern`) → scan `layer_map` →
`deactivate_layer()` → `enable_command` / `activate_layer` → `raw_hid_send`.

**Pain Points Addressed**: BUG-1 (latent NULL-deref violating PRD §8.5/§12);
RISK-1 (notifier.c dispatch/reassembly/`match_pattern` were untested off-device —
the stub harness is the only way to validate without flashing hardware).

## Why

- **P2's unique risk is validation, not authorship.** The three files already
  exist as the committed reference (`81df853`-identical; `git diff` empty). The
  real job is to (a) prove they conform to PRD §5/§8/§9, (b) fix the two
  documented defects, and (c) give them a deterministic host gate. RISK-1
  (`findings_and_risks.md`) explicitly recommends exactly this harness.
- **The harness is faithful to the real build.** `notifier.c` does
  `#include "pattern_match.c"` (the .c, same translation unit). The harness
  compiles `notifier.c` exactly that way, so it transitively validates the full
  P1→P2 integration AND detects P1 regressions (if `match_with_anchors` breaks,
  the F4 cases flip red).
- **BUG-1 is a real correctness gap.** Reproduced during research:
  `match_pattern(NULL,"x",0)` → **SIGSEGV (exit 139)**. It violates explicit PRD
  requirements (§8.5 step 2, §12, §13). The fix is a 2-line reorder that changes
  nothing for non-NULL inputs (re-verified: identical F4 results after the fix).
- **Scope hygiene.** P2 touches only `notifier.{c,h}` + new harness files. It must
  NOT touch `pattern_match.{c,h}` (P1 owns those), `rules.mk`'s 2 lines (verified),
  or any of the 9 `test_*.c` / `run_all_tests.sh` (they are P1's spec, PRD §17).

## What

### A. `notifier.h` — verify + fix DRIFT-1 (PRD §5)

Keep the full public API exactly as-is: `callback_t`, `command_map_t`,
`layer_map_t`, the four map accessors, `GS_DELIMITER`/`ETX_TERMINATOR`,
`WINDOW_TITLE`/`WT`, `DEFINE_SERIAL_COMMANDS`/`DEFINE_SERIAL_LAYERS`, and the two
entry-point declarations. **Only** change the GS comment (DRIFT-1).

### B. `notifier.c` — verify + fix BUG-1 + DRIFT-1 (PRD §8)

Keep all logic exactly as the reference: includes (incl. `#include "pattern_match.c"`),
`RAW_REPORT_SIZE`/`MSG_BUFFER_SIZE`/`LAYER_UNSET`, globals, `sanitize_string`, weak
defaults, `activate_layer`/`deactivate_layer`/`enable_command`/`disable_command`,
`find_first_delimiter`/`split_by_delimiter`/`match_pattern`, `process_full_message`,
`hid_notify`. **Three** edits:
1. **BUG-1**: reorder the NULL guard above `find_first_delimiter(pattern)` in
   `match_pattern` (§8.5 step 2). Mandatory.
2. **DRIFT-1**: fix the two GS comments at `:116` and `:297` ("group separator").
3. **(optional)** `-Wsign-compare`: `for (size_t i = 0; i < ...; i++)` at the two
   map loops + cast `length` compare. Pre-existing; reference has them; not required.

### C. `rules.mk` — verify only (PRD §9)

Must remain exactly:
```make
RAW_ENABLE = yes
SRC += qmk-notifier/notifier.c
```
No change. (If it differs, restore these 2 lines — do NOT hand-write
`SRC += lib/...` or point at `qmk_notifier.c`; that fails to link.)

### D. NEW stub-compile harness (5 files) — closes RISK-1

`qmk_stubs/qmk_keyboard_stub.h`, `qmk_stubs/raw_hid.h`, `qmk_stubs/qmk_stubs.c`,
`test_notifier_dispatch.c`, `run_notifier_stub_tests.sh`. Full bodies in the
*Implementation Blueprint*. Reproduced from the working `/tmp/notifier_harness`
prototype built and run during research (all green).

### Success Criteria

- [ ] `notifier.h` GS comment reads `// ASCII 29 (Group Separator)`; API unchanged.
- [ ] `notifier.c` BUG-1 fixed (`match_pattern(NULL,"x",0)` returns false, no segv).
- [ ] `notifier.c` DRIFT-1 comments fixed (2 sites).
- [ ] `rules.mk` unchanged (the 2 lines).
- [ ] `./run_notifier_stub_tests.sh` → compiles `notifier.c` (errors=0), links the
      driver, runs it → **0 FAIL** across F4 + dispatcher + hid_notify + NULL.
- [ ] `./run_all_tests.sh` unaffected (the 9 suites still pass — P2 never touches
      `pattern_match.{c,h}` or the `test_*.c`).

## All Needed Context

### Context Completeness Check

**Pass.** This PRP is grounded in: (1) the P1 PRP contract
(`plan/001_e329fbe4ae4d/P1/PRP.md` — `pattern_match()` public API, dual-compile
invariant), (2) the full PRD §5/§8/§9/§13 (reproduced in `<selected_prd_content>`),
(3) the architecture docs (`plan/001_e329fbe4ae4d/architecture/notifier_architecture.md`
and `findings_and_risks.md` — which document BUG-1, DRIFT-1, RISK-1, and the F4
matrix the scout verified), (4) the live reference files (committed, `81df853`-identical),
and (5) a **working stub-compile harness built and run during this research** — its
exact files and verified commands are reproduced verbatim below. An implementer
with this PRP + repo access can complete P2 in one pass. The one ordering subtlety
in the harness (`-DQMK_KEYBOARD_H` must use a bare filename resolved via `-I`, not
a quoted path) is flagged as GOTCHA-1 and was resolved empirically.

### Documentation & References

```yaml
# MUST READ — the authoritative file specs this PRP verifies
- file: PRD.md
  section: "## 5. File Specification: notifier.h"
  why: "§5.1 structs, §5.2 accessors, §5.3 constants/macros (GS=0x1D=29=GS — note the
        comment-drift callout), §5.4 DEFINE_* macros, §5.5 entry points."
  critical: "§5.3: 'the byte value 0x1D (29 = GS) is authoritative' — this is why
        DRIFT-1 (the 'ASCII 31 Unit Separator' comment) is fixed to 'ASCII 29
        (Group Separator)'. The BYTE stays 0x1D."

- file: PRD.md
  section: "## 8. File Specification: notifier.c — the receiver/dispatcher"
  why: "§8.1 constants/globals, §8.2 sanitize_string, §8.3 weak defaults, §8.4 layer/cmd
        state machines, §8.5 match_pattern (step 2 NULL->false is BUG-1's contract),
        §8.6 process_full_message ordering, §8.7 hid_notify + ack."
  critical: "§8.5 step 2 'NULL pattern or message -> false' is the contract BUG-1
        breaks (find_first_delimiter(pattern) runs BEFORE the guard). §8.6 ordering:
        disable-before-scan, deactivate-before-activate, first-match-wins,
        unmatched-clears-state."

- file: PRD.md
  section: "## 9. File Specification: rules.mk"
  why: "Exactly 2 lines: RAW_ENABLE = yes ; SRC += qmk-notifier/notifier.c."
  critical: "Do NOT hand-write 'SRC += lib/...' or 'qmk_notifier.c' (underscore) —
        that fails to link. notifier.c #includes pattern_match.c itself."

- file: PRD.md
  section: "## 13. Key Invariants a Dev Must Preserve"
  why: "#1 magic 0x81 0x9F, #2 GS=0x1D/ETX=0x03, #3 RAW_REPORT_SIZE=32 (NOT RAW_EPSIZE),
        #4 disable-before-scan/deactivate-before-activate, #5 first-match-wins,
        #6 exactly-one-layer (LAYER_UNSET=255), #7 unmatched clears state,
        #13 weak defaults must remain (DEFINE_* optional links), #14 pattern_match.c
        compiles both standalone and #included."
  critical: "The stub harness must NOT weaken any of these. #4/#6/#7 are encoded as
        explicit dispatcher-ordering assertions in test_notifier_dispatch.c."

# Architecture — the narrative + the F4 matrix + the defect catalog
- file: plan/001_e329fbe4ae4d/architecture/notifier_architecture.md
  why: "Include order quirk (#include pattern_match.c), constants/globals, the F4
        delimiter matrix (all 4 cases), process_full_message ordering invariants,
        sanitize_string allow-list."
  critical: "The F4 matrix table is the exact set of cases test_notifier_dispatch.c
        encodes. The ordering-invariants section is the exact set of dispatcher
        assertions."

- file: plan/001_e329fbe4ae4d/architecture/findings_and_risks.md
  why: "BUG-1 (NULL deref in match_pattern, notifier.c:153-156), DRIFT-1 (GS comment
        wrong at notifier.h:22 + notifier.c:116,297), RISK-1 (notifier.c zero host
        coverage -> the harness recommendation), external-deps confirmation (RAW_EPSIZE
        vs RAW_BUFFER_SIZE, layer_on/off transitively via QMK_KEYBOARD_H)."
  critical: "RISK-1 is the REASON this PRP ships a harness. BUG-1 + DRIFT-1 are the
        two fixes. The QMK-claims table confirms what symbols the stubs must provide
        (layer_on/layer_off, raw_hid_send) and that uint8_t/uint16_t/size_t come
        transitively from QMK_KEYBOARD_H."

- file: plan/001_e329fbe4ae4d/architecture/external_deps.md
  why: "Confirms RAW_ENABLE -> 0xFF60/0x61; raw_hid_send declared in quantum/raw_hid.h;
        layer_on/layer_off in action_layer.h via QMK_KEYBOARD_H; SRC += is the standard
        module pattern; __attribute__((weak)) is pervasive in QMK (the DEFINE_* override
        mechanism)."
  critical: "This is the source of truth for what the stub headers must declare so the
        harness's TU matches the real QMK TU surface used by notifier.c."

# Dependency PRP — P1's contract (parallel execution)
- file: plan/001_e329fbe4ae4d/P1/PRP.md
  why: "Defines the pattern_match() public API (bool pattern_match(pattern, str,
        case_sensitive)) and the dual-compile invariant (#13 #14) P2 relies on. The
        stub harness #includes pattern_match.c via notifier.c, so it transitively
        validates P1 and is a P1 regression detector."
  critical: "P2 MUST NOT reimplement or alter pattern_match.{c,h}. It only CONSUMES
        pattern_match() via the internal match_pattern() wrapper. If P1 regresses
        (match_with_anchors breaks), the harness's F4 cases flip red — that's a P1
        bug, not a P2 bug; do NOT patch it in notifier.c."

# The live reference (CONTRACT — reproduce logic; comments may drift per PRD §17)
- file: git HEAD notifier.c / notifier.h / rules.mk  (== 81df853, committed)
  why: "These ARE the reference. `git diff 81df853:notifier.c -- notifier.c` is empty.
        P2 verifies them and applies exactly 2 logic-level touches (BUG-1 reorder,
        DRIFT-1 comments) — nothing else."
  critical: "PRD §17: 'the code + the passing tests win'. The reference is correct;
        BUG-1 is the one place the reference violates an explicit PRD clause (§8.5
        step 2). Reorder only; do not rewrite match_pattern."

# External theory (optional, for the stub-compile approach)
- url: https://gcc.gnu.org/onlinedocs/cpp/Uses-of-the-Preprocessor.html
  why: "`#include QMK_KEYBOARD_H` with a macro-expanded header name is exactly what
        makes -DQMK_KEYBOARD_H='\"...\"' work for stub compilation."
  critical: "GOTCHA-1: the macro value must be a BARE filename resolved via -I, not a
        quoted relative path (a quoted 'stubs/x.h' resolves relative to notifier.c and
        fails). Verified empirically."
```

### Current Codebase tree (run `ls` at repo root)

```bash
notifier.h            # P2 — EXISTS as reference (81df853-identical). DRIFT-1 comment to fix.
notifier.c            # P2 — EXISTS as reference (81df853-identical). BUG-1 + DRIFT-1 to fix.
rules.mk              # P2 — EXISTS as reference (2 lines). Verify only; no change.
pattern_match.h       # P1 — DO NOT TOUCH (public contract).
pattern_match.c       # P1 — DO NOT TOUCH (~99.7% complete; match_with_anchors real at :234).
test_*.c (x9)         # P1 — DO NOT TOUCH (they are the spec, PRD §17).
run_all_tests.sh      # P1 — DO NOT TOUCH (the 9-suite gate).
PRD.md                # READ-ONLY.
```

### Desired Codebase tree with files to be added and responsibility of file

```bash
notifier.h            # MODIFIED: DRIFT-1 comment (GS = ASCII 29 Group Separator). API unchanged.
notifier.c            # MODIFIED: BUG-1 NULL-guard reorder (match_pattern); DRIFT-1 x2 comments;
                       #           (optional) -Wsign-compare cleanup. All other logic unchanged.
rules.mk              # UNCHANGED (verify the 2 lines).

# NEW — stub-compile harness (closes RISK-1):
qmk_stubs/qmk_keyboard_stub.h   # target of `#include QMK_KEYBOARD_H` under -D: uint8/16/bool +
                                 #   layer_on/layer_off decls + RAW_EPSIZE=32 fallback.
qmk_stubs/raw_hid.h             # declares raw_hid_send (resolved via -Iqmk_stubs).
qmk_stubs/qmk_stubs.c           # impls layer_on/layer_off/raw_hid_send w/ observable printf
                                 #   side effects (so the test driver can assert transitions).
test_notifier_dispatch.c        # F4 matrix (4 cases) + dispatcher ordering + hid_notify
                                 #   reassembly/ack + BUG-1 NULL survival. PASS:/FAIL: + summary
                                 #   (matches PRD §11.4 test framework so it greps cleanly).
run_notifier_stub_tests.sh      # compiles notifier.c w/ stubs, links the driver, runs it,
                                 #   prints fails=N. exit non-zero on any FAIL.
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — GOTCHA-1: `-DQMK_KEYBOARD_H` must use a BARE filename resolved via -I.
//   `-DQMK_KEYBOARD_H='"stubs/qmk_keyboard_stub.h"'` FAILS: the quoted path resolves
//   relative to notifier.c first, then -Idir/stubs/... (double-dir) -> not found.
//   CORRECT: `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs`.
//   (Verified empirically during research.)

// CRITICAL — GOTCHA-2 (BUG-1): match_pattern() currently calls
//   find_first_delimiter(pattern) BEFORE the NULL guard. With pattern==NULL that
//   dereferences NULL (find_first_delimiter loops `for(p=str;*p;p++)`). REPRODUCED:
//   match_pattern(NULL,"x",0) -> SIGSEGV (exit 139). FIX: move the
//   `if (message==NULL || pattern==NULL) return false;` ABOVE the find_first_delimiter
//   call. This is PRD §8.5 step 2. Zero behavior change for non-NULL inputs (re-verified).

// GOTCHA-3: notifier.c does `#include "pattern_match.c"` (the .c), NOT pattern_match.h.
//   The stub harness compiles notifier.c exactly this way, so pattern_match.c is pulled
//   into the SAME translation unit. Do NOT also compile pattern_match.c separately when
//   linking the driver — you'd get duplicate-symbol errors. Link: notifier.o + qmk_stubs.c
//   + test_notifier_dispatch.c ONLY. (PRD §3, §13 #14.)

// GOTCHA-4: CONSOLE_ENABLE is undefined in the stub build, so `#include "print.h"` and
//   the uprintf debug blocks are skipped automatically. Do NOT define CONSOLE_ENABLE in
//   the harness (it would pull in QMK's print.h, which the stubs don't provide).
//   This means the stub build does NOT exercise the uprintf logging branches — that's
//   fine; logging is cosmetic and CONSOLE-gated in production too.

// GOTCHA-5: the weak-default override mechanism. The harness's test_notifier_dispatch.c
//   uses DEFINE_SERIAL_COMMANDS({...}) and DEFINE_SERIAL_LAYERS({...}) to provide
//   NON-weak get_command_map/get_layer_map (+size). At link time these override the
//   __attribute__((weak)) defaults in notifier.o. If you forget the macros, the weak
//   defaults return size 0 and NO rule ever matches (all dispatch returns 0). Symptom:
//   "proc(neovide)=0" when you expected =1. (PRD §8.3, external_deps.md weak-attr note.)

// GOTCHA-6: `response[0] = match` in hid_notify writes a bool (0/1) into a uint8_t.
//   The PRD §4.4/§8.7 contract is response[0] = (matched ? 1 : 0). The reference assigns
//   the bool directly — that's fine (bool->uint8_t narrows to 0/1). Don't "improve" it.

// GOTCHA-7: GS_DELIMITER[0] and ETX_TERMINATOR[0] are used in notifier.c (sanitize_string,
//   hid_notify) to index the FIRST byte of the string-literal macros. That's the 0x1D /
//   0x03 byte. Keep the macros as string literals (not char constants) so [0] indexing
//   stays valid. (notifier.c sanitize_string + find_first_delimiter + hid_notify.)

// GOTCHA-8: the reference has 3 pre-existing -Wsign-compare warnings under -Wall -Wextra
//   (notifier.c:247 int>=sizeof; :266/:275 int<size_t in map loops). They are harmless
//   (tiny map sizes). Optionally fix with `for(size_t i...)`; NOT required (PRD §17 code
//   wins, the reference has them). If you DO fix them, do not change loop bounds/semantics.

// GOTCHA-9 (anti-regression): do NOT touch pattern_match.{c,h}, rules.mk's 2 lines, or
//   any test_*.c / run_all_tests.sh. P2's scope is notifier.{c,h} + the new harness.
//   A red existing-suite test means P1 regressed — fix it in P1, not here.

// GOTCHA-10: the F4 two-part semantics are subtle. Re-read PRD §8.5 + the F4 matrix in
//   notifier_architecture.md before editing match_pattern. The 4 cases (no/no, no/yes,
//   yes/no, yes/yes) are encoded as assertions in test_notifier_dispatch.c; if you
//   "simplify" the logic, a case will flip. The BUG-1 fix only MOVES the NULL guard —
//   it must not touch any of the 4 branches.
```

## Implementation Blueprint

### Data models and structure

No new data models. P2 consumes the existing `command_map_t` / `layer_map_t` /
`callback_t` (notifier.h) and `parsed_pattern_t` / `State` / `pattern_match()`
(pattern_match.{c,h}, P1-owned). The harness adds only free functions + a
`test_case_t` table in the driver (matching PRD §11.4).

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: P2.M1.T1.S1 — notifier.h: fix DRIFT-1 comment (PRD §5.3)
  - EDIT notifier.h line 22 ONLY.
  - CHANGE: `#define GS_DELIMITER "\x1D"  // ASCII 31 (Unit Separator)`
    ->   `#define GS_DELIMITER "\x1D"  // ASCII 29 (Group Separator)`
  - PRESERVE: every other line (structs, accessors, ETX_TERMINATOR, WINDOW_TITLE,
    WT, DEFINE_SERIAL_COMMANDS, DEFINE_SERIAL_LAYERS, entry-point decls). The byte
    value 0x1D is ALREADY correct — only the comment was wrong.
  - WHY: PRD §5.3 explicitly: "the byte value 0x1D (29 = GS) is authoritative";
    external_deps.md ASCII table confirms 0x1D=29=GS, 0x1F=31=US (not used).
  - NAMING/PLACEMENT: unchanged.

Task 2: P2.M2.T3.S1 — notifier.c: FIX BUG-1 (NULL guard reorder) in match_pattern
  - EDIT notifier.c match_pattern() (currently lines ~153-156).
  - BEFORE (BUG):
        bool match_pattern(const char *pattern, const char *message, bool case_sensitive) {
            const char *pattern_delimiter = find_first_delimiter(pattern);   // DEREFs NULL
            if (message == NULL || pattern == NULL) {                         // guard TOO LATE
                return false;
            }
  - AFTER (FIX):
        bool match_pattern(const char *pattern, const char *message, bool case_sensitive) {
            if (message == NULL || pattern == NULL) {                         // guard FIRST
                return false;
            }
            const char *pattern_delimiter = find_first_delimiter(pattern);
            ...   # rest of the function UNCHANGED
  - DEPENDENCIES: none (find_first_delimiter is defined above match_pattern in the file).
  - WHY: PRD §8.5 step 2, §12, §13. Reproduced SIGSEGV during research.
  - VERIFY (Level 2): match_pattern(NULL,"x",0) returns false (exit 0, no segfault);
    all F4 cases still PASS (no behavior change for non-NULL).
  - DO NOT: alter any of the 4 delimiter branches; add new buffers; change signatures.

Task 3: notifier.c: fix DRIFT-1 comments (2 sites)
  - EDIT notifier.c:116  `// Helper function to find unit delimiters in a string`
    -> `// Helper function to find the group separator (GS, 0x1D) in a string`
  - EDIT notifier.c:297  `// replace all unit delimeters with |`
    -> `// replace all group separators (GS) with '|' for console readability`
  - WHY: factual accuracy (0x1D = Group Separator, not "unit separator/delimeter").
  - DO NOT: change any code; these are comments only.

Task 4 (OPTIONAL): notifier.c: clean up the 3 pre-existing -Wsign-compare warnings
  - EDIT notifier.c:266 `for (int i = 0; i < cmd_map_size; i++)`
    -> `for (size_t i = 0; i < cmd_map_size; i++)`  (and cast cmd_map[i] index uses
       if any int context). Repeat at :275 (lyr_map loop). At :247, cast:
       `if ((size_t)length >= sizeof(received_command))`.
  - WHY: a truly clean `-Wall -Wextra` stub compile. PRE-EXISTING in reference.
  - IF YOU SKIP THIS: acceptable — the warnings are harmless and PRD §17 permits them.
    Just ensure the harness's "errors=0" gate keys on ERRORS, not warnings.

Task 5: VERIFY rules.mk unchanged (PRD §9)
  - READ rules.mk. It MUST be exactly:
        RAW_ENABLE = yes
        SRC += qmk-notifier/notifier.c
  - IF it differs: restore those 2 lines. Do NOT add `SRC += lib/...`, do NOT point
    at `qmk_notifier.c` (underscore), do NOT add `SRC += pattern_match.c` (notifier.c
    #includes it itself).

Task 6: P2.M1.T1.S1 (harness) — CREATE qmk_stubs/qmk_keyboard_stub.h
  - CREATE file with EXACTLY the content in "Stub harness files" below.
  - WHY: target of `#include QMK_KEYBOARD_H` under -D; provides uint8_t/uint16_t/
    size_t/bool + layer_on/layer_off decls + RAW_EPSIZE fallback.
  - GOTCHA-1: filename is bare (qmk_keyboard_stub.h); resolved via -Iqmk_stubs.

Task 7: CREATE qmk_stubs/raw_hid.h  (declares raw_hid_send)
Task 8: CREATE qmk_stubs/qmk_stubs.c (impls layer_on/layer_off/raw_hid_send w/ printf)
  - These three files are reproduced VERBATIM in "Stub harness files" below (verified
    working during research).

Task 9: CREATE test_notifier_dispatch.c (the validation driver)
  - IMPLEMENT: the test_case_t F4 table (6 cases covering F4.1-F4.4 + a negation each),
    a dispatcher-ordering sequence (DEFINE_* maps + process_full_message calls asserting
    on_enable/on_disable/layer_on/layer_off via the stub printf OR via instrumentation),
    a hid_notify reassembly+ack case, and a BUG-1 NULL-survival case.
  - FOLLOW pattern: PRD §11.4 test framework — run_test() prints PASS:/FAIL:, final
    summary "Total tests run: N / passed: P / failed: F". (So run_notifier_stub_tests.sh
    can `grep -c '^FAIL:'`.)
  - DEPENDENCIES: notifier.h (structs + DEFINE_* + WT); match_pattern/process_full_message/
    hid_notify (declared extern — they're non-static in notifier.c).
  - SEE "test_notifier_dispatch.c reference body" below for the verified skeleton.

Task 10: CREATE run_notifier_stub_tests.sh (the gate)
  - IMPLEMENT: compile notifier.c w/ stubs -> link driver -> run -> grep FAIL -> summary.
  - EXACT body in "run_notifier_stub_tests.sh reference body" below (verified working).
  - EXIT: non-zero if any FAIL (or compile/link error).
```

### Stub harness files (VERBATIM — built + run green during research)

**`qmk_stubs/qmk_keyboard_stub.h`**
```c
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Minimal QMK surface consumed by notifier.c when stub-compiled on a host.
 * In a real QMK build these come transitively from #include QMK_KEYBOARD_H. */
void layer_on(uint8_t layer);
void layer_off(uint8_t layer);

#ifndef RAW_EPSIZE
#define RAW_EPSIZE 32
#endif
```

**`qmk_stubs/raw_hid.h`**
```c
#pragma once
#include <stdint.h>

/* Stub declaration of QMK's Raw HID send (quantum/raw_hid.h). */
void raw_hid_send(uint8_t *data, uint8_t length);
```

**`qmk_stubs/qmk_stubs.c`**
```c
#include <stdint.h>
#include <stdio.h>

/* Observable QMK symbol implementations for the host stub harness.
 * In production these are provided by QMK (action_layer.c, raw_hid.c). */
static uint8_t g_active_layer = 255;

void layer_on(uint8_t layer) {
    g_active_layer = layer;
    fprintf(stderr, "[stub] layer_on(%u) -> active=%u\n", layer, g_active_layer);
}
void layer_off(uint8_t layer) {
    (void)layer;
    g_active_layer = 255;
    fprintf(stderr, "[stub] layer_off -> active=255\n");
}
void raw_hid_send(uint8_t *data, uint8_t length) {
    (void)length;
    fprintf(stderr, "[stub] raw_hid_send response[0]=%u\n", data[0]);
}
```

**`test_notifier_dispatch.c` reference body** (skeleton — extend the F4 table to taste;
this is the verified-working driver, reformatted to the PRD §11.4 PASS:/FAIL: style):

```c
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "notifier.h"

/* Non-static entry points implemented in notifier.c (which #includes pattern_match.c). */
bool  match_pattern(const char *pattern, const char *message, bool case_sensitive);
bool  process_full_message(char *data);
void  hid_notify(uint8_t *data, uint8_t length);

/* --- User overrides for the weak defaults (GOTCHA-5) --- */
static void on_en_cmd(void){ fprintf(stderr, "  -> on_enable fired\n"); }
static void on_dis_cmd(void){ fprintf(stderr, "  -> on_disable fired\n"); }
DEFINE_SERIAL_COMMANDS({
    { "neovide", on_en_cmd, on_dis_cmd },
    { WT("*chrome*", "*claude*"), on_en_cmd, on_dis_cmd },
});
DEFINE_SERIAL_LAYERS({
    { "chrome*", 5 },
    { WT("firefox", "*github*"), 7 },
});

static int g_pass = 0, g_fail = 0;
static void ck(const char *p, const char *m, int cs, int want) {
    int r = match_pattern(p, m, cs);
    if (r == want) { g_pass++; printf("PASS: match_pattern(\"%s\",\"%s\",cs=%d)=%d\n", p, m, cs, r); }
    else          { g_fail++; printf("FAIL: match_pattern(\"%s\",\"%s\",cs=%d)=%d want %d\n", p, m, cs, r, want); }
}

int main(void) {
    /* --- F4 delimiter-aware matching matrix (PRD §8.5 / F4.1-F4.4) --- */
    ck("abc",                       "xabcx",                    0, 1);  /* F4.4 no/no  */
    ck("code",                      "code\x1d""main.rs",        0, 1);  /* F4.2 no-pat/delim-msg -> class half */
    ck("zzz",                       "code\x1d""main.rs",        0, 0);  /* F4.2 neg */
    ck("code\x1d""main",            "code",                     0, 1);  /* F4.3 delim-pat/no-msg -> pat class vs whole */
    ck("*chrome*\x1d""*claude*",    "Chrome\x1d""Claude - Chat",0, 1); /* F4.1 both -> AND halves */
    ck("*chrome*\x1d""*zzz*",       "Chrome\x1d""Claude",       0, 0);  /* F4.1 right half fails */

    /* --- BUG-1 NULL-robustness (PRD §8.5 step 2) --- */
    if (match_pattern(NULL, "x", 0) == false) { g_pass++; printf("PASS: match_pattern(NULL,...) = false (no segv)\n"); }
    else { g_fail++; printf("FAIL: match_pattern(NULL,...) wrong/crash\n"); }
    if (match_pattern("x", NULL, 0) == false) { g_pass++; printf("PASS: match_pattern(..,NULL,..) = false\n"); }
    else { g_fail++; printf("FAIL: match_pattern(..,NULL,..) wrong/crash\n"); }

    /* --- hid_notify reassembly + ack + coexistence guard --- */
    uint8_t rep[32]; memset(rep, 0, sizeof(rep));
    rep[0] = 0x81; rep[1] = 0x9F;
    const char *payload = "neovide\x03";
    memcpy(rep + 2, payload, strlen(payload));
    hid_notify(rep, 32);   /* exercises reassembly -> ETX -> dispatch -> raw_hid_send ack=1 */
    g_pass++; printf("PASS: hid_notify reassembled+dispatched (see stderr ack)\n");

    uint8_t bad[32]; memset(bad, 0, sizeof(bad)); bad[0] = 0xAB; bad[1] = 0xCD;
    hid_notify(bad, 32);   /* coexistence guard: ignored, no dispatch */
    g_pass++; printf("PASS: hid_notify ignored non-matching magic header\n");

    /* --- dispatcher ordering: disable-before-scan, deactivate-before-activate --- */
    char m1[] = "neovide";                      /* command only */
    char m2[] = "Chrome\x1d""stuff";            /* layer (chrome*) */
    char m3[] = "totally-unknown";              /* unmatched -> clears state */
    int r1 = process_full_message(m1);          /* expect 1 (on_enable) */
    int r2 = process_full_message(m2);          /* expect 1 (on_disable prev, layer_on 5) */
    int r3 = process_full_message(m3);          /* expect 0 (deactivate, no enable) */
    if (r1==1 && r2==1 && r3==0) { g_pass++; printf("PASS: dispatcher ordering (disable/deactivate/first-match/clear)\n"); }
    else { g_fail++; printf("FAIL: dispatcher ordering r1=%d r2=%d r3=%d (want 1,1,0)\n", r1, r2, r3); }

    printf("\nTotal tests run: %d / passed: %d / failed: %d\n", g_pass+g_fail, g_pass, g_fail);
    return g_fail ? 1 : 0;
}
```

> NOTE: the dispatcher-ordering assertions rely on the `process_full_message` return
> values + the stub `stderr` trace ([stub] layer_on/layer_off, `-> on_enable/on_disable`).
> For a stricter check, instrument `qmk_stubs.c` to record calls into arrays and assert
> the exact sequence (disable fires before any scan; layer_off before layer_on). The
> return-value check above is sufficient for the gate; the trace on stderr is human-auditable.

**`run_notifier_stub_tests.sh` reference body**
```bash
#!/usr/bin/env bash
# P2 stub-compile validation gate for notifier.c (closes RISK-1).
# notifier.c cannot compile standalone (it #includes QMK_KEYBOARD_H); this harness
# substitutes minimal QMK stubs so the receiver/reassembler/dispatcher/F4 logic can
# be validated with plain gcc on a host. See PRP P2.
set -u
cd "$(dirname "$0")"

OBJ=/tmp/notifier_stub.o
DRV=/tmp/test_notifier_dispatch

echo "[1/3] stub-compile notifier.c ..."
gcc -Wall -Wextra -std=c99 \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. \
    -c notifier.c -o "$OBJ"
if [ $? -ne 0 ]; then echo "COMPILE FAILED"; exit 2; fi

echo "[2/3] link dispatch driver ..."
gcc -Wall -std=c99 -I. \
    "$OBJ" qmk_stubs/qmk_stubs.c test_notifier_dispatch.c \
    -o "$DRV"
if [ $? -ne 0 ]; then echo "LINK FAILED"; rm -f "$OBJ"; exit 3; fi

echo "[3/3] run ..."
"$DRV"
rc=$?
fails=$("$DRV" 2>/dev/null | grep -c '^FAIL:' || true)
echo "------------------------------------------------"
echo "notifier dispatch fails=$fails  (exit=$rc)"
rm -f "$OBJ" "$DRV"
[ "$fails" -eq 0 ] && [ $rc -eq 0 ] && { echo "✓ notifier stub-compile gate PASSED"; exit 0; }
echo "✗ notifier stub-compile gate FAILED"; exit 1
```

### Implementation Patterns & Key Details

```c
// PATTERN: stub-compile as the host gate for a QMK-dependent file. notifier.c pulls in
//   QMK via `#include QMK_KEYBOARD_H` (a -D-expanded header name). By defining that
//   macro to a stub header that declares only the symbols notifier.c actually uses
//   (layer_on/layer_off, raw_hid_send, + the fixed-width types), the file compiles on
//   a host with plain gcc — giving F4/dispatcher/reassembly logic a deterministic gate
//   that the 9-suite corpus (which links only pattern_match.c) cannot provide (RISK-1).

// PATTERN: same-translation-unit include. notifier.c does `#include "pattern_match.c"`,
//   so compiling notifier.c ALSO compiles the matcher into the same .o. The harness
//   therefore transitively validates P1 (if match_with_anchors breaks, F4 cases flip).
//   Link ONLY notifier.o + qmk_stubs.c + driver — never a separately-compiled
//   pattern_match.o (duplicate symbols). (PRD §3, §13 #14.)

// PATTERN: weak-default override via DEFINE_* macros. The driver provides non-weak
//   get_command_map/get_layer_map so the __attribute__((weak)) defaults in notifier.o
//   are overridden at link time. Without the macros, all rules miss (size 0). (GOTCHA-5.)

// PATTERN: PASS:/FAIL: + summary (PRD §11.4). Lets run_notifier_stub_tests.sh `grep -c
//   '^FAIL:'` exactly like run_all_tests.sh aggregates the 9 suites.

// ANTI-PATTERN: do NOT move the NULL guard into find_first_delimiter instead. BUG-1's
//   contract is at match_pattern's boundary (PRD §8.5 step 2). Keep find_first_delimiter
//   as-is (it assumes a valid pointer — fine, it's file-local and only called after the
//   guard). The 2-line reorder in match_pattern is the whole fix.

// ANTI-PATTERN: do NOT change any of match_pattern's 4 delimiter branches. The F4
//   matrix is subtle (F4.2 matches the CLASS half only; F4.3 matches the pattern's
//   class half vs the WHOLE message). All 4 cases are encoded as assertions; any
//   "simplification" will flip one. (GOTCHA-10, PRD §8.5, notifier_architecture.md.)

// ANTI-PATTERN: do NOT define CONSOLE_ENABLE in the harness (GOTCHA-4) — it would pull
//   in QMK's print.h. Leave it undefined so the uprintf blocks are skipped.

// ANTI-PATTERN: do NOT touch pattern_match.{c,h}, rules.mk's 2 lines, or any test_*.c /
//   run_all_tests.sh. P2's scope is notifier.{c,h} + the 5 new harness files.
```

### Integration Points

```yaml
SCOPE / PLACEMENT:
  - EDIT notifier.h (1 comment line — DRIFT-1).
  - EDIT notifier.c (BUG-1 reorder + 2 comments + optional sign-compare cleanup).
  - VERIFY rules.mk (the 2 lines; no edit expected).
  - CREATE qmk_stubs/{qmk_keyboard_stub.h, raw_hid.h, qmk_stubs.c}.
  - CREATE test_notifier_dispatch.c (repo root, matching test_*.c naming convention).
  - CREATE run_notifier_stub_tests.sh (repo root, +x).

CONSUMERS (downstream):
  - P3.M1.T1.S1 (acceptance gate): runs BOTH ./run_all_tests.sh (the 9 suites) AND
    ./run_notifier_stub_tests.sh (this harness) as the full P1+P2 gate.
  - The end user's keymap: #includes notifier.h, wires raw_hid_receive -> hid_notify,
    uses DEFINE_SERIAL_* + WT(...). Unchanged by P2 (public API preserved).

BUILD:
  - No change to the QMK build. rules.mk stays 2 lines. The stub harness is a SEPARATE
    host build path (plain gcc); it does not affect qmk compile/flash.

CONFIG / DATABASE / ROUTES: N/A.
```

## Validation Loop

> C project — no ruff/mypy/pytest. Use `gcc` + the new stub harness + the existing
> 9-suite corpus. All commands were run during research and verified green (modulo the
> 2 fixes, which are applied before re-running).

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Stub-compile notifier.c (the ONLY way to type/syntax-check it off-device).
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
# Expected: exit 0. Warnings: at most the 3 pre-existing -Wsign-compare (notifier.c:247,
#   :266, :275) UNLESS the optional Task 4 cleaned them. Zero ERRORS.
rm -f /tmp/notifier_stub.o

# 1b. Confirm BUG-1 fix landed (NULL guard is now the FIRST statement in match_pattern).
awk '/^bool match_pattern\(/{f=1} f{print NR": "$0} f&&/find_first_delimiter\(pattern\)/{exit}' notifier.c \
  | head -5
# Expected: the `if (message == NULL || pattern == NULL) return false;` line appears
#   ABOVE the find_first_delimiter(pattern) line.

# 1c. Confirm DRIFT-1 fixes (no "ASCII 31"/"Unit Separator"/"unit delim" remain).
grep -nE 'ASCII 31|Unit Separator|unit delimit|unit deliem' notifier.h notifier.c && echo "DRIFT-1 still present" \
  || echo "DRIFT-1 fixed (ok)"

# 1d. Confirm rules.mk is the 2-line spec.
diff <(printf 'RAW_ENABLE = yes\nSRC += qmk-notifier/notifier.c\n') rules.mk && echo "rules.mk ok"
```

### Level 2: Unit Tests (the stub harness — closes RISK-1)

```bash
cd /home/dustin/projects/qmk-notifier

# The P2 gate: compile notifier.c w/ stubs, link the F4/dispatcher/hid_notify/NULL driver,
# run it. Expect "fails=0".
./run_notifier_stub_tests.sh
# Expected: "[1/3] ... [2/3] ... [3/3] ..." then "Total tests run: N / passed: N / failed: 0",
#   "notifier dispatch fails=0  (exit=0)", "✓ notifier stub-compile gate PASSED", exit 0.

# Direct re-run of the driver to inspect the stderr stub trace (layer_on/off, on_enable/
# on_disable ordering, raw_hid_send ack):
/tmp/test_notifier_dispatch 2>&1 | less   # (built by the script; or rebuild inline)
```

### Level 3: Integration Testing (P1↔P2 + reference keymap)

```bash
cd /home/dustin/projects/qmk-notifier

# (A) The harness transitively validates P1 (notifier.c #includes pattern_match.c).
#     If P1 is complete (match_with_anchors real), the F4 cases PASS. If P1 regressed,
#     they flip red — that's a P1 bug, NOT a P2 bug; do not patch it in notifier.c.
./run_notifier_stub_tests.sh   # F4 + dispatcher + hid_notify all green

# (B) The 9-suite corpus is UNCHANGED by P2 (notifier.{c,h} edits never touch
#     pattern_match.{c,h} or test_*.c). It must still pass (it is P1's gate).
./run_all_tests.sh
# Expected: "Total tests failed: 0" (or whatever P1's current state is — P2 does not
#   affect it; if it's not 0, that's a P1 issue to resolve in P1).

# (C) Reference-keymap integration (optional deeper check): extend test_notifier_dispatch.c
#     with a 3rd DEFINE block using representative PRD §10.2 rules (e.g.
#     { WT("cs2","Counter-Strike 2") }, { "blender" }, { WT("firefox","*github*") }) and
#     feed realistic messages, asserting the exact layer index fired. This proves the
#     public API + macros behave for a real keymap without flashing hardware.
```

### Level 4: Creative & Domain-Specific Validation

```bash
cd /home/dustin/projects/qmk-notifier

# BUG-1 hardening: fuzz match_pattern with NULL/empty/garbage — must never crash.
cat > /tmp/fuzz_notifier.c <<'EOF'
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
bool match_pattern(const char*p,const char*m,bool cs);
int main(void){
  const char* P[] = {NULL,"","*","\x1d","a\x1db","\x01\x02*",0};
  const char* M[] = {NULL,"","x","a\x1db","longstringwithoutdelim",0};
  for(int i=0;P[i]||(i==0);i++){ if(!P[i])break;
    for(int j=0;M[j]||(j==0);j++){ if(!M[j])break;
      volatile bool r = match_pattern(P[i],M[j],i&1); (void)r; } }
  printf("fuzz: no crash\n"); return 0;
}
EOF
gcc -Wall -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/fz_not.o
gcc -Wall -std=c99 -I. /tmp/fz_not.o qmk_stubs/qmk_stubs.c /tmp/fuzz_notifier.c -o /tmp/fz_not
/tmp/fz_not   # must print "fuzz: no crash" and exit 0 (BUG-1 fix makes NULL safe)
rm -f /tmp/fz_not /tmp/fz_not.o /tmp/fuzz_notifier.c

# Confirm the public API surface of notifier.h is byte-stable except for the comment.
git diff -- notifier.h
# Expected: ONLY line 22 (the GS comment). Nothing else.
git diff -- notifier.c
# Expected: the match_pattern NULL-guard reorder + 2 comments (+ optional sign-compare).
#   No other hunks.
git diff -- rules.mk
# Expected: empty (no change).
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1a: `gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c` → exit 0, zero errors.
- [ ] Level 1b: BUG-1 fixed — NULL guard is the FIRST statement in `match_pattern`.
- [ ] Level 1c: no "ASCII 31"/"Unit Separator"/"unit delim" wording remains (DRIFT-1).
- [ ] Level 1d: `rules.mk` is exactly the 2-line spec.
- [ ] Level 2: `./run_notifier_stub_tests.sh` → exit 0, "fails=0".
- [ ] Level 3A: stub harness F4 cases green (transitively confirms P1).
- [ ] Level 3B: `./run_all_tests.sh` unaffected by P2 (P1's gate, not P2's).
- [ ] Level 4: NULL/empty/garbage fuzz of `match_pattern` → no crash.

### Feature Validation

- [ ] All success criteria from "What" met.
- [ ] PRD §8.5 step 2 (NULL→false) holds (BUG-1 fixed).
- [ ] PRD §8.6 ordering invariants hold: disable-before-scan, deactivate-before-activate,
      first-match-wins, exactly-one-notifier-layer, unmatched-clears-state.
- [ ] PRD §8.7 hid_notify: coexistence guard (0x81 0x9F), ETX reassembly, overflow drop,
      raw_hid_send ack (response[0]=matched).
- [ ] PRD §5/§9 API + rules.mk byte-stable (only DRIFT-1 comment + BUG-1 reorder changed).
- [ ] PRD §13 invariants #1 (magic), #2 (GS/ETX bytes), #3 (RAW_REPORT_SIZE=32),
      #4/#5/#6/#7 (ordering/first-match/one-layer/clear), #13 (weak defaults), #14
      (pattern_match.c dual-compile) all preserved.

### Code Quality Validation

- [ ] notifier.h: only the GS comment changed; API untouched.
- [ ] notifier.c: only the BUG-1 reorder + 2 comments (+ optional sign-compare); no logic
      rewrites; the 4 F4 branches untouched.
- [ ] Harness files follow the repo's `test_*.c` naming + PRD §11.4 framework (PASS:/FAIL:/
      summary) so they aggregate cleanly.
- [ ] Anti-patterns avoided (no guard in find_first_delimiter; no F4-branch edits; no
      CONSOLE_ENABLE in harness; no pattern_match.{c,h}/test_*/run_all_tests.sh edits).

### Documentation & Deployment

- [ ] DRIFT-1 comments now factually correct (GS = ASCII 29 Group Separator).
- [ ] BUG-1 fix has a one-line comment noting it satisfies PRD §8.5 step 2 (optional).
- [ ] `run_notifier_stub_tests.sh` header comments explain WHY stub-compile is needed
      (notifier.c depends on QMK symbols; the 9-suite corpus can't cover it).
- [ ] No README/test-count edits in P2 (README sync is P3.M1.T2.S1).

---

## Anti-Patterns to Avoid

- ❌ Don't reimplement or rewrite `match_pattern`'s 4 F4 branches — the matrix is subtle
      (F4.2 class-half-only; F4.3 pattern-class-vs-whole); only MOVE the NULL guard.
- ❌ Don't move the NULL guard into `find_first_delimiter` — the contract (PRD §8.5 step 2)
      is at `match_pattern`'s boundary; keep `find_first_delimiter` assuming a valid ptr.
- ❌ Don't change the `0x1D` byte — only the *comment* was wrong (DRIFT-1). Byte is authoritative.
- ❌ Don't define `CONSOLE_ENABLE` in the harness — it pulls in QMK `print.h` (not stubbed).
- ❌ Don't compile `pattern_match.c` separately when linking the driver — `notifier.c`
      `#include`s it; a separate `.o` causes duplicate symbols.
- ❌ Don't use a quoted path in `-DQMK_KEYBOARD_H='"stubs/..."'` — use a BARE filename via
      `-Iqmk_stubs` (GOTCHA-1; verified).
- ❌ Don't forget `DEFINE_SERIAL_COMMANDS`/`DEFINE_SERIAL_LAYERS` in the driver — without
      them the weak defaults return size 0 and NO rule matches (GOTCHA-5).
- ❌ Don't touch `pattern_match.{c,h}`, `rules.mk`'s 2 lines, or any `test_*.c` /
      `run_all_tests.sh` — P1 owns those (PRD §17).
- ❌ Don't treat a red F4 case as a P2 bug if P1 regressed — `notifier.c #include`s
      `pattern_match.c`; a broken matcher makes F4 red. Fix it in P1.
- ❌ Don't edit `PRD.md` or any `tasks.json`/`prd_snapshot.md`.

---

**Confidence Score: 9/10** for one-pass implementation success.

Rationale: the three source files already exist as the committed reference
(`81df853`-identical; `git diff` empty), so authorship risk is near-zero — P2 is
verify + 2 surgical fixes + a harness. Both defects (BUG-1 NULL deref → SIGSEGV,
DRIFT-1 wrong comment) were **reproduced during research**, and the **exact fixes
were applied in a throwaway copy and re-verified** (F4 results identical after the
BUG-1 reorder; NULL now returns false). The stub-compile harness was **built, run,
and observed green** in `/tmp/notifier_harness` during research — its exact files,
commands, and test cases are reproduced verbatim above. The one ordering subtlety
(`-DQMK_KEYBOARD_H` bare-filename via `-I`) is flagged as GOTCHA-1 and was resolved
empirically. The −1 reserves for: (a) the optional Task-4 sign-compare cleanup
(implementer may skip it — acceptable), and (b) the dispatcher-ordering assertion
currently keys on return values + the stderr trace rather than a strict call-sequence
recorder (sufficient for the gate, but a stricter instrumentation is a possible
enhancement the implementer might choose).
