# System Context — QA Bugfix Scope

## Project Overview

**qmk-notifier** is a QMK firmware module that receives window-title focus-change
strings from the host PC over raw HID, matches them against a user-defined
pattern map (layer_map / command_map), and activates QMK layers / fires QMK
commands accordingly. It also supports a typed-command protocol (§4.6) for
host-authoritative operations: `QUERY_INFO`, `QUERY_CALLBACK`, `SET_OS`, and
`APPLY_HOST_CONTEXT`.

### Architecture Summary

```
Host PC (window manager)
  │
  │ raw HID 32-byte reports: [0x81][0x9F][payload...][0x03 ETX]
  ▼
hid_notify()  ◄── notifier.c (947 lines)
  │
  ├── Legacy path: process_full_message() → pattern_match() → board dispatch
  │     (layer_on/off, command on_enable/on_disable)
  │
  └── Typed path (data[2]==0xF0): handle_typed_command()
        ├── QUERY_INFO (0x01): capability handshake
        ├── QUERY_CALLBACK (0x02): callback name discovery
        ├── SET_OS (0x03): set current_os for multi-OS map selection
        └── APPLY_HOST_CONTEXT (0x05): host-driven layer/callback override
```

### Key Constants (notifier.c / notifier.h)
- `RAW_REPORT_SIZE` = 32 (HID report size)
- `MSG_BUFFER_SIZE` = 256 (reassembled message buffer)
- `ETX_TERMINATOR` = "\x03" (message terminator)
- `NOTIFY_CMD_DISCRIMINATOR` = 0xF0 (typed-command marker at data[2])
- `NFA_MAX_PATTERN` = 128 (firmware) / 2048 (host/test default in pattern_match.c)

### State Planes (§13 invariant 21)
- **Board state**: `activated_layer`, `current_command`, `current_os` — driven by legacy path
- **Host state**: `host_layer`, `host_cb_enabled[]` — driven by typed commands
- **Reassembly state**: `typed_mode`, `typed_literal_remaining`, `msg_index`, `dropping` — all `static`, persist across hid_notify() calls for multi-report framing

## Bugfix Scope

This changeset addresses 4 issues found by end-to-end creative QA:

| # | Severity | Issue | Module |
|---|----------|-------|--------|
| 1 | **Major** | Malformed/truncated typed command permanently breaks legacy routing | `notifier.c` reassembly loop |
| 2 | Minor | README falsely reports SET_OS tests as broken/pending | `README.md` |
| 3 | Minor | Matcher tested at NFA_MAX_PATTERN=2048 but firmware runs at 128 | test fidelity gap |
| 4 | Minor | `has_been_queried` written but never read | `notifier.c` dead state |

## Test Infrastructure (two independent harnesses)

### Harness A: `run_all_tests.sh` (pattern-matcher corpus, 9 suites)
- Compiles `pattern_match.c` **directly** → `NFA_MAX_PATTERN` defaults to **2048**
- Result contract: greps `Total tests run:` / `Tests passed:` / `Tests failed:`
- Current: 2023/2023 passing

### Harness B: `run_notifier_stub_tests.sh` (notifier stub suites, 3 drivers)
- Compiles `notifier.c` (which `#define NFA_MAX_PATTERN 128`) → runs at firmware budget **128**
- Shared `notifier_stub.o` linked into 3 drivers: dispatch, os, host
- Result contract: greps `^FAIL:` + exit code
- Current: dispatch 14/14, os 31/31, host 64/64