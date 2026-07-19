# PRP — P1.M2.T2.S1: Add embedded-NUL sanitization test to test_notifier_dispatch.c

## Goal

**Feature Goal**: Add a regression test to `test_notifier_dispatch.c` that proves
the P1.M2.T1.S1 `sanitize_string` fix works **end-to-end through the public
`hid_notify` entry point**: a HID report whose payload contains an embedded
`0x00` NUL byte (between `"hello"` and `"world"`, terminated by ETX) must have
that NUL **stripped** (PRD F2.3 / §8.2: "strip every other byte") so the
post-NUL portion `"world"` **survives** into the dispatched message. The fix
turns `"hello\0world"` into `"helloworld"`; a command-map entry `"*world*"`
then matches and its callback fires. If a future change reintroduces the old
`while (*read_ptr)` truncation, the dispatched message would be just `"hello"`,
`"*world*"` would not match, the callback would not fire, and this test fails.

**Deliverable**: ONE file modified — `test_notifier_dispatch.c` (repo root),
with THREE additions: (1) a dedicated callback + counter (declared before
`DEFINE_SERIAL_COMMANDS`), (2) a new map row `{ "*world*", on_en_nul, 0 }`, and
(3) a labeled test block in `main()` that builds the NUL-bearing report, calls
`hid_notify`, asserts the callback fired, plus two `ck()` discrimination calls.
No other file changes. Test count grows 11 → 14.

**Success Definition**:
- `test_notifier_dispatch.c` compiles + links + runs with the exact
  `run_notifier_stub_tests.sh` flags → exit 0, **0 `FAIL:`**, summary
  `Total tests run: 14 / passed: 14 / failed: 0`.
- The new block prints `PASS: embedded NUL stripped — post-NUL "world" survived
  to dispatch (F2.3)`; the two `ck()` calls print
  `PASS: match_pattern("*world*","helloworld",cs=0)=1` and
  `PASS: match_pattern("*world*","hello",cs=0)=0`.
- `./run_notifier_stub_tests.sh` prints `notifier dispatch fails=0` AND ends
  `✓ notifier stub-compile gate PASSED` (test_notifier_os still 0 FAIL too).
- The new map row compiles cleanly under the runner's `-Wall` (NULL `on_disable`
  is guarded by `disable_command`).
- No edits to `notifier.c`, `notifier.h`, `pattern_match.*`, `qmk_stubs/*`,
  `test_notifier_os.c`, `run_*.sh`, `PRD.md`, `tasks.json`, `rules.mk`,
  `.gitignore`.

## User Persona (if applicable)

**Target User**: The maintainer running the §11.2D stub-compile gate and any
future contributor touching `sanitize_string` / the reassembly path. The case
is a regression gate: if it flips red, sanitization reverted to truncation.

**Use Case**: After the `sanitize_string` length-bounded fix (P1.M2.T1.S1)
lands, `./run_notifier_stub_tests.sh` rebuilds `test_notifier_dispatch` (which
now includes this case), runs it, and the embedded-NUL behavior is locked in —
no QMK/HID hardware needed.

**User Journey**: developer edits `sanitize_string` → `./run_notifier_stub_tests.sh`
→ the runner re-stub-compiles notifier.c, links test_notifier_dispatch, runs it
→ the NUL-bearing report is driven through `hid_notify` → the post-NUL pattern
matches iff the NUL was stripped → a truncation regression surfaces as a
`FAIL:` line + non-zero exit.

**Pain Points Addressed**: `sanitize_string` is `static` (not directly callable
from a test TU), so the bug could only be observed end-to-end through
`hid_notify`. Without this case, a future "rebuild to spec" could silently
reintroduce `while (*read_ptr)` truncation and no gate would catch it.

## Why

- **Locks the P1.M2.T1.S1 fix as a regression gate.** The code fix changes the
  behavior; this test makes any regression (back to truncation) a hard, visible
  `FAIL:`. The findings doc (Issue 2 §Test approach) prescribes exactly this:
  "Test via `hid_notify`: build a HID report with magic header + embedded `0x00`
  + ETX; define a command map pattern matching the post-NUL portion; if the NUL
  was stripped the callback fires, if it truncated it doesn't. Use the existing
  `ck()` helper pattern."
- **`sanitize_string` is `static` — `hid_notify` is the only observation point.**
  A unit test cannot call `sanitize_string` directly (internal linkage). Driving
  a synthetic NUL-bearing report through the public `hid_notify` entry is the
  only host-testable path that exercises sanitize in context (reassembly →
  sanitize → dispatch).
- **Defense-in-depth verification.** The bug is unreachable via the spec'd
  transport today (ETX is appended before zero-fill; desktop sends printable
  ASCII + GS), but a malformed/hostile frame or USB corruption could inject a
  NUL before ETX. This test proves the robust behavior holds, not just the
  happy path.
