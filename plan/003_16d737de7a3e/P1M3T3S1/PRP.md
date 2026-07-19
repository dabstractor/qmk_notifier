name: "P1.M3.T3.S1 — Add Host-Side Rules & Typed Commands section to README.md; update Setup / Running Tests / What-this-does-NOT-change"
description: >
  Mode B (changeset-level) documentation task. Edit ONE file — `README.md` — to
  document the host-side-rules + typed-command feature end-to-end (PRD §4.6/§4.7/§14),
  which was implemented by P1.M1–P1.M3.T1 and is exercised by the three-binary
  `run_notifier_stub_tests.sh` gate (P1.M3.T2.S1, already in place on disk).
  Adds: (a) a `## Host-Side Rules & Typed Commands` section after Multi-OS; (b) a
  host-rules bullet in the Features list; (c) a Setup note
  (`DEFINE_HOST_CALLBACKS` + host negotiates via `QUERY_INFO`); (d) a Running-Tests
  update naming `test_notifier_host` + an HONEST Current-Test-Status block; (e)
  removes the now-stale "host rules are planned future work" bullet in the Multi-OS
  "What this does NOT change" and adds a host-rules backward-compat bullet.
  No source/test/runner files are touched. The README must NEVER fabricate a green
  test status — see the decision rule in Task 5.

---

## Goal

**Feature Goal**: The repo `README.md` documents the Host-Side Rules & Typed
Commands feature (PRD §4.6 / §4.7 / §14) at the same depth and in the same
house style as the existing Multi-OS Configuration section, so an end user
understands (1) that it is strictly opt-in, (2) how a host ruleset coexists with
board `DEFINE_SERIAL_*` rules, (3) the at-a-glance typed-command namespace, (4)
the per-window stack-vs-replace model, and (5) the host-authoritative `SET_OS`.
All pre-existing README claims that contradict the now-shipped feature are
corrected, and the Running-Tests/Status blocks reflect the three-binary gate
honestly.

**Deliverable**: ONE file MODIFIED — `README.md` (repo root). No other file is
created or changed by this task (research notes already live under
`plan/003_16d737de7a3e/P1M3T3S1/research/`).

**Success Definition**:
- A new `## Host-Side Rules & Typed Commands` section exists, placed immediately
  after `## Multi-OS Configuration` and before `## Companion Projects`, with the
  subsections listed in Task 2.
- The `## Features` bullet list has a new host-rules bullet linking to
  `#host-side-rules--typed-commands`.
- `## Setup` mentions host-rules users add `DEFINE_HOST_CALLBACKS({ … })` and the
  host negotiates via `QUERY_INFO`.
- `## Running Tests` describes `test_notifier_host` as the third binary built+run
  by `run_notifier_stub_tests.sh`, and `### Current Test Status` reports the
  **actual** numbers from running the gates (no fabricated green — see Task 5).
- The Multi-OS `### What this does NOT change` no longer claims host rules are
  "planned future work"; it points to the new section.
- The new section's own `### What this does NOT change` states the legacy string
  wire protocol and the pattern matcher are untouched, and board
  `DEFINE_*`/`DEFINE_*_OS` rules keep working (stacked or replaced per the host's
  per-window decision).
- All cross-reference anchors resolve; markdown renders cleanly; the file is
  valid CommonMark/GFM.

## User Persona (if applicable)

**Target User**: A QMK keymap author who has already integrated qmk-notifier (the
legacy string path) and now wants to use the desktop-driven host rules — and a
contributor reading the README to understand what changed in this changeset.

**Use Case**: The user reads the README top-to-bottom, learns host rules are
opt-in, copies the `DEFINE_HOST_CALLBACKS({...})` snippet, and understands their
existing `DEFINE_SERIAL_*` rules keep working alongside (stacked or replaced
per window by the host).

**Pain Points Addressed**: Today the README says host rules are "planned future
work" (stale — they now ship), and the typed-command namespace / `clear_board` /
host-authoritative `SET_OS` are entirely undocumented. This task closes that gap.

## Why

- **The feature shipped with zero user-facing docs.** P1.M1–P1.M3.T1 implemented
  `notifier.h` API + `notifier.c` handlers + `test_notifier_host.c`; the README
  still describes the module as string-only. This is the changeset-level (Mode B)
  doc task that depends on all of them.
- **A stale claim must be corrected.** The Multi-OS section's "What this does NOT
  change" explicitly says host rules are future work — that now contradicts the
  shipped code and misleads readers.
- **Coexistence is non-obvious.** The hardest thing for a user is understanding
  how a host ruleset stacks vs replaces board rules per window (`clear_board`).
  The PRD spells this out (§14); the README must too.
- **Zero code risk.** Documentation-only edit; no build/test behavior changes.

## What

Edit `README.md` only. The five edit groups (mapped to Tasks 1–5 below): (1) add a
Features bullet; (2) add the full `## Host-Side Rules & Typed Commands` section;
(3) add a Setup note; (4) remove the stale Multi-OS "future work" bullet and add a
forward-pointer; (5) update Running Tests + Current Test Status (honestly).

### Success Criteria

- [ ] `## Host-Side Rules & Typed Commands` section present after Multi-OS, before
  Companion Projects, with subsections: lead/opt-in, How it works (two state
  planes), Enabling host rules, Typed-command namespace table, Stack vs replace
  (clear_board), Host-authoritative OS (SET_OS), Backward compatibility, What this
  does NOT change.
- [ ] `DEFINE_HOST_CALLBACKS` + `host_callback_t` row shape documented with a
  copy-pasteable `keymap.c` snippet.
- [ ] The 4 typed commands (`QUERY_INFO`/`QUERY_CALLBACK`/`SET_OS`/
  `APPLY_HOST_CONTEXT`), the `0xF0` discriminator, the `0x51` response marker, and
  `proto_ver=2` appear in an at-a-glance table.
- [ ] Stack (`clear_board=0`) vs Replace (`clear_board=1`) explained, pointing to
  `qmkonnect/spec/HOST_RULES.md` for the host-side per-rule
  `disable_firmware_config` decision.
- [ ] `SET_OS` documented as host-authoritative while connected (wins over
  `OS_DETECTION`); host layers reserved ≥ 224 stated.
- [ ] Features bullet + Setup note + Multi-OS forward-pointer added.
- [ ] Running Tests names `test_notifier_host`; Current Test Status is honest.
- [ ] No claim that host rules are "future work" remains anywhere in the file.

## All Needed Context

### Context Completeness Check

_Before writing this PRP, validated: "If someone knew nothing about this codebase,
would they have everything needed to implement this successfully?"_ — YES. The
exact feature contract (API, constants, handler behavior, stack/replace, SET_OS)
is captured in `host_feature_contract.md`; the README house style + section order
+ the stale bullet to remove are in `readme_conventions.md`; the test-status
honesty rule is in `test_status_state.md`. All three are summarized inline below.

### Documentation & References

```yaml
# MUST READ — the canonical spec the README must mirror (already expanded in your task prompt)
- docfile: PRD.md
  why: §4.6 (typed-command namespace), §4.7 (host-authoritative SET_OS), §14
    (Host-Side Rules & Typed Commands), §10.1 (Integration steps). The README
    section is a user-facing distillation of these three.
  critical: §14 "Coexistence" paragraph defines stack-vs-replace precisely.
    §4.6 command table is the at-a-glance namespace. §4.7 last paragraph
    ("Orthogonality") is the key invariant: multi-OS selection and the typed
    namespace are INDEPENDENT state.

# MUST READ — the actual implemented contract (the README must match the code, not just the PRD)
- file: notifier.h
  why: The exact `host_callback_t` struct, `DEFINE_HOST_CALLBACKS` macro, the
    accessor pair, and every typed-command constant (`NOTIFY_CMD_*`,
    `NOTIFY_RESPONSE_MARKER 0x51`, `NOTIFY_CMD_DISCRIMINATOR 0xF0`,
    `NOTIFY_PROTO_VER 2`, `HOST_LAYER_BASE 224`, `HOST_CALLBACK_MAX 32`).
  pattern: copy symbol names VERBATIM into the README (do not paraphrase constants).
  gotcha: `LAYER_UNSET = 255` is defined in notifier.c, NOT notifier.h — the
    README should refer to it as "`0xFF` (255)" descriptively, not as a header macro.

- file: notifier.c
  why: The four handlers (handle_typed_command ~L651-740), set_host_layer (~L252),
    apply_host_callbacks (~L283), apply_os_change (~L595), and the hid_notify
    dispatch fork (~L768-824) — these are the behaviors the README describes.
  section: QUERY_INFO reply layout is `[proto_ver=2][feature_flags][callback_count][board_rules_present]`;
    feature_flags = `0x01 | (registry_present ? 0x02 : 0)`.

# MUST READ — README house style + the exact stale bullet to remove
- file: plan/003_16d737de7a3e/P1M3T3S1/research/readme_conventions.md
  why: Section order, placement decision, style conventions, and the computed
    GitHub anchor `#host-side-rules--typed-commands` (DOUBLE hyphen where `&` was).

# MUST READ — feature contract distilled from code + PRD
- file: plan/003_16d737de7a3e/P1M3T3S1/research/host_feature_contract.md
  why: The 9-point contract (opt-in guarantee, API, constants table, the 4
    handlers, two state planes, stack/replace driver, handshake+legacy-fallback,
    framing, setup note). This is the source of truth for the README prose.

# MUST READ — honesty rule for the test-status block (do NOT fabricate green)
- file: plan/003_16d737de7a3e/P1M3T3S1/research/test_status_state.md
  why: Documents the verified SET_OS framing blocker (test_notifier_host emits 7
    SET_OS FAILs today; owned by notifier.c/P1.M2, NOT this task) and the decision
    rule for writing an honest Current-Test-Status.