- **Contained, surgical, zero-risk.** Adding a callback + a map row + a test
  block to a TEST file cannot affect production behavior. The runner already
  builds+runs `test_notifier_dispatch`; the new cases auto-increment the count
  (per test_infrastructure.md: "Adding tests to existing files ... no script
  changes needed").

## What

Modify ONE file: `test_notifier_dispatch.c`. Three additions:

1. **Callback + counter** — declared AFTER the existing `on_dis_cmd` definition
   and BEFORE `DEFINE_SERIAL_COMMANDS` (the macro references the callback by
   name at file scope):
   ```c
   static int nul_cmd_fired = 0;
   static void on_en_nul(void){ fprintf(stderr, "  -> on_enable (nul-test) fired\n"); nul_cmd_fired++; }
   ```
2. **Map row** — append a 3rd row to the existing `DEFINE_SERIAL_COMMANDS({...})`
   block:
   ```c
   { "*world*", on_en_nul, 0 },   /* embedded-NUL regression (PRD F2.3) */
   ```
   (`on_disable = 0` is safe — `disable_command` guards `on_disable != NULL`.)
3. **Test block in `main()`** — inserted BEFORE the final `printf("\nTotal
   tests run: ...")` summary:
   - reset `nul_cmd_fired = 0`;
   - build a 32-byte report: `{0x81, 0x9F, 'h','e','l','l','o', 0x00,
     'w','o','r','l','d', 0x03}` zero-padded to 32;
   - `hid_notify(rep, 32);`
   - assert `nul_cmd_fired == 1` → `g_pass++` / `PASS:` else `g_fail++` / `FAIL:`;
   - two `ck()` discrimination calls: `ck("*world*", "helloworld", 0, 1)` and
     `ck("*world*", "hello", 0, 0)`.

**How `hid_notify` processes the report (verified)**: coexistence guard checks
`data[0]==0x81 && data[1]==0x9F` → `data += 2; length -= 2` strips the magic
header → reassembly loop appends payload bytes to `msg_buffer` until ETX (0x03)
→ `msg_buffer = "hello\0world"`, `msg_index = 11` → at ETX,
`sanitize_string(msg_buffer, (size_t)msg_index)` strips the NUL →
`msg_buffer = "helloworld"` → `process_full_message("helloworld")` scans the
command map → `"*world*"` matches → `on_en_nul` fires → `nul_cmd_fired == 1`.

(If the OLD truncating `while (*read_ptr)` sanitize were present: `msg_buffer`
would be `"hello"`, `"*world*"` would not match, `nul_cmd_fired` would stay 0,
and the test would `FAIL:` — the intended regression signal.)

### Success Criteria

- [ ] Callback `on_en_nul` + counter `nul_cmd_fired` declared before `DEFINE_SERIAL_COMMANDS`.
- [ ] Map row `{ "*world*", on_en_nul, 0 }` added as a 3rd row in `DEFINE_SERIAL_COMMANDS`.
- [ ] Test block in `main()`: builds the NUL-bearing report, calls `hid_notify(rep, 32)`,
      asserts `nul_cmd_fired == 1`, plus the two `ck()` discrimination calls.
- [ ] `test_notifier_dispatch` runs → 0 `FAIL:`; summary `Total tests run: 14 /
      passed: 14 / failed: 0`; exit 0.
- [ ] `./run_notifier_stub_tests.sh` → `notifier dispatch fails=0`, ends
      `✓ notifier stub-compile gate PASSED`; `test_notifier_os` still 0 FAIL.
- [ ] Compiles clean under the runner's `-Wall -std=c99` (NULL on_disable guarded).
- [ ] No edits to any file except `test_notifier_dispatch.c`.

## All Needed Context

### Context Completeness Check

**Pass.** The exact three additions (callback+counter, map row, test block) are
given verbatim in "Implementation Tasks" and were **empirically validated during
research** by applying them to a /tmp copy of `test_notifier_dispatch.c` and
building+running against the current (fixed) `notifier.c` with the exact
`run_notifier_stub_tests.sh` flags: **14/14 PASS, 0 FAIL, exit 0, 0 warnings**.
The data flow (magic header stripped before reassembly; NUL stripped by sanitize;
`"*world*"` matches `"helloworld"`; NULL `on_disable` guarded) was confirmed by
reading `notifier.c` and the prototype run. An implementer with only this PRP +
repo access can make the three edits and prove them green.

### Documentation & References

```yaml
# MUST READ — the bug + the prescribed test approach
- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/architecture/findings_and_risks.md
  section: "## Issue 2: sanitize_string NUL Stripping Fix" -> "### Test approach"
  why: "Prescribes EXACTLY this test: 'sanitize_string is static — not directly
        callable from test files. Test via hid_notify: (1) build a HID report with
        magic header 0x81 0x9F, payload with an embedded 0x00, terminated by ETX
        0x03; (2) call hid_notify(report, len); (3) define a command map pattern
        matching the post-NUL portion; (4) if NUL stripped the callback fires;
        (5) if NUL truncated it doesn't. Use the existing ck() helper pattern.'"
  critical: "This task IS that prescribed test. The code fix (P1.M2.T1.S1) is its
        prerequisite and is ALREADY LANDED in notifier.c (verified)."

# MUST READ — the file being extended (the ONLY file this task touches)
- file: test_notifier_dispatch.c
  why: "The framework to follow: #include \"notifier.h\"; extern decls for
        hid_notify/process_full_message/match_pattern; DEFINE_SERIAL_COMMANDS +
        DEFINE_SERIAL_LAYERS at FILE SCOPE; static int g_pass/g_fail; the
        ck(p,m,cs,want) helper; manual if/{g_pass++;printf(\"PASS:...\")} blocks
        for non-match_pattern assertions (hid_notify/dispatcher); final
        'Total tests run: N / passed: P / failed: F'; return g_fail?1:0."
  pattern: "The new block uses the SAME manual if/g_pass/g_fail idiom the existing
        hid_notify + dispatcher-ordering blocks use (NOT ck(), because the
        assertion is on a callback counter, not on match_pattern's return). The
        two discrimination checks DO use ck()."
  critical: "DEFINE_SERIAL_COMMANDS is at FILE SCOPE, so the new callback + flag
        MUST be declared BEFORE the macro. Append the map row inside the existing
        macro block (3rd row). Insert the test block in main() BEFORE the summary
        printf. Do NOT reorder existing blocks."

# MUST READ — the data flow (why the magic header is NOT in the sanitized message)
- file: notifier.c
  section: "hid_notify (the entry point) + sanitize_string + process_full_message"
  why: "hid_notify does: coexistence guard (data[0]==0x81 && data[1]==0x9F) ->
        data += 2; length -= 2 (STRIPS the magic header) -> reassembly loop appends
        to msg_buffer until ETX -> sanitize_string(msg_buffer, (size_t)msg_index)
        -> process_full_message(msg_buffer). So the magic header NEVER enters
        msg_buffer / sanitize (it's consumed as the coexistence guard)."
  critical: "This is why the report layout is {0x81,0x9F,'h','e','l','l','o',0x00,
        'w','o','r','l','d',0x03}: bytes [2..] are the PAYLOAD that becomes
        msg_buffer. The NUL at rep[7] is the embedded NUL sanitize must strip.
        (0x81/0x9F are >0x7E so the allowlist WOULD strip them — but they never
        reach sanitize, by design.)"

# MUST READ — the fix under test (CONTRACT — the parallel task; ALREADY LANDED)
- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/P1M2T1S1/PRP.md
  section: "## Goal" + "## Implementation Blueprint"
  why: "Defines sanitize_string(char *str, size_t len) with for(size_t i=0;i<len;i++)
        reading str[i]; allowlist/NULL-guard/NUL-termination preserved; call site
        sanitize_string(msg_buffer, (size_t)msg_index). CONFIRMED LANDED in the
        current notifier.c (sanitize at :50, call at :502). This test consumes it."
  critical: "Do NOT modify notifier.c — the fix is this task's PREREQUISITE, not
        its scope. If the fix were NOT yet landed, this test would FAIL (nul_cmd_fired
        == 0) — which is the intended regression signal. Treat the fixed notifier.c
        as the baseline contract."

# The NULL-on_disable safety (why the map row may use on_disable = 0)
- file: notifier.c
  section: "enable_command (:195) + disable_command (:208)"
  why: "enable_command: 'if (command->on_enable != NULL) command->on_enable();'.
        disable_command: 'if (current_command != NULL && current_command->on_disable
        != NULL) current_command->on_disable();'. Both guard NULL callbacks."
  critical: "So { \"*world*\", on_en_nul, 0 } with on_disable=NULL is SAFE. The
        on_enable fires when the pattern matches; the NULL on_disable is never
        dereferenced. (Matches the P1.M2.T1.S1 research smoke-test idiom
        {\"*suffix*\", on_en, 0, false}.)"

# The macro + struct shape
- file: notifier.h
  section: "command_map_t typedef + DEFINE_SERIAL_COMMANDS macro"
  why: "command_map_t = { const char *pattern; callback_t on_enable; callback_t
        on_disable; const bool case_sensitive; }. case_sensitive is LAST and
        OPTIONAL (omits -> false). DEFINE_SERIAL_COMMANDS({...}) defines
        user_command_map[] at file scope."
  critical: "The existing test uses 3-field rows {pat, on_en, on_dis}. Under the
        runner's -Wall (NOT -Wextra), 3-field rows do NOT warn
        (-Wmissing-field-initializers is -Wextra-gated). So the new 3-field row
        {\"*world*\", on_en_nul, 0} is -Wall-clean and matches the existing style."

# The gate this test must keep green
- file: run_notifier_stub_tests.sh
  why: "Object-compiles notifier.c (-Wall -Wextra -std=c99 -DQMK_KEYBOARD_H=...
        -Iqmk_stubs -I.), links test_notifier_dispatch AND test_notifier_os
        (-Wall -std=c99 -Iqmk_stubs -I.), runs both, greps '^FAIL:', ends PASSED
        iff both 0 fails. Adding cases to test_notifier_dispatch.c needs NO runner
        change (auto-rebuilds, count auto-increments)."
  critical: "The [2/4] dispatch link uses -Iqmk_stubs -I. (already correct). This
        task does NOT touch the runner. Just run it to validate."

# The test-framework catalog (confirms the ck() idiom + auto-increment)
- file: plan/002_c243e735980a/bugfix/001_4a8e381fbf10/architecture/test_infrastructure.md
  section: "## test_notifier_dispatch.c (TARGET for Issue 2 test)"
  why: "Documents: ck(p,m,cs,want) helper; extern decls; DEFINE_SERIAL_COMMANDS
        available; slash-format summary 'Total tests run: N / passed: N / failed:
        N'; exit g_fail?1:0. And: 'Adding tests to existing files ... no script
        changes needed — auto-recompiles, count auto-increments.'"
  critical: "Confirms NO runner edit is needed for this task — only the .c file."

# The intended filter semantics (what 'strip every other byte' means)
- file: PRD.md   (also bugfix prd_snapshot.md)
  section: "### 8.2 sanitize_string(...) — in-place ASCII filter" + "F2.3 (§2)"
  why: "The allowlist: keep byte b iff (b>=32 && b<=126) || b==9 || b==10 || b==13
        || b==0x1D || b==0x03; 'strip every other byte' (F2.3). 0x00 is 'every
        other byte' -> strip it (the fix's job; this test's assertion)."
  critical: "PRD §8.2 documents the OLD signature sanitize_string(char*) — this is
        the documented spec drift (contained: static, one caller). Do NOT edit
        PRD.md; the drift is recorded in findings_and_risks.md §Issue 2."
```

### Current Codebase tree (relevant slice — POST P1.M2.T1.S1 fix)

```bash
notifier.c                 # LANDED (fixed): sanitize_string(char*, size_t) + index loop +
                           #   hid_notify call site sanitize_string(msg_buffer,(size_t)msg_index).
                           #   DO NOT TOUCH (this task's prerequisite, already complete).
notifier.h                 # DEFINE_SERIAL_COMMANDS macro + command_map_t (case_sensitive LAST,
                           #   optional). DO NOT TOUCH.
pattern_match.{c,h}        # unaffected. DO NOT TOUCH.
qmk_stubs/                 # stub harness (layer_on/off, raw_hid_send, stub_get_active_layer).
                           #   DO NOT TOUCH.
test_notifier_dispatch.c   # ← MODIFY (this task): +callback/flag, +map row, +test block. 11 -> 14 cases.
test_notifier_os.c         # OS suite (31/31). DO NOT TOUCH.
run_notifier_stub_tests.sh # the gate (already builds+runs test_notifier_dispatch). DO NOT TOUCH.
run_all_tests.sh           # 9-suite pattern_match corpus — unaffected. DO NOT TOUCH.
PRD.md                     # READ-ONLY.
```

### Desired Codebase tree with files to be changed

```bash
test_notifier_dispatch.c   # MODIFIED: 3 additions (callback+counter, map row, test block).
                           #   Test count 11 -> 14. No other file changes.
```

### Known Gotchas of our codebase & Library Quirks

```c
// CRITICAL — the magic header (0x81 0x9F) is stripped by hid_notify BEFORE
//   reassembly (data += 2; length -= 2), so it NEVER enters msg_buffer / sanitize.
//   The report payload that becomes msg_buffer is bytes [2..] (after the 2 magic
//   bytes). The embedded NUL must be in the PAYLOAD (rep[7] in the layout), not
//   in the magic bytes. (0x81/0x9F are >0x7E so the allowlist WOULD strip them —
//   but they never reach sanitize, by design. Do not 'fix' this.)

// CRITICAL — DEFINE_SERIAL_COMMANDS is at FILE SCOPE. The new callback (on_en_nul)
//   and counter (nul_cmd_fired) MUST be declared BEFORE the macro, because the
//   macro expands to `user_command_map[] = { ..., { "*world*", on_en_nul, 0 } }`
//   which references them by name. Declaring them after the macro = compile error
//   (use-before-declaration). Place them right after the existing on_dis_cmd def.

// CRITICAL — on_disable = 0 (NULL) in the new map row is SAFE. disable_command
//   (notifier.c:208) guards `current_command != NULL && current_command->on_disable
//   != NULL`. enable_command (:195) guards on_enable != NULL. So a NULL on_disable
//   is never dereferenced; the non-NULL on_enable fires on match. (Matches the
//   P1.M2.T1.S1 research smoke-test idiom.)

// GOTCHA — reset nul_cmd_fired = 0 IMMEDIATELY before the hid_notify call. The
//   existing test's earlier dispatches (m1/m2/m3) activate other commands whose
//   on_enable/on_disable are on_en_cmd/on_dis_cmd (NOT on_en_nul), so nul_cmd_fired
//   is 0 entering the block — but resetting it right before the measured dispatch
//   makes the assertion unambiguous regardless of future test reordering.

// GOTCHA — disable-before-scan fires the PREVIOUS command's on_disable at the
//   START of each process_full_message. The existing test's last dispatch (m3=
//   "totally-unknown", unmatched) clears the active command, so entering this
//   block no command is active and the disable is a no-op. Either way, on_en_nul
//   (on_enable) fires at most ONCE per dispatch (first-match-wins, one command).
//   nul_cmd_fired goes 0 -> 1. Do not assert >1.

// GOTCHA — the new map row is the 3rd entry in DEFINE_SERIAL_COMMANDS. First-
//   match-wins means the scan tries "neovide", then WT("*chrome*","*claude*"),
//   then "*world*". For the dispatched "helloworld": "neovide" no match; the WT
//   row's class half "*chrome*" vs "helloworld" no match; "*world*" MATCH. So
//   on_en_nul fires. Do NOT place "*world*" before patterns that could shadow it
//   (none do for "helloworld", but keep it last to be safe).

// GOTCHA — the runner link step uses -Wall (NOT -Wextra). -Wmissing-field-
//   initializers is -Wextra-gated, so the existing 3-field rows (case_sensitive
//   omitted) and your new 3-field row compile clean. Do NOT add a 4th `, false`
//   field unless you also want -Wextra cleanliness (harmless either way; the
//   existing file uses 3-field, so match that style).

// GOTCHA — "*world*" is case-insensitive (case_sensitive omitted -> false).
//   "helloworld" is lowercase, so it matches regardless. Do not set case_sensitive
//   true (would still match here, but the default false matches the existing
//   rows and the item-spec's "case-insensitive").

// GOTCHA — the report MUST be zero-padded to 32 bytes (RAW_REPORT_SIZE). hid_notify
//   loops `for (i=0; i<length; i++)` over all 32 bytes but BREAKs at ETX (rep[13]),
//   so trailing zeros (rep[14..31]) are never appended. memset(rep, 0, sizeof(rep))
//   first, then set the meaningful bytes. (Matches the existing hid_notify test
//   block's idiom.)

// GOTCHA — sanitize_string is STATIC. You CANNOT call it directly from the test.
//   The only host-testable observation is end-to-end through hid_notify (reassembly
//   -> sanitize -> dispatch). That is why the assertion is "the callback fired",
//   not "sanitize returned X".

// GOTCHA — do NOT add a 3rd driver to run_notifier_stub_tests.sh. This task adds
//   CASES to an EXISTING driver (test_notifier_dispatch); the runner already
//   builds+runs it and the count auto-increments (test_infrastructure.md confirms
//   "no script changes needed"). Only NEW .c files need a gcc line.
```

## Implementation Blueprint

### Data models and structure

No production types. The test adds:
- A `static int nul_cmd_fired` counter + a `static void on_en_nul(void)` callback
  (declared before `DEFINE_SERIAL_COMMANDS`).
- A 3rd row in the file-scope `DEFINE_SERIAL_COMMANDS({...})` block.
- A scoped block in `main()` (before the summary printf) that builds the report,
  calls `hid_notify`, and asserts + two `ck()` calls.

### Implementation Tasks (ordered by dependencies)

```yaml
Task 1: MODIFY test_notifier_dispatch.c — ADD the callback + counter
  - PLACE: immediately AFTER the existing `static void on_dis_cmd(void){...}` line
    and BEFORE `DEFINE_SERIAL_COMMANDS({...})`.
  - ADD (verbatim):
        /* embedded-NUL regression callback (PRD F2.3 / §Issue 2): fires only if
         * the post-NUL portion of the message survived sanitization to dispatch. */
        static int nul_cmd_fired = 0;
        static void on_en_nul(void){ fprintf(stderr, "  -> on_enable (nul-test) fired\n"); nul_cmd_fired++; }
  - WHY before the macro: DEFINE_SERIAL_COMMANDS references on_en_nul by name at
    file scope; it must be declared first.

Task 2: MODIFY test_notifier_dispatch.c — ADD the map row
  - PLACE: append a 3rd row INSIDE the existing DEFINE_SERIAL_COMMANDS({...}) block,
    after the WT("*chrome*","*claude*") row.
  - ADD (verbatim):
        { "*world*", on_en_nul, 0 },   /* embedded-NUL regression (PRD F2.3) */
  - on_disable = 0 is SAFE (disable_command guards NULL — verified).

Task 3: MODIFY test_notifier_dispatch.c — ADD the test block in main()
  - PLACE: in main(), immediately BEFORE the final
    `printf("\nTotal tests run: %d / passed: %d / failed: %d\n", ...);` line.
  - ADD the verbatim block from "The exact test block to add" below.
  - PRESERVE: all existing test blocks (F4 matrix, BUG-1 NULL, hid_notify
    reassembly, coexistence guard, dispatcher ordering) and the summary printf.
  - DO NOT reorder existing blocks; DO NOT change the summary format.

Task 4: VERIFY (no edit) — build + run + gate
  - Run Validation Level 1 (compile; 0 warnings).
  - Run Validation Level 2 (run; 14/14, 0 FAIL; run_notifier_stub_tests.sh PASSED).
  - Run Validation Level 3 (git diff confined to test_notifier_dispatch.c).
  - Run Level 4 (regression discrimination + Mode-A doc greps).
```

**The exact test block to add** (verbatim — validated 14/14 during research;
insert before the summary `printf`):

```c
    /* --- Embedded-NUL sanitization through hid_notify (PRD F2.3 / §Issue 2) ---
     * A NUL byte in the reassembled payload must be STRIPPED (not truncate the
     * scan), so bytes after it survive into the dispatched message. hid_notify
     * strips the 2-byte magic header, reassembles the payload "hello\0world"
     * into msg_buffer, and at ETX calls sanitize_string(msg_buffer, msg_index)
     * which strips the NUL -> "helloworld". The map entry "*world*" then matches
     * ONLY because "world" survived. If sanitize truncated at the NUL the
     * message would be "hello" and "*world*" would not match (nul_cmd_fired==0).
     * sanitize_string is static, so this end-to-end hid_notify path is the only
     * host-testable observation point. */
    {
        nul_cmd_fired = 0;
        uint8_t rep[32]; memset(rep, 0, sizeof(rep));
        rep[0] = 0x81; rep[1] = 0x9F;               /* magic header (stripped by hid_notify) */
        rep[2] = 'h'; rep[3] = 'e'; rep[4] = 'l'; rep[5] = 'l'; rep[6] = 'o';
        rep[7] = 0x00;                               /* embedded NUL — must be STRIPPED (F2.3) */
        rep[8] = 'w'; rep[9] = 'o'; rep[10] = 'r'; rep[11] = 'l'; rep[12] = 'd';
        rep[13] = 0x03;                              /* ETX terminator */
        hid_notify(rep, 32);
        if (nul_cmd_fired == 1) { g_pass++; printf("PASS: embedded NUL stripped — post-NUL \"world\" survived to dispatch (F2.3)\n"); }
        else { g_fail++; printf("FAIL: embedded NUL NOT stripped (nul_cmd_fired=%d, want 1) — sanitize truncated at NUL (§Issue 2)\n", nul_cmd_fired); }
    }
    /* (e) Discrimination proof: "*world*" matches the STRIPPED message but NOT
     * the TRUNCATED one — so the callback firing above means sanitize STRIPPED
     * the NUL (dispatched "helloworld"), not that the pattern is loose. */
    ck("*world*", "helloworld", 0, 1);   /* stripped form: matches */
    ck("*world*", "hello", 0, 0);        /* truncated form: does NOT match */
```

### Implementation Patterns & Key Details

```c
// PATTERN: end-to-end observation through the public entry point. sanitize_string
//   is static (internal linkage), so a test TU cannot call it directly. The only
//   host-testable path is hid_notify -> reassembly -> sanitize -> dispatch. The
//   assertion is therefore "the command-map callback fired", not "sanitize
//   returned X". (findings_and_risks.md §Issue 2 Test approach prescribes this.)

// PATTERN: distinguishable callback reveals WHICH map matched. on_en_nul is a
//   SEPARATE function from on_en_cmd, incrementing a SEPARATE counter
//   (nul_cmd_fired). So nul_cmd_fired==1 unambiguously means the "*world*" row
//   matched this dispatch — no confusion with the other rows' callbacks.

// PATTERN: 3-field map row matches the existing file's style. {pat, on_en, on_dis}
//   with case_sensitive omitted (defaults false). -Wall (not -Wextra) does not
//   warn on the missing 4th field. on_disable=0 is guarded by disable_command.

// PATTERN: reset the counter immediately before the measured dispatch. Makes the
//   assertion unambiguous regardless of prior dispatches or future test reordering.

// PATTERN: ck() for match-layer discrimination, manual if/g_pass/g_fail for the
//   callback assertion. The existing hid_notify + dispatcher-ordering blocks use
//   the manual idiom (they assert on dispatch behavior, not match_pattern's
//   return); the new block follows that. The two discrimination checks use ck()
//   because they ARE match_pattern assertions.

// ANTI-PATTERN: do NOT place on_en_nul/nul_cmd_fired AFTER DEFINE_SERIAL_COMMANDS
//   — the macro references on_en_nul at file scope; it must be declared first.

// ANTI-PATTERN: do NOT set the new row's on_disable to a real callback unless you
//   also account for it in the disable-before-scan accounting. NULL (0) is correct
//   and safe (guarded).

// ANTI-PATTERN: do NOT use ck() for the callback assertion — ck() calls
//   match_pattern and compares its return; it cannot observe whether a dispatch
//   callback fired. Use the manual if/nul_cmd_fired idiom.

// ANTI-PATTERN: do NOT add a 3rd driver or any edit to run_notifier_stub_tests.sh
//   — this task adds CASES to an existing driver; the runner auto-rebuilds and
//   the count auto-increments (test_infrastructure.md).

// ANTI-PATTERN: do NOT modify notifier.c — the sanitize fix is this task's
//   PREREQUISITE (P1.M2.T1.S1, already landed), not its scope.

// ANTI-PATTERN: do NOT touch any file other than test_notifier_dispatch.c.
```

### Integration Points

```yaml
NEW CALLBACK + COUNTER (file scope, before DEFINE_SERIAL_COMMANDS):
  - on_en_nul / nul_cmd_fired — referenced by the new map row + the test block.
NEW MAP ROW (inside DEFINE_SERIAL_COMMANDS):
  - { "*world*", on_en_nul, 0 } — 3rd row; matches "helloworld" (the sanitized form).
NEW TEST BLOCK (main(), before summary printf):
  - builds the NUL-bearing report, hid_notify, assert nul_cmd_fired==1 + 2 ck().
CONSUMES (LANDED — unchanged):
  - notifier.c: hid_notify (strips magic, reassembles, sanitize(buf,msg_index),
    process_full_message), sanitize_string (the fix), process_full_message
    (disable-before-scan, command-map scan, enable_command NULL guard).
  - notifier.h: DEFINE_SERIAL_COMMANDS macro + command_map_t (case_sensitive LAST).
BUILD / CONFIG / DATABASE / ROUTES:
  - none. No runner edit (existing driver auto-rebuilds). No rules.mk edit. No
    new files. Pure test-file edit.
```

## Validation Loop

> Toolchain: gcc (C project — no ruff/mypy/pytest). All commands were
> **executed during research** against a /tmp copy of test_notifier_dispatch.c
> with the three additions applied, built with the exact run_notifier_stub_tests.sh
> flags, and run against the current (fixed) notifier.c: **14/14 PASS, 0 FAIL,
> exit 0, 0 warnings**.

### Level 1: Syntax & Style (Immediate Feedback)

```bash
cd /home/dustin/projects/qmk-notifier

# 1a. Object-compile notifier.c (mirrors run_notifier_stub_tests.sh [1/4]); then
#     link test_notifier_dispatch with -Wall (mirrors [2/4]). Expect 0 warnings.
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
    -c notifier.c -o /tmp/notifier_stub.o
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    /tmp/notifier_stub.o qmk_stubs/qmk_stubs.c test_notifier_dispatch.c \
    -o /tmp/test_notifier_dispatch
echo "build rc=$?  (expect 0)"
gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. -c notifier.c -o /tmp/o 2>&1 | grep -c 'warning:'  # expect 0
gcc -Wall -std=c99 -Iqmk_stubs -I. /tmp/notifier_stub.o qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -o /tmp/o2 2>&1 | grep -c 'warning:'  # expect 0
rm -f /tmp/o /tmp/o2

# 1b. Confirm the three additions are present.
grep -q 'static int nul_cmd_fired = 0;' test_notifier_dispatch.c && echo "✓ counter present"
grep -q 'static void on_en_nul(void)' test_notifier_dispatch.c && echo "✓ callback present"
grep -q '{ "\*world\*", on_en_nul, 0 }' test_notifier_dispatch.c && echo "✓ map row present"
grep -q 'embedded NUL stripped' test_notifier_dispatch.c && echo "✓ test block present"
grep -q 'ck("\*world\*", "helloworld", 0, 1)' test_notifier_dispatch.c && echo "✓ discrimination ck() present"

# 1c. Confirm the callback/counter are declared BEFORE DEFINE_SERIAL_COMMANDS.
awk '/on_en_nul/{cb=NR} /DEFINE_SERIAL_COMMANDS/{if(!mc)mc=NR} END{print "callback line="cb" macro line="mc; exit (cb<mc)?0:1}' test_notifier_dispatch.c \
  && echo "✓ callback declared before macro" || echo "ERROR: callback must precede the macro"

# 1d. Confirm NO other file changed.
git status --porcelain | grep -vE '^\?\? plan/' | grep -vE 'M test_notifier_dispatch.c' || true
# Expected: nothing except ` M test_notifier_dispatch.c` (+ plan/ PRP/research).
```

### Level 2: Component Test (THE PRIMARY GATE)

```bash
cd /home/dustin/projects/qmk-notifier

# Run the test binary directly. MUST report 0 FAIL and the new PASS lines.
/tmp/test_notifier_dispatch 2>/dev/null | tee /tmp/td_run.log
echo "fails=$(grep -c '^FAIL:' /tmp/td_run.log)  (expect 0)"
grep 'Total tests run' /tmp/td_run.log
# Expected (exact summary): "Total tests run: 14 / passed: 14 / failed: 0"; fails=0.
# Expected new PASS lines:
#   PASS: embedded NUL stripped — post-NUL "world" survived to dispatch (F2.3)
#   PASS: match_pattern("*world*","helloworld",cs=0)=1
#   PASS: match_pattern("*world*","hello",cs=0)=0
/tmp/test_notifier_dispatch >/dev/null 2>&1; echo "exit=$?  (expect 0)"
rm -f /tmp/test_notifier_dispatch /tmp/td_run.log
```

### Level 3: Integration (the §11.2D stub-compile gate — no regression)

```bash
cd /home/dustin/projects/qmk-notifier

# The full stub gate: builds+runs BOTH test_notifier_dispatch AND test_notifier_os.
./run_notifier_stub_tests.sh 2>/dev/null | grep -E 'fails=|PASSED|FAILED'
echo "runner exit=${PIPESTATUS[0]}  (expect 0)"
# Expected:
#   notifier dispatch fails=0  (exit=0)
#   notifier os fails=0        (exit=0)
#   ✓ notifier stub-compile gate PASSED
# (test_notifier_os is untouched and must remain 0 FAIL — no regression.)

# Also confirm the §11.2A pattern_match corpus is unaffected (this task touches
# no matcher file). Quick exit-code check:
./run_all_tests.sh >/dev/null 2>&1; echo "run_all_tests exit=$?  (expect 0)"
```

### Level 4: Creative & Domain-Specific Validation (regression discrimination)

```bash
cd /home/dustin/projects/qmk-notifier

# 4a. Discrimination proof: the two ck() calls prove "*world*" distinguishes
#     STRIPPED ("helloworld", match) from TRUNCATED ("hello", no-match). So the
#     callback firing (nul_cmd_fired==1) conclusively means the NUL was STRIPPED.
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    /tmp/notifier_stub.o qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -o /tmp/td 2>/dev/null
/tmp/td 2>/dev/null | grep -E 'match_pattern."\*world\*"'
# Expected:
#   PASS: match_pattern("*world*","helloworld",cs=0)=1
#   PASS: match_pattern("*world*","hello",cs=0)=0

# 4b. Mode-A documentation: the test block cites PRD F2.3 / §Issue 2 (item-spec 5
#     says "no user-facing docs", but the inline test comment should cite the
#     rationale so a future reader understands WHY the case exists).
grep -q 'F2.3' test_notifier_dispatch.c && echo "✓ cites PRD F2.3"
grep -q '§Issue 2\|Issue 2' test_notifier_dispatch.c && echo "✓ cites §Issue 2"
grep -q 'stripped' test_notifier_dispatch.c && echo "✓ documents strip (not truncate) semantics"

# 4c. End-to-end behavior locked: if sanitize regressed to while(*read_ptr)
#     truncation, this test would FAIL (nul_cmd_fired==0). The current PASS proves
#     the fix is in effect. (No need to revert notifier.c — the discrimination
#     logic + the prototype already confirmed fired=1 with the fix, fired=0 without.)
rm -f /tmp/td /tmp/notifier_stub.o
```

## Final Validation Checklist

### Technical Validation

- [ ] Level 1: object-compile + link → 0 warnings; counter/callback/map row/test
      block present; callback declared before DEFINE_SERIAL_COMMANDS; no other
      file changed.
- [ ] Level 2: `test_notifier_dispatch` → 14/14, 0 `FAIL:`, exit 0; the 3 new
      PASS lines present.
- [ ] Level 3: `./run_notifier_stub_tests.sh` → dispatch fails=0 AND os fails=0,
      `✓ notifier stub-compile gate PASSED`, exit 0; `run_all_tests.sh` exit 0.
- [ ] Level 4: discrimination ck() lines present; F2.3/§Issue 2 cited in the
      inline comment; strip-vs-truncate semantics documented.

### Feature Validation

- [ ] An embedded `0x00` in the HID payload is stripped → post-NUL "world"
      survives into the dispatched message.
- [ ] The command-map callback fires (`nul_cmd_fired == 1`) iff the NUL was
      stripped (regression to truncation → 0 → FAIL).
- [ ] The discrimination `ck("*world*","helloworld")=1` and `ck("*world*","hello")=0`
      lock the semantics that make the callback assertion meaningful.
- [ ] No regression to test_notifier_os (still 0 FAIL) or the 9 pattern_match suites.

### Code Quality Validation

- [ ] Follows the existing `test_notifier_dispatch.c` framework (g_pass/g_fail,
      PASS:/FAIL:, manual if-block for dispatch assertions, ck() for match_pattern,
      slash-format summary, `return g_fail?1:0`).
- [ ] Callback/counter declared before the file-scope macro; map row appended
      inside the existing block; test block before the summary printf.
- [ ] 3-field map row (`on_disable=0`) matches the file's style and is -Wall-clean.
- [ ] No anti-patterns (see below).

### Documentation & Deployment

- [ ] Inline test comment cites PRD F2.3 / §Issue 2 and explains strip-vs-truncate.
- [ ] No new env vars / config / build-system / runner changes.
- [ ] README test-count sync is a separate doc task (NOT this task — out of scope).

---

## Anti-Patterns to Avoid

- ❌ Don't declare `on_en_nul`/`nul_cmd_fired` AFTER `DEFINE_SERIAL_COMMANDS` —
  the macro references `on_en_nul` at file scope; declare the callback first.
- ❌ Don't use `ck()` for the callback-firing assertion — `ck()` tests
  `match_pattern`'s return; it cannot observe a dispatch callback. Use the manual
  `if (nul_cmd_fired == 1) { g_pass++; ... }` idiom (matching the existing
  hid_notify/dispatcher blocks).
- ❌ Don't set the new map row's `on_disable` to a real callback unless you also
  account for it in the disable-before-scan flow. `0` (NULL) is correct and safe
  (`disable_command` guards `on_disable != NULL`).
- ❌ Don't place `"*world*"` BEFORE patterns that could shadow it for "helloworld"
  (none do, but keep it last). First-match-wins means scan order matters.
- ❌ Don't forget to `memset(rep, 0, sizeof(rep))` before setting the meaningful
  bytes — the report must be 32 bytes (RAW_REPORT_SIZE); trailing zeros are never
  read (the loop breaks at ETX) but the buffer must be full.
- ❌ Don't modify `notifier.c` — the `sanitize_string` fix (P1.M2.T1.S1) is this
  task's **prerequisite** (already landed), not its scope.
- ❌ Don't edit `run_notifier_stub_tests.sh` — adding cases to an existing driver
  needs no runner change (it auto-rebuilds; the count auto-increments).
- ❌ Don't add a 4th `, false` field to the map row unless you're being -Wextra-
  clean — the existing file uses 3-field rows and the runner uses -Wall (not
  -Wextra), so 3-field is the consistent style.
- ❌ Don't touch `notifier.h`, `notifier.c`, `pattern_match.*`, `qmk_stubs/*`,
  `test_notifier_os.c`, `run_all_tests.sh`, `PRD.md`, `tasks.json`,
  `prd_snapshot.md`, `rules.mk`, or `.gitignore`. Only `test_notifier_dispatch.c`.
- ❌ Don't interpret the item-spec's literal "(e) ... 'hello' does NOT match alone"
  as the test — "hello" DOES match "helloworld" as a prefix, so it's not a useful
  negative control. Use the item's "Or alternatively" branch: `"*world*"` (matches
  stripped, not truncated) + the two `ck()` discrimination calls. (Documented in
  the research notes.)

---

## Confidence Score: 10/10

The deliverable is a precise single-file edit (`test_notifier_dispatch.c`) with
three additions — (1) a callback `on_en_nul` + counter `nul_cmd_fired` declared
before the file-scope `DEFINE_SERIAL_COMMANDS`, (2) a 3rd map row
`{ "*world*", on_en_nul, 0 }`, and (3) a labeled test block in `main()` that
builds the NUL-bearing HID report, calls `hid_notify`, asserts `nul_cmd_fired==1`,
plus two `ck()` discrimination calls. The exact code is given verbatim above and
was **empirically validated during research** by applying it to a /tmp copy of
the test and building+running against the current (fixed) `notifier.c` with the
exact `run_notifier_stub_tests.sh` flags: **14/14 PASS, 0 FAIL, exit 0, 0
warnings**. The critical data-flow detail (the magic header is stripped by
`hid_notify` before reassembly, so only the payload `"hello\0world"` enters
`msg_buffer`/sanitize) was confirmed by reading `notifier.c`; the NULL-`on_disable`
safety was confirmed (`disable_command`/`enable_command` both guard NULL); and
the discrimination (`"*world*"` matches stripped `"helloworld"` but not truncated
`"hello"`) was confirmed by the prototype run. The fix under test (P1.M2.T1.S1)
is **already landed** in `notifier.c` (verified). The runner needs no change
(existing driver auto-rebuilds). Dependencies and boundaries (no conflict with
P1.M1.T1.S1/test_pattern_match.c; notifier.c is prerequisite-not-scope) are
explicit. No external dependencies are added.