# The host-side design the README points to (external repo; link only, do not read)
- url: https://github.com/dabstractor/qmkonnect/blob/main/spec/HOST_RULES.md
  why: Canonical host-side design (rules.toml, the per-rule
    disable_firmware_config flag, the matcher). The README tells readers the
    firmware offers both stack+replace and the HOST chooses per window via
    clear_board; the details of that choice live here.

# The companion crates already linked by the README (do not duplicate, just reference)
- url: https://github.com/dabstractor/qmkonnect
  why: Desktop daemon (already in the README "Companion Projects" section).
```

### Current Codebase tree (relevant subset)

```bash
qmk-notifier/
├── README.md            ← THE ONLY FILE THIS TASK EDITS (≈396 lines today)
├── notifier.h           ← host_callback_t, DEFINE_HOST_CALLBACKS, NOTIFY_CMD_* (READ for exact symbols)
├── notifier.c           ← handle_typed_command + 4 handlers + set_host_layer + apply_host_callbacks + apply_os_change (READ for exact behavior)
├── pattern_match.{c,h}  ← UNCHANGED — README must say so
├── rules.mk             ← UNCHANGED (host rules need NO rules.mk change)
├── run_notifier_stub_tests.sh  ← ALREADY extended to 3 binaries (dispatch/os/host) — describe as-is
├── run_all_tests.sh     ← the 9-suite pattern_match gate (unchanged)
├── test_notifier_host.c ← the NEW host test binary (64 assertions; blocks i–iv SET_OS, v–viii AHC, coexist, multi-rep)
├── test_notifier_dispatch.c / test_notifier_os.c  ← unchanged legacy gates
└── qmk_stubs/           ← stub harness (unchanged by docs)
```

### Desired Codebase tree with files to be added/changed

```bash
qmk-notifier/
└── README.md            ← MODIFIED (Tasks 1–5). No new files.
```

### Known Gotchas of our codebase & Library Quirks

```markdown
# CRITICAL: GitHub anchor for "Host-Side Rules & Typed Commands" is
#   #host-side-rules--typed-commands   (DOUBLE hyphen — the "&" is stripped,
#   its surrounding spaces each become a hyphen). The Features bullet and the
#   Multi-OS forward-pointer MUST use this exact anchor or the link is dead.
#   Verify by rendering and clicking the heading after editing.

# CRITICAL: The Multi-OS section's "What this does NOT change" has a THIRD bullet
#   that says host rules are "planned future work". This is now FALSE and MUST be
#   replaced (Task 4) — leaving it makes the README contradict itself.

# CRITICAL (honesty): test_notifier_host has 7 known SET_OS FAILs today (verified
#   blocker, owned by notifier.c/P1.M2, NOT this task — see test_status_state.md).
#   The "Current Test Status" block MUST reflect the ACTUAL gate output. Do NOT
#   write "test_notifier_host 64/64 green" unless run_notifier_stub_tests.sh
#   actually ends PASSED. See Task 5 decision rule.

# GOTCHA: LAYER_UNSET (255) and the host_layer/host_cb_enabled[] statics live in
#   notifier.c (implementation detail), not notifier.h. Describe them
#   behaviorally ("0xFF clears the host layer"), not as user-facing macros.

# GOTCHA: Host rules need NO rules.mk change — the feature is always compiled in;
#   the registry is simply empty (weak {NULL,0} accessors) when
#   DEFINE_HOST_CALLBACKS is omitted. Do NOT invent a make flag for it.

# GOTCHA: The pattern matcher is the single source of truth for match semantics;
#   the host-side matcher lives in qmkonnect, NOT here. The README must say the
#   FIRMWARE matcher is untouched (it is).
```

## Implementation Blueprint

### Data models and structure

Not applicable — this is a documentation task. The "data" is prose blocks edited
into `README.md`. The symbol/constant surface is fixed by `notifier.h` and must be
copied verbatim (see `host_feature_contract.md` §3).

### Implementation Tasks (ordered by dependencies)

The five edit groups are INDEPENDENT (different regions of `README.md`) and may be
applied in any order, but the order below matches the file top→bottom for review
clarity. Use the `edit` tool with precise `oldText` anchors (the README is ~396
lines; anchor on unique surrounding prose, not whole sections).

```yaml
Task 1: ADD a host-rules bullet to the `## Features` list
  - FILE: README.md
  - FIND: the existing multi-OS Features bullet (starts "- **Optional per-OS maps**"
    and ends "See [Multi-OS Configuration](#multi-os-configuration).")
  - ACTION: INSERT a new bullet IMMEDIATELY AFTER it (same indentation/style).
  - CONTENT (mirror the Multi-OS bullet's voice; use the exact computed anchor):
      - **Host-side rules & typed commands (opt-in)** — a desktop host (QMKonnect)
        can push layer/callback decisions over Raw HID without reflashing, via a
        typed-command namespace (`QUERY_INFO`/`QUERY_CALLBACK`/`SET_OS`/
        `APPLY_HOST_CONTEXT`). The host declares its OS authoritatively and applies
        per-window host layers + callbacks that stack on top of, or replace, your
        board `DEFINE_SERIAL_*` rules. Strictly opt-in: a keymap without
        `DEFINE_HOST_CALLBACKS` is byte-for-byte unchanged. See
        [Host-Side Rules & Typed Commands](#host-side-rules--typed-commands).
  - NAMING/ANCHOR: link target MUST be `#host-side-rules--typed-commands` (double hyphen).

Task 2: ADD the `## Host-Side Rules & Typed Commands` section (the bulk of the work)
  - FILE: README.md
  - PLACEMENT: INSERT a new `## Host-Side Rules & Typed Commands` heading + body
    IMMEDIATELY AFTER the end of the `## Multi-OS Configuration` section and
    IMMEDIATELY BEFORE the `## Companion Projects` heading.
  - ANCHOR for the `edit` tool: anchor on the blank line(s) just before
    "## Companion Projects".
  - CONTENT: the full section given verbatim in "Task 2 — full section prose"
    below. It MUST include these subsections, in order:
      (a) lead paragraph — opt-in nature + one-line "what it does";
      (b) `### How it works` — two independent state planes (board vs host),
          the `0xF0` discriminator + `0x51` response marker;
      (c) `### Enabling host rules` — DEFINE_HOST_CALLBACKS snippet + the
          host negotiates via QUERY_INFO (no rules.mk change);
      (d) `### The typed-command namespace at a glance` — the command table;
      (e) `### Stack vs replace per window` — clear_board + pointer to
          qmkonnect/spec/HOST_RULES.md;
      (f) `### Host-authoritative OS` — SET_OS wins over OS_DETECTION while
          connected; host layers reserved >= 224;
      (g) `### Backward compatibility (the guarantee)` — no DEFINE_HOST_CALLBACKS
          => weak {NULL,0} => identical to today;
      (h) `### What this does NOT change` — legacy string wire protocol +
          pattern matcher untouched; board DEFINE_*/DEFINE_*_OS keep working
          (stacked or replaced per the host's per-window decision).
  - DEPENDENCIES: copy symbol names from notifier.h (host_feature_contract.md §2-3).

Task 3: ADD a host-rules Setup note to `## Setup`
  - FILE: README.md
  - FIND: the end of `### 2. Include the module in your keymap` (the block showing
    `raw_hid_receive → hid_notify` + the "Multi-OS users also override
    process_detected_host_os_kb ..." sentence).
  - ACTION: APPEND a short paragraph after that sentence (same subsection, before
    `### 3. Update your rules.mk`).
  - CONTENT (one paragraph, no new heading):
      Host-rules users additionally define a named callback registry with
      `DEFINE_HOST_CALLBACKS({ … })` in `keymap.c` (see
      [Host-Side Rules & Typed Commands](#host-side-rules--typed-commands)). No
      `rules.mk` change is required for host rules — the host (QMKonnect)
      negotiates capability automatically at connect via the `QUERY_INFO` typed
      command, then drives `SET_OS` + `APPLY_HOST_CONTEXT`. Omit the macro and the
      module behaves exactly as before.
  - NAMING/ANCHOR: link target MUST be `#host-side-rules--typed-commands`.

Task 4: REPLACE the stale Multi-OS "future work" bullet
  - FILE: README.md
  - FIND: in `## Multi-OS Configuration` → `### What this does NOT change`, the
    bullet beginning "- **Host-provided OS and host-side rules are planned future
    work**, not implemented. Today the OS comes only ..." (it is the THIRD bullet
    of that list).
  - ACTION: REPLACE that entire bullet with a forward-pointer:
      - **Host-provided OS and host-side rules are now implemented** — see
        [Host-Side Rules & Typed Commands](#host-side-rules--typed-commands).
        While a host is connected it declares its OS authoritatively via `SET_OS`
        (taking precedence over this firmware-side heuristic); with no host
        connected, `OS_DETECTION` remains the only OS signal exactly as described
        here.
  - CRITICAL: do not leave the "planned future work" wording anywhere in the file
    (grep `-i "planned future work"` must return nothing after the edit).

Task 5: UPDATE `## Running Tests` + `### Current Test Status` (HONEST — read this fully)
  - FILE: README.md
  - EDIT 5a (the runner description): FIND "links it into **two** host test
    binaries, and runs both:" and the two-bullet list that follows
    (`test_notifier_dispatch` ... / `test_notifier_os` ...).
    CHANGE "two" → "three" and "both" → "all three", and APPEND a third bullet:
      - **`test_notifier_host`** (64 cases) — the typed-command / host-rules
        contract (§4.6 / §4.7 / §14): `QUERY_INFO` capability handshake +
        `has_been_queried` timing, `QUERY_CALLBACK` name discovery (valid +
        out-of-range), `SET_OS` (response layout, OS-map selection, F9
        clear-on-change, idempotence), `APPLY_HOST_CONTEXT` STACK vs REPLACE
        (`clear_board`), callback-diff ordering (disable-before-enable),
        host-layer clear (`0xFF`), legacy-string/typed coexistence, non-magic
        discard, and multi-report typed reassembly.
  - EDIT 5b (Current Test Status): this block currently claims "all suites green
    ... 2023/2023 ... 14/14 + 31/31". RUN BOTH GATES FIRST, then choose wording
    by the decision rule in test_status_state.md (reproduced here):
      1. `./run_all_tests.sh`          → read the 9-suite aggregate.
      2. `./run_notifier_stub_tests.sh`→ read the three `fails=N (exit=M)` lines.
      - IF test_notifier_host prints `fails=0 (exit=0)` (framing flaw fixed
        upstream): state `test_notifier_host` 64/64 green alongside dispatch
        14/14 + os 31/31 and the 2023/2023 pattern corpus.
      - ELSE (the verified CURRENT state — 7 SET_OS FAILs, owned by
        notifier.c/P1.M2, NOT this task): describe each of the three binaries'
        COVERAGE factually, state ONLY the observably-green suites in the status
        line (run_all_tests 9 suites + dispatch 14/14 + os 31/31), and add ONE
        honest sentence that `test_notifier_host`'s four SET_OS blocks are
        pending the upstream ETX-collision framing fix in notifier.c (the other
        57 assertions pass). Do NOT print a fabricated "64/64 green".
    - FORBIDDEN by the honesty rule (see Anti-Patterns): claiming green when the
      gate ends `✗ ... FAILED`; rounding counts up; editing any non-README file
      to change the numbers.
  - NAMING: keep the existing "Performance Impact" + "production-ready" tail
    sentence if it still holds; do not delete unrelated status prose.
```

### Task 2 — full section prose (the authoritative block to insert)

Insert this block between `## Multi-OS Configuration` and `## Companion Projects`.
Adapt phrasing to match the existing voice, but keep every symbol/constant
VERBATIM from `notifier.h` (see `host_feature_contract.md`).

````markdown
## Host-Side Rules & Typed Commands

Host-side rules are an **opt-in overlay** on the legacy string path. A desktop
host (QMKonnect) can push layer/callback decisions over Raw HID **without
reflashing**: it declares its OS authoritatively and applies per-window host
layers + callbacks that stack on top of, or replace, your board
`DEFINE_SERIAL_*` rules. A keymap that does not define `DEFINE_HOST_CALLBACKS`
is **byte-for-byte unchanged** — the feature is structural (no `#ifdef`), so the
module links and behaves identically to today.

### How it works

The firmware keeps **two independent state planes**:

- **Board state** — `activated_layer`, the current command, and `current_os`,
  driven by the legacy string path (`process_full_message`) and defined via
  `DEFINE_SERIAL_*` / `DEFINE_SERIAL_*_OS`. This is everything the rest of this
  README describes.
- **Host state** — a separate host layer (`host_layer`, independent of the board
  `activated_layer`) and a host-callback enable set, driven by **typed
  commands**. Defined via `DEFINE_HOST_CALLBACKS`.

The two planes touch each other only at two explicit seams: the `clear_board`
flag (an explicit board teardown inside `APPLY_HOST_CONTEXT`) and `SET_OS`
(which updates the shared `current_os`). Otherwise they are orthogonal — a
legacy string send never touches host state, and a typed command never touches
board state (except via `clear_board`).

**Discriminator.** The byte after the magic header selects the path:
`data[2] == 0xF0` routes to the typed path (checked on the first report of a
message only); anything else is the legacy string (unchanged). Because the
sanitizer allows only `0x20–0x7E`, `0xF0` can never begin a real matched string,
so a host that sends only legacy strings coexists unchanged. Typed responses are
prefixed `0x51` (≥2), which is distinct from the legacy `0`/`1` match-bool, so
the host disambiguates without ambiguity.

### Enabling host rules

Host rules need **no `rules.mk` change**. In `keymap.c`, define a named callback
registry:

```c
static void mute_on(void)  { /* unmute / show mute OSD */ }
static void mute_off(void) { /* restore */ }

DEFINE_HOST_CALLBACKS({
    { "mute", &mute_on, &mute_off },
});
```

Each row is `{ name, on_enable, on_disable }` (`on_disable` may be `NULL`). The
`id` is the array index, stable per build. At connect, the host sends
`QUERY_INFO`; if the firmware is typed-capable (`proto_ver == 2`) it replies
`[0x51][0x01][2][flags][count][board_rules_present]`, the host then sweeps
`QUERY_CALLBACK(i)` for `i in 0..count` to build its `name → id` map, and finally
drives `SET_OS` + `APPLY_HOST_CONTEXT`. Omit the macro and `count` is `0`, the
`callback_registry` feature bit is clear, and the module behaves exactly as
before.

### The typed-command namespace at a glance

Typed commands are ETX-framed and may span multiple 32-byte reports, exactly like
legacy strings: `[0x81][0x9F][0xF0][cmd_id][args…][0x03]`. Responses are 32-byte
reports: `[0x51][cmd_echo][payload…][padding]`.

| `cmd_id` | Name | Request args | Response payload (after `[0x51][cmd_echo]`) |
|---|---|---|---|
| `0x01` | `QUERY_INFO` | none | `[proto_ver][feature_flags][callback_count][board_rules_present]` |
| `0x02` | `QUERY_CALLBACK` | `[index]` | `[index][name bytes, NUL-padded]` (or `[index][0x00]` if absent) |
| `0x03` | `SET_OS` | `[os_byte]` | `[ack]` (`1` = applied) |
| `0x05` | `APPLY_HOST_CONTEXT` | `[layer][flags][count][id0][id1]…` | `[ack]` (`1` = applied) |

- `proto_ver` = `2` here (a legacy string-only firmware reports `1`). Firmware-owned.
- `feature_flags` = `0x01` (`APPLY_HOST_CONTEXT` supported) OR'd with `0x02` when a
  callback registry is present. (`0x04` is reserved for future VIA-coexist.)
- `callback_count` = the size of your `DEFINE_HOST_CALLBACKS` registry (`0` if absent).
- `board_rules_present` = `1` iff **any** board map (default or any per-OS map) is
  non-empty — a single bit; per-OS granularity is not exposed.
- `os_byte`: `0 UNSURE · 1 LINUX · 2 WINDOWS · 3 MACOS · 4 IOS` (mirrors QMK's
  `os_variant_t`).

### Stack vs replace per window

The firmware offers **both** stack and replace semantics; the **host chooses per
window** via the `clear_board` flag (bit 0 of `APPLY_HOST_CONTEXT.flags`):

- **Stack** (`clear_board = 0`): the host first sends the legacy **string** (the
  board runs its rules → sets its layer/command → replies), then sends
  `APPLY_HOST_CONTEXT{layer, callbacks, clear_board=0}`. Board layer/command stay
  active; the host layer stacks above; board callbacks fire first, host callbacks
  after.
- **Replace** (`clear_board = 1`): the host sends **only**
  `APPLY_HOST_CONTEXT{layer, callbacks, clear_board=1}` (no string). The firmware
  `deactivate_layer()`s its board layer and `disable_command()`s its board command,
  then applies the host layer + callbacks. Board rules are inert for that window
  and re-engage normally on the host's next string send.

The host computes `clear_board` from its own per-rule `disable_firmware_config`
flag in `rules.toml` (a window is *replace* iff **every** matched rule is
disabling). The host-side design — the `rules.toml` schema, the matcher, and the
per-window decision — is canonical in **`qmkonnect/spec/HOST_RULES.md`**; this
firmware's `pattern_match.c` (and its test corpus) remain the single source of
truth for match semantics. Host callback transitions use
**disable-before-enable** ordering (mirroring the board path): newly-out-of-set
ids fire `on_disable`, then newly-in-set ids fire `on_enable`.

### Host-authoritative OS

While a host is connected and has sent `SET_OS`, the host's value is
**authoritative** for `current_os` — it takes precedence over the
`OS_DETECTION` heuristic described in [Multi-OS Configuration](#multi-os-configuration).
`SET_OS` updates `current_os` through the same internal seam as
`notifier_set_os`, so an OS **change** clears notifier state (disable command +
deactivate layer) before recording the new OS, exactly as a firmware-side OS
change does. With no host connected, `OS_DETECTION` remains the only OS signal.

Host layers are **reserved ≥ 224** (`HOST_LAYER_BASE`), so they resolve above
board layers under QMK's highest-layer-wins rule; `0xFF` (255) clears the host
layer.

### Backward compatibility (the guarantee)

If you do **not** define `DEFINE_HOST_CALLBACKS`, the module provides weak
`{NULL, 0}` accessors — there is no registry, `callback_count` is `0`, the
callback-registry feature bit is clear, and the module links and behaves
identically to the pre-host-rules firmware. A host that sends only legacy strings
never triggers the typed path. You cannot break an existing keymap by *not*
opting in.

### What this does NOT change

- **The legacy string wire protocol is unchanged.** The companion app still sends
  `class\x1Dtitle` as 32-byte Raw HID reports with the `0x81 0x9F` magic header
  and `ETX` terminator; the `process_full_message` dispatch is byte-for-byte
  identical.
- **The pattern matcher is untouched.** `pattern_match.{c,h}` is the single source
  of truth for match semantics; the host-side matcher lives in `qmkonnect`, not here.
- **Board `DEFINE_*` / `DEFINE_*_OS` rules keep working.** When the host stacks,
  they run first; when the host replaces, they are inert for that window and
  re-engage on the next string send. The choice is the host's, per window.
````

### Implementation Patterns & Key Details

```markdown
# PATTERN: cross-section links use GitHub auto-anchors. The new section's anchor is
#   #host-side-rules--typed-commands  (double hyphen — verify by rendering).
#   The Multi-OS anchor #multi-os-configuration already works; reuse it in the
#   SET_OS subsection's back-link.

# PATTERN: every symbol is wrapped in backticks: `DEFINE_HOST_CALLBACKS`,
#   `APPLY_HOST_CONTEXT`, `clear_board`, `data[2]`, `0xF0`, `0x51`, `proto_ver`,
#   `host_layer`, `current_os`, `notifier_set_os`, `OS_DETECTION`. This matches
#   the existing Multi-OS section's heavy use of inline code.

# PATTERN: the "opt-in overlay" framing + "Backward compatibility (the guarantee)"
#   + "What this does NOT change" triad is the EXACT structure the Multi-OS
#   section uses. Mirror it verbatim in voice so the two feature sections read as
#   siblings.

# CRITICAL: do NOT invent a rules.mk flag, a QMK config option, or a new public
#   function for host rules. The feature is always compiled in; opting in is
#   purely a `DEFINE_HOST_CALLBACKS` macro presence/absence.

# CRITICAL: do NOT describe the firmware as choosing stack-vs-replace. The
#   FIRMWARE offers both; the HOST chooses per window via clear_board. Getting
#   this agency right is the single most important correctness point in the section.
```

### Integration Points

```yaml
FILES:
  - modify: "README.md (repo root) — Tasks 1-5"
  - read-only: "notifier.h, notifier.c (source of truth for symbols/behavior)"
  - untouched: "every .c/.h/.sh file, rules.mk, PRD.md, all tasks.json, all prd_snapshot.md"

CROSS-REFERENCES TO ADD:
  - "#host-side-rules--typed-commands"  (Features bullet, Setup note, Multi-OS forward-pointer)
  - "#multi-os-configuration"           (back-link from the SET_OS subsection)
  - "https://github.com/dabstractor/qmkonnect/blob/main/spec/HOST_RULES.md"
                                        (Stack-vs-replace subsection — host-side design)

CROSS-REFERENCES TO REMOVE/REPLACE:
  - the Multi-OS "planned future work" bullet (Task 4) — MUST be gone after editing
```

## Validation Loop

### Level 1: Markdown hygiene (Immediate Feedback)

```bash
# No trailing-whitespace / CRLF regressions introduced.
cd /home/dustin/projects/qmk-notifier
git diff --check README.md

# The two anchors we rely on exist exactly once as headings.
grep -nc '^## Host-Side Rules & Typed Commands$' README.md          # => 1
grep -nc '^## Multi-OS Configuration$' README.md                    # => 1
grep -nc '^## Companion Projects$' README.md                        # => 1

# The stale claim is gone.
grep -in 'planned future work' README.md                            # => (no output)

# The new section is placed BETWEEN Multi-OS and Companion Projects.
awk '/^## Multi-OS Configuration/{m=1} /^## Host-Side Rules & Typed Commands/{h=1; print "host section after multi-os:", (m==1)} /^## Companion Projects/{print "companion after host:", (h==1)}' README.md
# Expected: "host section after multi-os: 1" then "companion after host: 1"

# Symbol names appear (sanity: the feature is actually documented).
grep -c 'DEFINE_HOST_CALLBACKS'        README.md                    # => >= 3
grep -c 'APPLY_HOST_CONTEXT'           README.md                    # => >= 2
grep -c 'clear_board'                  README.md                    # => >= 3
grep -c '0xF0'                         README.md                    # => >= 1
grep -c '0x51'                         README.md                    # => >= 1
grep -c 'proto_ver'                    README.md                    # => >= 1
grep -c 'SET_OS'                       README.md                    # => >= 2
```

### Level 2: Anchor & link resolution (Component Validation)

```bash
# Every in-page link target we add must resolve to a real heading slug.
python3 - <<'PY'
import re
txt = open('README.md').read()
# Collect GitHub-style slugs from headings.
heads = re.findall(r'^#+\s+(.+)$', txt, re.M)
def slug(h):
    h = h.lower()
    h = re.sub(r'[^a-z0-9 \-]', '', h)      # strip '&' etc.
    h = h.replace(' ', '-')
    return h
slugs = {slug(h) for h in heads}
links = re.findall(r'\]\((#[^)]+)\)', txt)
bad = [l for l in links if l.lstrip('#') not in slugs and not l.startswith('#http')]
print("dead in-page anchors:", bad or "none")
assert not bad, f"dead anchors: {bad}"
# Specifically assert the two we care about:
assert "host-side-rules--typed-commands" in slugs, "new section anchor missing/wrong"
assert "multi-os-configuration" in slugs
print("OK: anchors resolve")
PY
```

### Level 3: Render check (System Validation)

```bash
# If a markdown renderer is available, render and eyeball the new section.
# (Optional — the grep/python checks above are the real gate.)
command -v mdcat >/dev/null && mdcat README.md | sed -n '/Host-Side Rules/,/Companion Projects/p' | head -60
# OR, with glow:
command -v glow >/dev/null && glow README.md >/tmp/readme-render.txt && grep -A40 'Host-Side Rules' /tmp/readme-render.txt | head -50
```

### Level 4: Documentation honesty gate (CRITICAL — Domain-Specific Validation)

```bash
# Run BOTH test gates and RECORD the actual output (do NOT fabricate status).
cd /home/dustin/projects/qmk-notifier
echo "=== run_all_tests.sh ==="   ; ./run_all_tests.sh 2>&1 | tail -5
echo "=== run_notifier_stub_tests.sh ===" ; ./run_notifier_stub_tests.sh 2>&1 | tail -12

# Then VERIFY the README's "Current Test Status" block matches reality:
#   - If the runner above ended "✓ ... PASSED", the README MAY state
#     test_notifier_host green (with the observed count).
#   - If it ended "✗ ... FAILED" (7 SET_OS FAILs — the verified current state),
#     the README MUST NOT claim test_notifier_host green; it must use the honest
#     wording from Task 5 EDIT 5b (option B).
# A quick consistency check the implementer performs manually: open README.md,
# read the Current Test Status block, and confirm every number is supported by
# the gate output above. No rounding up. No silent omission of the SET_OS caveat.
```

## Final Validation Checklist

### Technical Validation
- [ ] Level 1 markdown hygiene: `git diff --check README.md` clean; heading-count
      greps return the expected `1`s; `grep -in 'planned future work'` empty.
- [ ] Level 2 anchor resolution: the python script prints "OK: anchors resolve"
      AND asserts both `host-side-rules--typed-commands` and
      `multi-os-configuration` slugs exist.
- [ ] Level 4 honesty: README Current Test Status matches the actual
      `run_notifier_stub_tests.sh` / `run_all_tests.sh` output (no fabricated green).

### Feature Validation
- [ ] `## Host-Side Rules & Typed Commands` present, between Multi-OS and
      Companion Projects, with all eight subsections from Task 2.
- [ ] `DEFINE_HOST_CALLBACKS` snippet copy-pasteable; `host_callback_t` row shape
      `{ name, on_enable, on_disable }` documented.
- [ ] Typed-command table lists all four commands + the `0xF0`/`0x51`/`proto_ver=2`
      facts.
- [ ] Stack (`clear_board=0`) vs Replace (`clear_board=1`) explained with the HOST
      as the per-window chooser; link to `qmkonnect/spec/HOST_RULES.md` present.
- [ ] `SET_OS` documented as host-authoritative while connected; host layers ≥ 224
      stated; back-link to Multi-OS present.
- [ ] Features bullet + Setup note + Multi-OS forward-pointer all added and all
      link to `#host-side-rules--typed-commands`.

### Code Quality Validation
- [ ] README house style matches the Multi-OS section (opt-in framing, bold
      lead-in labels, heavy inline-code backticks, "Backward compatibility
      (the guarantee)" + "What this does NOT change" triad).
- [ ] No invented API surface (no fake rules.mk flag / QMK config / public fn).
- [ ] No source/test/runner file modified — `git status --short` shows ONLY
      `README.md` (plus the pre-existing research notes under plan/, which are
      not tracked changes to the module).

### Documentation & Deployment
- [ ] Symbols copied verbatim from `notifier.h` (no paraphrased constants).
- [ ] Cross-references (internal anchors + the qmkonnect HOST_RULES.md URL) resolve.

---

## Anti-Patterns to Avoid

- ❌ Don't claim host rules are "future work" anywhere — they ship in this changeset.
- ❌ Don't describe the firmware as choosing stack-vs-replace — the HOST chooses
  per window via `clear_board`; the firmware offers both.
- ❌ Don't invent a `rules.mk` flag / QMK config option / new public function for
  host rules — opting in is purely the `DEFINE_HOST_CALLBACKS` macro.
- ❌ Don't paraphrase `notifier.h` constants (`0xF0`, `0x51`, `NOTIFY_CMD_*`,
  `HOST_LAYER_BASE 224`) — copy them verbatim.
- ❌ Don't fabricate test status. If `run_notifier_stub_tests.sh` ends FAILED,
  the README must say so honestly (Task 5 EDIT 5b option B). Masking the SET_OS
  framing flaw in the docs is strictly forbidden.
- ❌ Don't edit `notifier.c` / `notifier.h` / `test_notifier_host.c` /
  `run_notifier_stub_tests.sh` to make numbers pass — that is out of scope (owned
  by P1.M2 / P1.M3.T2.S1). This task edits ONLY `README.md`.
- ❌ Don't duplicate the qmkonnect host-side design in the README — link to
  `qmkonnect/spec/HOST_RULES.md` instead.
- ❌ Don't break the existing Multi-OS section — only remove/replace the one stale
  "future work" bullet (Task 4); leave its other bullets intact.

---

## Confidence Score: 8/10

**Why 8, not higher**: the feature contract is fully pinned by code + PRD + the
research notes, and the prose block for the main section is provided verbatim, so
one-pass success is highly likely. The one residual risk is the **honest
test-status wording** (Task 5 EDIT 5b), which depends on the ACTUAL gate output at
implementation time — the verified-current state has 7 SET_OS FAILs (an upstream
`notifier.c` blocker, not this task's to fix). The decision rule handles both the
green and red cases explicitly, but it requires the implementer to run the gates
and choose honestly rather than transcribe a fixed string. The other minor risk is
the double-hyphen anchor `#host-side-rules--typed-commands`, which the Level-2
validation script verifies automatically.