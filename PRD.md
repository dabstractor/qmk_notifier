# SPECIFICATION — qmk_notifier (QMK Firmware Module)

**Master Product Requirements & Engineering Specification**
Target: a single document complete enough for a developer agent to one-shot this
entire codebase from scratch. Read it top to bottom before writing any code.

> **Product scope.** This is the complete product & engineering specification
> for the qmk_notifier firmware module. The module MUST provide: raw-HID
> reception with the `0x81 0x9F` coexistence guard and multi-report reassembly
> (§4); the Thompson-NFA pattern matcher (§7); per-OS command/layer map selection
> — a strict opt-in overlay where a default-only keymap is byte-identical to a
> single-map firmware (§2 F8/F9, §5.5, §8.6–§8.7, §10.3); and host-side-rules
> support — the typed-command namespace (§4.6), host-authoritative `SET_OS`
> (§4.7), and the board-side host-rules machinery (`clear_board`, the
> `host_layer`/host-callback trackers, the named callback registry) in §14. The
> typed-command wire contract is canonical in §4.6; the host-side orchestration
> is in `qmkonnect/spec/HOST_RULES.md` and the transport in the `qmk-notifier`
> crate `PRD.md`.

> **Scope of "this codebase."** This document specifies **qmk_notifier**
> (underscore) — the **C firmware module** that runs on a QMK keyboard. It is
> *not* the desktop app (that is `QMKonnect`, repo `dabstractor/qmkonnect`) and
> *not* the Rust transport crate (`qmk-notifier`, hyphen, repo
> `dabstractor/qmk-notifier`). Those are the other two nodes of the ecosystem
> (see §1.2). A developer rebuilding **this** repo produces only the four source
> files in §3.

---

## Table of Contents

1. [Product Overview](#1-product-overview)
2. [Functional Requirements](#2-functional-requirements)
3. [Repository Layout & Deliverables](#3-repository-layout--deliverables)
4. [The Wire Protocol Contract (most important section)](#4-the-wire-protocol-contract-most-important-section)
5. [File Specification: `notifier.h`](#5-file-specification-notifierh)
6. [File Specification: `pattern_match.h`](#6-file-specification-pattern_matchh)
7. [File Specification: `pattern_match.c` — the matcher](#7-file-specification-pattern_matchc--the-matcher)
8. [File Specification: `notifier.c` — the receiver/dispatcher](#8-file-specification-notifierc--the-receiverdispatcher)
9. [File Specification: `rules.mk`](#9-file-specification-rulesmk)
10. [The User-Facing API & Reference Keymap](#10-the-user-facing-api--reference-keymap)
11. [Build & Test (the acceptance gate)](#11-build--test-the-acceptance-gate)
12. [Non-Functional Requirements](#12-non-functional-requirements)
13. [Key Invariants a Dev Must Preserve](#13-key-invariants-a-dev-must-preserve)
14. [Host-Side Rules & Typed Commands](#14-host-side-rules--typed-commands)
15. [Appendix A — Pattern-Semantics Reference Table](#15-appendix-a--pattern-semantics-reference-table)
16. [Appendix B — Constants Reference](#16-appendix-b--constants-reference)
17. [Appendix C — File Sizes & Live Source of Truth](#17-appendix-c--file-sizes--live-source-of-truth)
18. [Community Module Distribution (Planned Migration)](#18-community-module-distribution-planned-migration)

---

## 1. Product Overview

### 1.1 What qmk_notifier is

**qmk_notifier** is a QMK **module** (consumed as a git submodule inside a QMK
keymap) that turns a mechanical keyboard into a *context-aware* keyboard. It
listens on QMK's Raw HID interface for short messages sent by a companion
desktop app, **pattern-matches** each message against user-defined rules, and on
a match either **switches the active keymap layer** and/or **invokes a user
callback** (e.g. toggle a vim-mode module).

Concretely, the desktop app (QMKonnect) sends the *foreground window's* identity
on every focus change as the string

```
<application_class>\x1D<window_title>
```

qmk_notifier reassembles that string from 32-byte HID reports, sanitizes it, and
runs it through two user-supplied lookup tables — `command_map` (pattern →
callback) and `layer_map` (pattern → QMK layer). The user writes those tables
with two macros, `DEFINE_SERIAL_COMMANDS({ ... })` and
`DEFINE_SERIAL_LAYERS({ ... })`, directly in their `keymap.c`. Changing which
apps trigger which behavior currently requires editing the keymap and
**reflashing** (the host-side-rules feature, §14, lets the host change rules
without reflashing).

A single keyboard often travels between macOS, Windows, and Linux, where the
*same application reports entirely different `application_class` strings* (e.g.
a terminal is `Terminal`/`iTerm` on macOS, `WindowsTerminal` on Windows,
`alacritty`/`kitty` on Linux; a browser is `Google Chrome` on macOS vs
`chrome`/`Chrome_WidgetWin_1` elsewhere). To support this, qmk_notifier lets a
keymap define **per-OS** command and layer maps in addition to an optional
**default** map. The detected host OS selects which maps are consulted:
**OS-specific rules are scanned first and take precedence; the default map is a
fallback** for any rule the OS-specific map does not match (§2 F8). A single-OS
user defines only the default map and observes zero behavior change — multi-OS
is a strict opt-in overlay. The OS signal is **pushed** into the module by the
keymap (`notifier_set_os`); the module never calls QMK's `detected_host_os()`
itself, so it carries no link dependency on the OS-detection subsystem.

### 1.2 The broader ecosystem (a dev must understand all three)

| Project | Repo | Language | Role |
|---|---|---|---|
| **qmk_notifier** ← *this repo* | `dabstractor/qmk_notifier` | C | On-keyboard **receiver + matcher + actor**. Consumed as a submodule by the user's keymap. |
| **QMKonnect** | `dabstractor/qmkonnect` | Rust | Cross-platform **desktop daemon**. Detects the foreground window and *sends* the `class\x1Dtitle` string. Never decides behavior. |
| **qmk-notifier** (hyphen) | `dabstractor/qmk-notifier` | Rust | **Transport crate** QMKonnect links. Owns the wire framing (magic header, 32-byte chunking, ETX terminator, device cache). |
| **qmk_firmware** | `qmk/qmk_firmware` | C | Upstream QMK. The host firmware; qmk_notifier plugs into it via `RAW_ENABLE`. |

> **Naming note (read once):** `qmk_notifier` (underscore) = the firmware C
> module (this repo). `qmk-notifier` (hyphen) = the Rust transport crate. The two
> halves communicate over a tiny fixed wire protocol (§4). Any byte wrong and the
> halves will not talk.

### 1.3 Design philosophy

- **Coexistence-safe.** The module inspects only messages beginning with the
  magic bytes `0x81 0x9F` and ignores everything else, so other Raw HID modules
  (e.g. `qmk-field-kit`, VIA) can share the same interface.
- **Minimal resource use.** A single static 256-byte reassembly buffer, a tiny
  stack-allocated NFA, no dynamic allocation on the receive hot path (one
  `malloc` per `pattern_match` call, freed before return).
- **Backward-compatible by construction.** The pattern matcher is a strict
  superset of the original glob matcher; existing rules keep matching identically.
- **Robust to garbage.** Non-ASCII bytes are stripped; oversized messages are
  dropped silently; NULL pointers return `false`; no input can crash it.
- **OS-aware, optionally.** Multi-OS map selection (§2 F8) is an opt-in overlay:
  a keymap that defines only the default `DEFINE_SERIAL_*` maps is byte-for-byte
  equivalent to the pre-multi-OS firmware. The OS signal is **pushed** into the
  module by the keymap (`notifier_set_os`); the module never calls QMK's
  `detected_host_os()` itself, so it carries no link dependency on the
  OS-detection subsystem.

---

## 2. Functional Requirements

The module MUST satisfy all of the following. Each is verified by a test in §11
or by an invariant in §13.

### F1 — Raw HID reception & coexistence
- F1.1 Receive data via `hid_notify(uint8_t *data, uint8_t length)` called from
  the keymap's `raw_hid_receive`.
- F1.2 **Discard** any report whose first two bytes are not `0x81` then `0x9F`,
  or whose `length < 2`. This is the coexistence guard.
- F1.3 After discarding the 2-byte header, reassemble the remaining bytes into a
  static 256-byte buffer across multiple reports until an **ETX** (`0x03`).

### F2 — Message reassembly & sanitization
- F2.1 Buffer exactly `MSG_BUFFER_SIZE = 256` bytes; NUL-terminate on ETX.
- F2.2 On buffer overflow (`msg_index >= 255`), reset the index and drop the
  message (do **not** write past the buffer).
- F2.3 `sanitize_string`: keep only bytes `0x20`–`0x7E` (printable ASCII) plus
  `0x09` (tab), `0x0A` (LF), `0x0D` (CR), `0x1D` (GS delimiter), `0x03` (ETX).
  Strip every other byte. (ETX is processed before sanitize runs, so its presence
  in the allow-list is belt-and-suspenders.)

### F3 — Pattern matching (`pattern_match`)
A public function `bool pattern_match(const char *pattern, const char *str, bool case_sensitive)`
supporting the constructs in §7 and §15. Must be **non-crashing on any input**
(NULL → `false`) and **linear-time** (Thompson NFA, no catastrophic backtracking).

### F4 — Delimiter-aware matching (`match_pattern`, internal to `notifier.c`)
- F4.1 A pattern containing the GS delimiter (`0x1D`) is a **two-part** pattern:
  both the class half and the title half must match the corresponding halves of
  the message.
- F4.2 A pattern **without** a delimiter matches the **class part only** when the
  message has a delimiter (firmware parity with the desktop's class-only rules).
- F4.3 A pattern with a delimiter matched against a message **without** one
  matches the pattern's class half against the whole message.
- F4.4 Neither has a delimiter → direct `pattern_match`.

### F5 — Rule dispatch (`process_full_message`)
- F5.1 On **every** fully-reassembled message: first run `disable_command()`
  (fires the previous command's `on_disable` if non-NULL), then scan
  `command_map`, then scan `layer_map`.
- F5.2 **First-match-wins** in each map (scan order = definition order).
- F5.3 Always `deactivate_layer()` (the currently-active notifier layer) before
  activating a new one → **exactly one notifier layer is active at a time**.
- F5.4 An unmatched/empty message deactivates the layer and disables the command
  (neutral keymap). This is how an empty workspace clears state.
- F5.5 Commands and layers are **independent** — one window can match both a
  command (toggle vim) and a layer (switch keymap) simultaneously.

### F6 — Acknowledgement
- F6.1 After processing, always call `raw_hid_send(response, RAW_REPORT_SIZE)`
  with `response[0] = 1` if any command or layer matched, else `0`. The host
  receives this 32-byte reply (see §4.4); it is not dropped.

### F7 — Weak-symbol defaults
- F7.1 If the keymap defines neither `DEFINE_SERIAL_COMMANDS` nor
  `DEFINE_SERIAL_LAYERS`, the module compiles and runs against **empty maps**
  (weak default accessors returning size 0). The module must never fail to link
  because a user omitted the macros.

### F8 — Multi-OS map selection (opt-in overlay)
- F8.1 The module tracks `static os_variant_t current_os`, initialized to
  `OS_UNSURE` (0). `os_variant_t` is QMK's enum
  (`OS_UNSURE/0, OS_LINUX/1, OS_WINDOWS/2, OS_MACOS/3, OS_IOS/4`), obtained
  header-only via `#include "os_detection.h"` (§5.1). It is **reused as-is**, not
  redefined.
- F8.2 `void notifier_set_os(os_variant_t os)` is the **only** way `current_os`
  changes. It is called by the keymap, conventionally from
  `process_detected_host_os_kb`/`_user` (§10.1). The module **never** calls
  `detected_host_os()` itself — so it has no link dependency on the OS-detection
  `.c` (only the header-only `os_variant_t` type is consumed).
- F8.3 A keymap may define, **per OS and per map type**, OS-specific maps via
  `DEFINE_SERIAL_COMMANDS_OS(os, { … })` and `DEFINE_SERIAL_LAYERS_OS(os, { … })`
  (§5.5). Each is optional; any subset of {LINUX, WINDOWS, MACOS, IOS} ×
  {commands, layers} may be defined. `OS_UNSURE` has no OS-specific map by design.
- F8.4 **Merge/fallback dispatch — per map type, independent (the core rule):**
  for each of {command_map, layer_map}, `process_full_message` first scans the
  OS-specific map for `current_os` (if one is defined for it). **A match there
  wins and the default map for that type is not consulted.** If no OS-specific
  map is defined for `current_os`, or one is defined but produces no match, the
  **default** map is scanned. First-match-wins within whichever map(s) are
  consulted. (Implementation: §8.6.)
- F8.5 The command track and the layer track each make this OS-vs-default
  decision **independently** — a layer may resolve from the OS map while a
  command resolves from the default map, or vice versa. (Preserves F5.5: a
  match on an OS layer never invalidates a default-command match, and
  vice versa.)
- F8.6 `OS_UNSURE` (the boot state, and what detection returns when unsure) maps
  to **no** OS-specific map ⇒ both tracks use their default maps ⇒ today's exact
  behavior during the ~250 ms pre-detection window and on any board that does
  not enable `OS_DETECTION_ENABLE`.
- F8.7 Weak-default guarantees (F7) extend to per-OS accessors (§8.3): a keymap
  that defines no `DEFINE_*_OS` macros links and behaves identically to one that
  defines none at all; a keymap that defines OS maps but no default links and
  simply matches nothing for OSes/tracks without a map.

### F9 — OS-change state clearing
- F9.1 When `notifier_set_os(os)` is called with a value **different** from
  `current_os`, the module clears all notifier state before recording the new OS:
  `disable_command()` (fires the previous command's `on_disable` if any), then
  `deactivate_layer()` (turns off the active notifier layer if any). This
  guarantees no layer/command chosen under the *previous* OS's maps persists
  into the new OS.
- F9.2 After clearing, the module does **not** re-dispatch the last received
  message. The next focus-change message from the host re-establishes state
  under the new maps (QMKonnect sends on every focus change, so the gap is one
  window switch).
- F9.3 If `os == current_os`, `notifier_set_os` is a no-op (idempotent), so
  repeated stable-detection callbacks (e.g. macOS-on-ARM's delayed stability)
  do not flap state.
- F9.4 Flappy KVM / USB-switch environments where OS detection never stabilizes
  are mitigated by the keymap author setting QMK's `OS_DETECTION_SINGLE_REPORT`
  and/or `OS_DETECTION_KEYBOARD_RESET` in `config.h`/`rules.mk`. The module
  places no requirement on either.

---

## 3. Repository Layout & Deliverables

The repo root contains exactly these source files plus tests, docs, and the test
runner. A from-scratch rebuild must produce:

```
qmk_notifier/
├── notifier.h                 # public API: structs, macros, OS-selection decls (~80 lines)
├── notifier.c                 # receiver, reassembler, dispatcher, OS select (~410 lines)
├── pattern_match.h            # pattern_match() public decl + doc comment   (~53 lines)
├── pattern_match.c            # the matcher (anchors/escapes/classes/NFA)    (~514 lines)
├── rules.mk                   # single-line QMK integration                  (2 lines)
├── README.md                  # user-facing README                           (provided)
├── run_all_tests.sh           # builds + runs the pattern_match suites       (~181 lines)
├── run_notifier_stub_tests.sh # stub-compiles notifier.c + runs dispatch/OS tests
├── qmk_stubs/                 # minimal QMK header fakes for host-testing notifier.c
│   ├── qmk_keyboard_stub.h    #   QMK_KEYBOARD_H stand-in
│   ├── raw_hid.h              #   raw_hid_send decl stub
│   ├── os_detection.h         #   os_variant_t enum stub (header-only — the module
│   │                          #   uses the TYPE only, never detected_host_os())
│   └── qmk_stubs.c            #   layer_on/off + raw_hid_send implementations
├── test_notifier_dispatch.c   # host dispatch/reassembly/F4/ack tests        (links stubs)
├── test_notifier_os.c         # host multi-OS selection / fallback / clear tests
└── test_*.c                   # 9 pattern_match host suites (gcc)            (the spec)
```

> **Two compilation contexts (both host-testable).** `notifier.c` and
> `pattern_match.c` are compiled **by the QMK build system** (they `#include
> QMK_KEYBOARD_H` etc.). For host testing, `pattern_match.c` is linked directly
> by the 9 `test_*pattern*.c` suites. `notifier.c` — which depends on QMK symbols
> (`layer_on`/`layer_off`, `raw_hid_send`) — is **also** host-tested via the
> `qmk_stubs/` harness: `run_notifier_stub_tests.sh` compiles `notifier.c` with
> `-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs`, where the stubs
> provide minimal definitions of those QMK symbols plus a header-only
> `os_detection.h` containing the `os_variant_t` enum. (`notifier.c` only *uses
> the type* `os_variant_t`; it never calls `detected_host_os()`, so the stub
> needs no OS-detection implementation.) This exercises the receiver,
> reassembler, F4 delimiter matcher, dispatcher ordering, the ack path, **and
> the multi-OS selection logic** on a host with plain gcc.
> `test_comprehensive_integration.c` is built with `-DNOTIFIER_STUB`; this macro
> is currently vestigial (not referenced in source) and may be passed harmlessly.

---

## 4. The Wire Protocol Contract (most important section)

> Get any byte here wrong and the desktop app and the firmware will not talk.
> This is the exact contract with `qmk-notifier` (the Rust crate) and `QMKonnect`.

### 4.1 The logical message

```
<application_class>\x1D<window_title>
```

- `application_class` — the stable app identifier (e.g. `firefox`, `code`,
  `neovide`, `Chrome_WidgetWin_1`). What the user matches in firmware.
- `\x1D` — ASCII **Group Separator** (decimal 29). The delimiter. Macro name:
  `GS_DELIMITER` (the header comment reads `// ASCII 29 (Group Separator)`;
  `0x1F` is the separate "Unit Separator" codepoint and is not used here).
- `window_title` — the window's title, may be empty.

Examples produced by the desktop:
- VS Code → `code\x1Dmain.rs - qmkonnect`
- Firefox → `firefox\x1DGitHub - Mozilla Firefox`
- Empty Hyprland workspace → `\x1D` (both empty)
- macOS without Screen Recording → `Safari\x1D` (app name, empty title)

> The desktop builds the message **without** a terminator. The transport crate
> (`qmk-notifier`) appends the **ETX** (`0x03`) terminator *before* framing.
> The firmware never sees a terminator in the user payload until the crate adds it.

### 4.2 Report framing on the wire

QMK Raw HID (`RAW_ENABLE = yes`) exposes a vendor-defined interface with **usage
page `0xFF60`**, **usage `0x61`** (QMK defaults `RAW_USAGE_PAGE` / `RAW_USAGE_ID`).
Each logical HID report is **32 bytes**.

The transport crate sends each 32-byte report to the device as a **33-byte**
buffer to `hidapi::HidDevice::write` (hidapi demands a leading report-ID byte;
the interface has no report ID so it's `0x00`):

```
byte[0]      = 0x00     (report-ID leading byte for hidapi write)
byte[1]      = 0x81     (magic header byte 1)
byte[2]      = 0x9F     (magic header byte 2)
byte[3..33]  = up to 30 payload bytes (zero-filled on the final report)
```

**30 payload bytes per report** (`PAYLOAD_PER_REPORT = 32 - 2`). A payload longer
than 30 bytes is split into `ceil(len/30)` back-to-back reports. The end of the
logical message is signaled by an **ETX terminator** (`0x03`) appended to the
payload **before** framing. Burst-write (all reports back-to-back, no per-report
ACK) is safe because QMK's raw-HID OUT endpoint buffers up to 4 reports and NAKs
when full — reports are never dropped.

### 4.3 What `raw_hid_receive` on the firmware sees

QMK delivers **32 bytes** per report to `raw_hid_receive(data, length)` where
`length == 32`. (On V-USB low-speed AVR boards the USB packet is 8 bytes, but QMK
reassembles it into the 32-byte logical report before calling the hook — so the
firmware always sees `length == 32`. Do not handle `length == 8`.)

```
data[0]  = 0x81      ← qmk_notifier magic byte 1
data[1]  = 0x9F      ← qmk_notifier magic byte 2
data[2..]= <payload bytes for this report>
```

### 4.4 The acknowledgement

After each report the firmware sends a 32-byte response with `response[0] =
(matched ? 1 : 0)` via `raw_hid_send(response, RAW_REPORT_SIZE)`. The host
**receives** it: the reply is a full 32-byte logical report, which satisfies the
`length == 32` guard on every QMK USB protocol (ChibiOS/LUFA endpoint = 32; V-USB
reassembles 32 internally). The firmware MUST always send it. See `notifier.c` §8.

> **Historical note (resolved).** An earlier build reused the header-stripped
> `length` — i.e. `30`, after `data += 2; length -= 2;` — in the `raw_hid_send`
> call, producing a 30-byte reply that the `length == 32` guard rejected, so the
> host timed out and layer/callback reaction lagged. Fixed in commit `01a51935`
> ("fixed raw hid response size"), which sends the constant `RAW_REPORT_SIZE`
> (32). The "ack is silently dropped by QMK because `length == RAW_EPSIZE`"
> wording that appeared in older revisions of this spec, the desktop
> `PROTOCOL.md`, and the `qmk-notifier` crate comments is stale carryover from
> that pre-fix state and has been corrected in all three.

### 4.5 Why the magic header exists

Multiple Raw HID modules may share the one interface (e.g. `qmk_notifier` +
`qmk-field-kit` + VIA). The reference keymap wires:

```c
void raw_hid_receive(uint8_t *data, uint8_t length) {
    field_kit_process_message(data, length);   // other module
    hid_notify(data, length);                  // this module
}
```

`hid_notify` returns immediately if `data[0..1] != 0x81 0x9F`, so each module
only consumes its own messages. `0x81 0x9F` is arbitrary but fixed forever.

### 4.6 Typed-command namespace (canonical owner)

This section is the **canonical wire contract** for the typed-command namespace.
The desktop `PROTOCOL.md` and the `qmk-notifier` crate `PRD.md` mirror it; where
they disagree, **this section wins** (the firmware dictates the conditions under
which communication can happen — see `qmkonnect/spec/HOST_RULES.md` for the
host-side orchestration).

**Discriminator.** The byte after the magic header selects the path:
- `data[2] == 0xF0` ⇒ **typed command**. Routed to `handle_typed_command()`
  *before* any string processing, so it has **no `process_full_message` side
  effects** (no disable/deactivate).
- anything else ⇒ **legacy string** (unchanged): bytes are string chars until
  `0x03` (ETX). Because the sanitizer allows only `0x20–0x7E` (+ a few controls),
  `0xF0` can never begin a real matched string, so a host that sends only legacy
  strings coexists unchanged.

**Framing.** Typed commands are **ETX-framed and may span multiple 32-byte
reports**, exactly like legacy strings:
`[0x81][0x9F][0xF0][cmd_id][ args… ][0x03]`, chunked at 30 payload bytes/report.
A single-report command simply carries its args then `0x03`. Multi-report framing
removes any fixed cap on argument size (notably the callback-id list in
`APPLY_HOST_CONTEXT`).

**Responses** (32-byte reports, sent on every typed command):
- Legacy string response (unchanged): `[matched(0|1)][padding…]`.
- Typed response: `[0x51][cmd_id_echo][payload…][padding]`. The `0x51` marker
  (≥2) is distinct from the legacy `0`/`1`, so the host disambiguates without
  ambiguity. No reply within the host's timeout ⇒ `Timeout` (host treats a
  non-capable / offline device as string-only).

**Command table:**

| `cmd_id` | Name | Request args | Response payload (after `[0x51][cmd_echo]`) |
| --- | --- | --- | --- |
| `0x01` | `QUERY_INFO` | none | `[proto_ver][feature_flags][callback_count][board_rules_present]` |
| `0x02` | `QUERY_CALLBACK` | `[index]` | `[index][name bytes, NUL-padded]` (name absent ⇒ `[index][0x00]`) |
| `0x03` | `SET_OS` | `[os_byte]` | `[ack]` (`1`=applied) |
| `0x04` | *(reserved — VIA-coexist dispatch)* | — | — |
| `0x05` | `APPLY_HOST_CONTEXT` | `[layer][flags][count][id0][id1]…` | `[ack]` (`1`=applied) |

**Field definitions:**
- `proto_ver`: protocol version. `1` = a legacy string-only firmware (no typed
  commands). `2` = a firmware that implements this namespace. **Owned by the
  firmware.**
- `feature_flags` bitmask: `0x01` = `APPLY_HOST_CONTEXT` supported; `0x02` =
  callback registry present (`DEFINE_HOST_CALLBACKS`); `0x04` = *(reserved)*
  VIA-coexist dispatch.
- `callback_count`: number of entries in the firmware's host-callback registry
  (`0` if none / not defined).
- `board_rules_present`: `1` iff **any** board map (default or any
  `DEFINE_SERIAL_*_OS`) is non-empty — i.e. the keymap uses the notifier matcher
  at all. A single bit suffices; per-OS granularity is not exposed (the host does
  not need it).
- `SET_OS.os_byte`: `0 UNSURE · 1 LINUX · 2 WINDOWS · 3 MACOS · 4 IOS` (mirrors
  QMK's `os_variant_t`). See §4.7.
- `APPLY_HOST_CONTEXT.layer`: the host's desired host-layer number, or `0xFF`
  (255 = `LAYER_UNSET`) to clear the host layer. **Host layers are reserved ≥
  224** so they resolve above board layers under QMK's highest-layer-wins rule.
- `APPLY_HOST_CONTEXT.flags` bit 0 = **`clear_board`**: when set, the firmware
  `deactivate_layer()`s its board `activated_layer` and `disable_command()`s its
  current board command **before** applying the host context. This is the
  mechanism by which the host selects per-window "replace" (clear board) vs
  "stack" (leave board) semantics — see §14.
- `APPLY_HOST_CONTEXT.id…`: the **full desired enabled** host-callback id set;
  the firmware diffs against its current enabled set and calls `on_enable`/
  `on_disable` accordingly (disable-before-enable).

**Capability handshake & legacy fallback.** On device (re)connect the host sends
`QUERY_INFO`:
1. A firmware that implements this namespace replies
   `[0x51][0x01][proto=2][flags][count][…]`.
2. A legacy (string-only) firmware walks the typed bytes as a no-match string,
   replies `[0x00…]` (or times out). The host treats `response[0] != 0x51` (or
   timeout) as **legacy ⇒ string-only mode**: it keeps sending the legacy string
   (board rules still work) and **never sends typed commands**.
3. If capable and `flags & 0x01`: for `i in 0..count` send `QUERY_CALLBACK(i)`
   and build the `name→id` map; validate the host `rules.toml` callback names
   against it (warn + skip unknown).

**Handshake timing & `has_been_queried` (correctness).** `QUERY_INFO`/typed
commands bypass `process_full_message`, so they have **no board side effects** on
a firmware that implements this namespace. Against a *legacy* firmware,
`QUERY_INFO` is walked as a no-match string and `process_full_message` *always*
calls `disable_command()`/`deactivate_layer()` first — harmless only when board
state is fresh. The firmware therefore sets a **`has_been_queried`** bool on the
first `QUERY_INFO` it services, and the host handshakes **at most once per board
boot** (never on a mere HID re-enumeration/reconnect), so a mid-session reconnect
against legacy firmware cannot clear an active board layer. Host rules are
additionally gated on `proto_ver == 2`, so legacy firmware never receives typed
commands in practice.

### 4.7 OS source: host-authoritative when a host is connected; firmware heuristic otherwise

**Firmware-side (heuristic).** The OS used for multi-OS map selection (§2 F8) is
*determined firmware-side* by QMK's `OS_DETECTION` feature and **pushed** into the
module by the keymap via `notifier_set_os(os_variant_t)` (§5.2, §8.7).
`OS_DETECTION` is a heuristic USB fingerprint and can misdetect (Linux boxes
spoofing macOS for media-key compat, VMs, `OS_UNSURE`). When no host is
connected, this is the only OS signal.

**Host-authoritative (when connected).** The host (QMKonnect / `qmk-notifier`
crate) knows its own OS **with certainty** and it never changes at runtime (it is
the OS the desktop is running on). The host declares its OS at connect via the
`SET_OS` typed command (`0x03`, §4.6): `[0x81][0x9F][0xF0][0x03][os_byte][0x03]`
→ `[0x51][0x03][ack]`. **While a host is connected and has sent `SET_OS`, the
host's value is authoritative for `current_os`** — it takes precedence over the
`OS_DETECTION` heuristic. (The host sends `SET_OS` once at connect; the firmware's
`OS_DETECTION`-driven maps are the fallback only when no host is connected.)
`SET_OS` is sent only to a typed-command-capable firmware (gated by the §4.6
handshake); legacy firmware never sees it.

`SET_OS` updates `current_os` through the same seam as `notifier_set_os`, so an
OS **change** clears notifier state per F9 (disable command + deactivate layer)
before recording the new OS.

**Orthogonality (load-bearing):** multi-OS map selection touches **only** which
board `command_map`/`layer_map` the legacy *string* path consults. The
typed-command namespace (§4.6) and the `host_layer`/host-callback trackers (§14)
are **independent** state — they do not consume each other's mechanisms.

## 5. File Specification: `notifier.h`

The **public** header consumed by the user's `keymap.c`. It defines two structs,
the default-map + OS-map macros (`DEFINE_SERIAL_*` and `DEFINE_SERIAL_*_OS`),
two constants, the default-map accessor declarations, the OS-selector
(`notifier_set_os`), and the entry-point declaration. (Per-OS accessor symbols
are an internal linkage contract — §5.5/§8.3 — and are intentionally not
declared in this header.)

### 5.1 Type aliases & structs

```c
#pragma once
#include <stdbool.h>
#include "os_detection.h"   /* os_variant_t — header-only, always in the QMK tree.
                             * No link cost: the module uses the TYPE only and
                             * never calls detected_host_os() (the OS is pushed
                             * in by the keymap via notifier_set_os). §2 F8.2. */

/* A user callback is a nullary C function. */
typedef void (*callback_t)(void);

/* One entry in the command map: pattern → on_enable/on_disable callbacks. */
typedef struct {
    const char   *pattern;
    callback_t    on_enable;
    callback_t    on_disable;      /* may be NULL */
    const bool    case_sensitive;
} command_map_t;

/* One entry in the layer map: pattern → QMK layer index. */
typedef struct {
    const char   *pattern;
    const int     layer;
    const bool    case_sensitive;
} layer_map_t;
```

### 5.2 Map accessors (user overrides via macros; module provides weak defaults)

```c
/* Default maps (user overrides via DEFINE_SERIAL_*; module provides weak defaults). */
command_map_t* get_command_map(void);
size_t         get_command_map_size(void);
layer_map_t*   get_layer_map(void);
size_t         get_layer_map_size(void);

/* The OS selector — pushed from the keymap (§8.7). os_variant_t comes from
 * os_detection.h (included above). This is the only public OS entry point. */
void notifier_set_os(os_variant_t os);
```

### 5.3 Constants & helper macros

```c
#define GS_DELIMITER      "\x1D"   /* ASCII 29 — Group Separator, class|title delim */
#define ETX_TERMINATOR    "\x03"   /* ASCII 3  — End of Text, message terminator    */

/* Build a two-part pattern: classname + GS + title.
   Expands to the literal `classname "\x1D" title`. */
#define WINDOW_TITLE(classname, title)  classname GS_DELIMITER title
#define WT(...) WINDOW_TITLE(__VA_ARGS__)
```

`WT` is the short alias and is what keymaps actually use (e.g.
`WT("firefox", "*youtube*")`). It accepts variadic args purely so callers may
write `WT(class, title)`; it always expands to the 2-argument `WINDOW_TITLE`.

### 5.4 Map-definition macros

These macros, used at file scope in the keymap, define the array **and** the
matching accessor pair — overriding the weak defaults in `notifier.c`.

```c
#define DEFINE_SERIAL_COMMANDS(...) \
    command_map_t user_command_map[] = __VA_ARGS__; \
    const size_t user_command_map_size = sizeof(user_command_map) / sizeof(user_command_map[0]); \
    command_map_t* get_command_map(void) { return user_command_map; } \
    size_t get_command_map_size(void) { return user_command_map_size; }

#define DEFINE_SERIAL_LAYERS(...) \
    layer_map_t user_layer_map[] = __VA_ARGS__; \
    const size_t user_layer_map_size = sizeof(user_layer_map) / sizeof(user_layer_map[0]); \
    layer_map_t* get_layer_map(void) { return user_layer_map; } \
    size_t get_layer_map_size(void) { return user_layer_map_size; }
```

> Because C aggregate initialization zero-fills omitted trailing struct fields,
  a `DEFINE_SERIAL_COMMANDS` row may omit the `case_sensitive` 4th field (it
  becomes `false`) and a `DEFINE_SERIAL_LAYERS` row may omit its 3rd field. The
  reference keymap relies on this.

### 5.5 OS-specific map-definition macros

`DEFINE_SERIAL_COMMANDS_OS(os, { … })` and `DEFINE_SERIAL_LAYERS_OS(os, { … })`
define a per-OS command/layer map **and** the matching accessor pair, overriding
the per-OS weak defaults in `notifier.c` (§8.3). `os` is an `os_variant_t`
enumerator constant — `OS_LINUX`, `OS_WINDOWS`, `OS_MACOS`, or `OS_IOS`. (Do not
pass `OS_UNSURE`; it has no OS-specific map by design — §2 F8.6.)

```c
#define DEFINE_SERIAL_COMMANDS_OS(os, ...) \
    command_map_t _notifier_command_map_##os[] = __VA_ARGS__; \
    const size_t  _notifier_command_map_##os##_size = \
        sizeof(_notifier_command_map_##os) / sizeof(_notifier_command_map_##os[0]); \
    command_map_t* _notifier_get_command_map_##os(void) { \
        return _notifier_command_map_##os; \
    } \
    size_t _notifier_get_command_map_##os##_size(void) { \
        return _notifier_command_map_##os##_size; \
    }

#define DEFINE_SERIAL_LAYERS_OS(os, ...) \
    layer_map_t _notifier_layer_map_##os[] = __VA_ARGS__; \
    const size_t _notifier_layer_map_##os##_size = \
        sizeof(_notifier_layer_map_##os) / sizeof(_notifier_layer_map_##os[0]); \
    layer_map_t* _notifier_get_layer_map_##os(void) { \
        return _notifier_layer_map_##os; \
    } \
    size_t _notifier_get_layer_map_##os##_size(void) { \
        return _notifier_layer_map_##os##_size; \
    }
```

> **How the token-paste works.** `##os` concatenates the *preprocessor token*
> passed as `os`. Because the caller writes the enumerator name (`OS_MACOS`),
> the generated symbols are e.g. `_notifier_command_map_OS_MACOS`,
> `_notifier_get_command_map_OS_MACOS`, `_notifier_get_command_map_OS_MACOS_size`.
> These exact names are what `notifier.c`'s `select_*_map_os()` switch
> references (§8.3), and what its weak defaults provide for every OS. **The
> keymap never calls these directly.**

> **Row struct parity.** A `DEFINE_SERIAL_COMMANDS_OS` row has the same shape as
> `DEFINE_SERIAL_COMMANDS` (`{ pattern, on_enable, on_disable, case_sensitive? }`);
> a `DEFINE_SERIAL_LAYERS_OS` row has the same shape as `DEFINE_SERIAL_LAYERS`
> (`{ pattern, layer, case_sensitive? }`). Omitted trailing fields zero-fill as
> before (`case_sensitive` → `false`).

> **Selection rule (restated at the API surface).** At dispatch time
> (`process_full_message`, §8.6), for each map type the OS-specific map for
> `current_os` is scanned **first**; a match wins and the default map is **not**
> consulted. If no OS-specific map exists for `current_os`, or it exists but
> matches nothing, the default map is scanned. The two map types decide this
> independently. `current_os == OS_UNSURE` ⇒ no OS-specific map ⇒ default only.

### 5.6 Entry points (implemented by QMK / this module)

```c
/* Provided by the user's keymap (QMK's hook). */
void raw_hid_receive(uint8_t* data, uint8_t length);
/* Provided by this module. The keymap's raw_hid_receive delegates to it. */
void hid_notify(uint8_t* data, uint8_t length);
```

---

## 6. File Specification: `pattern_match.h`

Public header for the matcher. One function, exhaustively documented in the
doc comment (reproduce the doc comment verbatim in a rebuild).

```c
#pragma once
#include <stdbool.h>

bool pattern_match(const char *pattern, const char *str, bool case_sensitive);
```

**Contract (from the doc comment):**
- Wildcard `*` matches any sequence (including empty).
- `^` at pattern start → anchor to beginning. `$` at pattern end → anchor to end.
  `^...$` together → exact full-string match.
- Escapes `\^ \$ \* \\` match the literal character.
- `@note` Returns `false` if either argument is `NULL`.
- Thread-safe (no global state). Memory managed internally; caller frees nothing.
- **No anchors ⇒ substring match** (this is the backward-compatible default).

---

## 7. File Specification: `pattern_match.c` — the matcher

This is the most algorithmically intricate file. It is a **Thompson-NFA** regex
engine over a small, fixed token set. The public pipeline is:

```
pattern_match(pattern, str, case_sensitive)        [public API]
 ├─ parse_pattern(pattern)                          [strip ^/$ anchors, record flags]
 │     └─ process_escapes(core)  → "processed pattern" byte string
 ├─ match_with_anchors(&parsed, str, case)         [choose strategy, loop offsets]
 │     ├─ match_string_with_start(...)      == nfa_match(..., full_match=false)
 │     └─ match_reaches_end_with_start(...) == nfa_match(..., full_match=true)
 └─ free_parsed_pattern(...)
```

`match_with_anchors` loops over input start offsets for the **unanchored
(substring)** and **end-anchored** cases, calling the NFA at each offset.
`string_start` is always the **original** string base (so `\b`/`\B` can compute
absolute positions). The NFA replaces only the leaf matcher; everything above
stays.

### 7.1 The processed-pattern byte contract (what the NFA consumes)

`process_escapes()` emits a NUL-terminated byte string. **Every byte is one of:**

| Byte(s) | Source | Meaning | Width |
|---|---|---|---|
| `0x2A` `*` | bare `*` | **glob wildcard** — any sequence incl. `\n`/`\r` | variable |
| `0x0E` | `+` after a consuming element | **`+` quantifier marker** (`X+` = ≥1 of X) | marker |
| `0x01`–`0x04` | `\^` `\$` `\*` `\\` | escaped literals | 1 consuming |
| `0x05`–`0x0A` | `\d` `\D` `\w` `\W` `\s` `\S` | character classes | 1 consuming |
| `0x0B` `0x0C` | `\b` `\B` | word-boundary / non-boundary assertions | 0 (zero-width) |
| `0x0D` | bare `.` | dot metacharacter — any char **except `\n`/`\r`** | 1 consuming |
| `0x2E` `.` | `\.` | literal dot | 1 consuming |
| `0x2B` `+` | `\+`, or a bare `+` not following a consumable element | literal plus | 1 consuming |
| any other | ordinary char | literal byte | 1 consuming |
| `0x00` | end | NUL terminator | — |

`get_escaped_char(placeholder)` reverses `0x01`–`0x0D` back to the readable char
(for debugging; the matcher uses `pattern_char_matches`, not this, for matching).

### 7.2 Escape & metacharacter processing rules (`process_escapes`)

Maintains a `bool last_consumable` flag while walking the source pattern:

- `\X` (backslash + next char):
  - `\^ \$ \* \\` → placeholder `0x01`–`0x04`; `last_consumable = true`.
  - `\. \+` → literal `.`/`+` (ordinary bytes); `last_consumable = true`.
  - `\d \D \w \W \s \S` → placeholder `0x05`–`0x0A`; `last_consumable = true`.
  - `\b \B` → placeholder `0x0B`/`0x0C`; **`last_consumable = false`** (zero-width).
  - **Unrecognized** (`\x`, `\z`, …) → emit `\\` + the char **literally** (so
    `\x` matches the two-byte string `\x`); `last_consumable = true`.
- A trailing lone `\` (backslash then NUL) → emit `\` literally.
- Bare `*` → emit `0x2A`; `last_consumable = false` (wildcards don't consume
  in the "previous element" sense).
- Bare `+` → if `last_consumable` is true, emit `0x0E` (quantifier) and set
  `last_consumable = false`; **else** emit literal `+` and set `last_consumable = true`.
- Bare `.` → emit `0x0D`; `last_consumable = true`.
- Anything else → emit literally; `last_consumable = true`.

### 7.3 Anchor detection (`parse_pattern`)

- If the pattern starts with `^` → `start_anchored = true`; skip it.
- If the pattern ends with `$` **and the `$` is not escaped** (an even number —
  including zero — of backslashes precede it) → `end_anchored = true`; drop it.
- The substring between the anchors is the **core pattern**, fed to
  `process_escapes`.

### 7.4 Matching strategy (`match_with_anchors`)

| Anchors | Strategy |
|---|---|
| `^` + `$` (both) | **Exact**: `nfa_match(core, str, full=true)` — accept only if MATCH reachable after consuming the whole string. |
| `^` only | **Prefix**: `nfa_match(core, str, str, full=false)` — MATCH reachable at any point from position 0. |
| `$` only | **Suffix**: try `nfa_match(core, str+i, str, full=true)` for `i = 0..len`; succeed on first hit. |
| neither | **Substring** (backward-compat default). Special case: an empty core matches **only** the empty string. Otherwise try `nfa_match(core, str+i, str, full=false)` for `i = 0..len`. |

### 7.5 The NFA engine

Compile the processed pattern once into a stack-allocated pool of `State`s
(Thompson construction), then simulate with two state-lists and a generation
tag. **No backtracking → guaranteed O(states × strlen).**

```c
#define NFA_MAX_PATTERN 128
#define NFA_MAX_STATES  (2 * NFA_MAX_PATTERN + 2)

enum { OP_CHAR, OP_ANY, OP_SPLIT, OP_ASSERT, OP_MATCH };

struct State {
    int    op;
    char   arg;       /* OP_CHAR: pattern byte; OP_ASSERT: 0x0B or 0x0C */
    State *out;
    State *out1;
    int    lastlist;  /* generation tag, set during simulation */
};
```

**Compile (`nfa_compile`)** walks the processed pattern, threading a `tail`
pointer that each unit writes its start into:

- `0x2A` (glob `*`) → `OP_ANY` looping back through an `OP_SPLIT`
  (matches `.*` semantics: any sequence, including empty, including newline).
- `0x0B`/`0x0C` (`\b`/`\B`) → `OP_ASSERT` (zero-width).
- A consuming element `X`:
  - If followed by `0x0E` (`X+`) → `OP_CHAR(X)` then `OP_SPLIT` (loop back to X
    / exit) → compiles `a+` as linear (no catastrophic blow-up for `a+a+a+…`).
  - Else → `OP_CHAR(X)`.
- A stray `0x0E` (should not occur) → skipped defensively.
- End → append an `OP_MATCH` accepting state.
- Zero `lastlist` on every allocated state (the pool is fresh each call).

**Simulate (`nfa_match`, signature identical to the old backtracking core):**

```c
static bool nfa_match(const char *pattern, const char *str,
                      const char *string_start, bool case_sensitive, bool full_match);
```

- `abspos = str - string_start` (absolute offset into the **original** string —
  needed for `\b`/`\B`).
- Seed the current list (`clist`) with the epsilon-closure of the start state at
  `abspos`. If `!full_match` and MATCH is already reachable → return true
  (matches empty prefix).
- For each input char `c` at position `pos`: build the next list (`nlist`) by
  advancing every `OP_ANY` state (consume any non-NUL char, **including**
  newline) and every `OP_CHAR` state whose `pattern_char_matches(arg, c, …)`
  holds; bump `nfa_gen` so closure de-dup works. Swap lists. If `!full_match`
  and MATCH reachable → return true. If the list is empty → break (dead).
- Return `nfa_has_match(clist)` (for `full=true`, accept only at end).

**Epsilon-closure (`nfa_addstate`)** — guarded by `lastlist == nfa_gen`:
- `OP_MATCH` → added to list.
- `OP_SPLIT` → recurse into both `out` and `out1` (same `abspos`).
- `OP_ASSERT` → recurse into `out` **only if** `is_word_boundary(string_start,
  abspos) == want_boundary` (zero-width; `want_boundary` is true for `0x0B`).
  *Empty original string*: neither boundary nor non-boundary (legacy semantics).
- `OP_CHAR`/`OP_ANY` → added to list, waiting to consume a char.

### 7.6 Character classification helpers (static)

```c
static bool is_digit_char(char c);        /* '0'..'9' */
static bool is_word_char(char c);         /* [A-Za-z0-9_] */
static bool is_whitespace_char(char c);   /* ' ' \t \n \r \f \v */
static bool is_word_boundary(const char *str, size_t pos);  /* see below */
```

`is_word_boundary(str, pos)`:
- `pos == 0` → true iff `str[0]` is a word char.
- `pos == strlen` → true iff `str[len-1]` is a word char.
- `pos > strlen` → false.
- interior → true iff `is_word_char(str[pos-1]) != is_word_char(str[pos])`.
- `str == NULL` → false.

### 7.7 The single-char predicate (`pattern_char_matches`)

```c
static bool pattern_char_matches(char pc, char sc, bool case_sensitive);
```

- `pc` in `0x01`–`0x04` → literal (decoded via `get_escaped_char`), case-folded
  via `tolower` unless `case_sensitive`.
- `0x05` `0x06` → `is_digit_char(sc)` / its negation (`\d` / `\D`).
- `0x07` `0x08` → `is_word_char(sc)` / its negation (`\w` / `\W`).
- `0x09` `0x0A` → `is_whitespace_char(sc)` / its negation (`\s` / `\S`).
- `0x0D` → `sc != '\n' && sc != '\r'` (the dot).
- default → ordinary literal, case-folded unless `case_sensitive`.

### 7.8 Why an NFA (not backtracking)

The previous engine backtracked and went **exponential** on patterns like
`a+a+a+a+a+a+a+a+a+a+b` against a long run of `a`. A `MATCH_STEP_BUDGET` cap
prevented hangs but returned wrong "no match" results. The Thompson NFA
compiles-once and simulates in **O(states × input_len), always**. Reference:
Russ Cox, *"Regular Expression Matching Can Be Simple And Fast"*
(https://swtch.com/~rsc/regexp/regexp1.html). The acceptance test (§11.2B)
requires that pathological case to finish in **< 50 ms**.

### 7.9 Sizing note (MCU RAM)

`State pool[NFA_MAX_STATES]` + two pointer lists live on the stack. With
`NFA_MAX_PATTERN = 128` that is ~256 `State`s (~6–8 KB) + ~2 KB of lists per
call. Fine on desktop and RP2040. **For low-RAM AVR, lower `NFA_MAX_PATTERN`
(e.g. 48).** Patterns are short (user keymap rules), so this is ample. The arrays
must stay on the stack (not `static`) if reentrancy is ever needed; in QMK the
matcher is single-threaded.

---

## 8. File Specification: `notifier.c` — the receiver/dispatcher

This is the QMK-side file. It `#include`s (in order) `QMK_KEYBOARD_H`,
`pattern_match.c` (note: the **.c**, directly — so the matcher compiles into the
same translation unit), `notifier.h`, `raw_hid.h`, `<string.h>`, and (if
`CONSOLE_ENABLE`) `print.h`.

> **Include quirk to preserve:** `notifier.c` does `#include "pattern_match.c"`,
> not `pattern_match.h`. The host test programs instead compile `pattern_match.c`
> as a separate translation unit. Both work because the matcher has no
> file-scope mutable global state (only a monotonic `nfa_gen` int).

### 8.1 Constants & globals

```c
#define RAW_REPORT_SIZE 32          /* logical HID report size, ALL QMK protocols */
#define MSG_BUFFER_SIZE 256         /* reassembly buffer */
#define LAYER_UNSET     255

static char    msg_buffer[MSG_BUFFER_SIZE];
static uint16_t msg_index = 0;       /* persists across hid_notify calls */

uint8_t activated_layer = LAYER_UNSET;
command_map_t *current_command = {0};   /* effectively NULL */

/* The host OS used for multi-OS map selection (§2 F8). Pushed in by the keymap
 * via notifier_set_os(); never read from detected_host_os() directly (no link
 * dependency on the OS-detection subsystem). OS_UNSURE ⇒ default maps only. */
os_variant_t current_os = OS_UNSURE;
```

> The comment on `RAW_REPORT_SIZE` is load-bearing: 32 is the **logical** report
> on every QMK USB protocol and is **not** `RAW_EPSIZE` (the USB packet size).
> V-USB (low-speed AVR) has an 8-byte endpoint but reassembles a 32-byte logical
> report; the host's `raw_hid_send` guard requires `length == 32`. **32 is the
> single value that works on every board.**

### 8.2 `sanitize_string(char *str)` — in-place ASCII filter

Single pass, read/write pointers. Keep byte `b` iff:
`(b >= 32 && b <= 126) || b == 9 || b == 10 || b == 13 || b == 0x1D || b == 0x03`.
NUL-terminate at the write pointer. NULL input → immediate return.

### 8.3 Weak default maps

```c
static command_map_t empty_command_map[1] = {0};
static layer_map_t   empty_layer_map[1]   = {0};

__attribute__((weak)) command_map_t* get_command_map(void)    { return empty_command_map; }
__attribute__((weak)) size_t         get_command_map_size(void){ return 0; }
__attribute__((weak)) layer_map_t*   get_layer_map(void)      { return empty_layer_map; }
__attribute__((weak)) size_t         get_layer_map_size(void)  { return 0; }
```

If the keymap defines `DEFINE_SERIAL_COMMANDS`/`DEFINE_SERIAL_LAYERS`, those
non-weak definitions override these at link time. If not, the module still links
and matches nothing.

**Per-OS weak defaults + selector** (multi-OS, §2 F8). For each OS, the module
provides a weak accessor pair returning `{NULL, 0}` ("no OS-specific map"); a
`DEFINE_SERIAL_*_OS(os, …)` in the keymap overrides the pair for that `os`. The
`select_*_map_os()` helpers dispatch by `current_os` and are what
`process_full_message` calls. `OS_UNSURE` (and any unexpected value) resolves to
`{NULL, 0}` so the default map is used.

```c
/* command map, per OS — weak; overridden by DEFINE_SERIAL_COMMANDS_OS */
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_LINUX(void)   { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_LINUX_size(void)   { return 0; }
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_WINDOWS(void) { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_WINDOWS_size(void) { return 0; }
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_MACOS(void)   { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_MACOS_size(void)   { return 0; }
__attribute__((weak)) command_map_t* _notifier_get_command_map_OS_IOS(void)     { return NULL; }
__attribute__((weak)) size_t         _notifier_get_command_map_OS_IOS_size(void)     { return 0; }

/* layer map, per OS — weak; overridden by DEFINE_SERIAL_LAYERS_OS */
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_LINUX(void)   { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_LINUX_size(void)   { return 0; }
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_WINDOWS(void) { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_WINDOWS_size(void) { return 0; }
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_MACOS(void)   { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_MACOS_size(void)   { return 0; }
__attribute__((weak)) layer_map_t* _notifier_get_layer_map_OS_IOS(void)     { return NULL; }
__attribute__((weak)) size_t       _notifier_get_layer_map_OS_IOS_size(void)     { return 0; }

/* Resolve the OS-specific command/layer map for `os`, or {NULL,0} if none. */
static void select_command_map_os(os_variant_t os, command_map_t **map, size_t *size) {
    switch (os) {
        case OS_LINUX:   *map = _notifier_get_command_map_OS_LINUX();   *size = _notifier_get_command_map_OS_LINUX_size();   return;
        case OS_WINDOWS: *map = _notifier_get_command_map_OS_WINDOWS(); *size = _notifier_get_command_map_OS_WINDOWS_size(); return;
        case OS_MACOS:   *map = _notifier_get_command_map_OS_MACOS();   *size = _notifier_get_command_map_OS_MACOS_size();   return;
        case OS_IOS:     *map = _notifier_get_command_map_OS_IOS();     *size = _notifier_get_command_map_OS_IOS_size();     return;
        default:         *map = NULL; *size = 0; return;   /* OS_UNSURE / unexpected */
    }
}
static void select_layer_map_os(os_variant_t os, layer_map_t **map, size_t *size) {
    switch (os) {
        case OS_LINUX:   *map = _notifier_get_layer_map_OS_LINUX();   *size = _notifier_get_layer_map_OS_LINUX_size();   return;
        case OS_WINDOWS: *map = _notifier_get_layer_map_OS_WINDOWS(); *size = _notifier_get_layer_map_OS_WINDOWS_size(); return;
        case OS_MACOS:   *map = _notifier_get_layer_map_OS_MACOS();   *size = _notifier_get_layer_map_OS_MACOS_size();   return;
        case OS_IOS:     *map = _notifier_get_layer_map_OS_IOS();     *size = _notifier_get_layer_map_OS_IOS_size();     return;
        default:         *map = NULL; *size = 0; return;
    }
}
```

### 8.4 Layer & command state machines

```c
void activate_layer(uint8_t layer);   /* layer_on(layer); activated_layer = layer; */
void deactivate_layer(void);          /* if != LAYER_UNSET: layer_off; activated_layer = LAYER_UNSET */
void enable_command(command_map_t *c);/* current_command = c; c->on_enable(); */
void disable_command(void);           /* if current_command && on_disable: call it; current_command = NULL */
```

`activate`/`deactivate` and `enable`/`disable` wrap QMK's `layer_on`/`layer_off`
and the user's callbacks. Debug `uprintf`s are emitted only under `CONSOLE_ENABLE`.

### 8.5 `match_pattern(pattern, message, case_sensitive)` — delimiter-aware wrapper

This is the **internal** function `process_full_message` calls (it is **not**
the public `pattern_match`). It applies the firmware's two-part semantics:

1. Find the first GS (`0x1D`) in `pattern`.
2. NULL `pattern` or `message` → `false`.
3. **No delimiter in pattern:**
   - If `message` has a delimiter → copy the message's class half (before GS)
     into a 256-byte buffer and `pattern_match(pattern, class_half, …)`.
   - Else → `pattern_match(pattern, message, …)` directly.
4. **Delimiter in pattern:**
   - If `message` has **no** delimiter → copy the pattern's class half and
     `pattern_match(pattern_left, message, …)`.
   - If both have a delimiter → split both (helper `split_by_delimiter`), then
     `pattern_match(pattern_left, msg_left) && pattern_match(pattern_right, msg_right)`.
5. Buffer/length guards (`>= 256`) → `false`.

Helper `find_first_delimiter(str)` returns a pointer to the first GS or NULL.
Helper `split_by_delimiter(source, delimiter_pos, left, left_size, right,
right_size)` copies the left part (length = `delimiter_pos - source`) and the
right part (everything after the delimiter), NUL-terminating both; returns
`false` on truncation.

### 8.6 `process_full_message(char *data)` — the dispatcher

```
1. length = strlen(data); if length >= 256 return false.
2. memcpy into local received_command[256]; NUL-terminate.
   resolve maps for current_os:
     (os_cmd_map,  os_cmd_size)   = select_command_map_os(current_os)   {NULL,0} if none / OS_UNSURE
     (os_layer_map,os_layer_size) = select_layer_map_os(current_os)    {NULL,0} if none / OS_UNSURE
     (def_cmd_map, def_cmd_size)  = get_command_map()/get_command_map_size()
     (def_layer_map,def_layer_sz) = get_layer_map()/get_layer_map_size()
3. disable_command();                     ← ALWAYS (runs prev on_disable)
4. COMMAND TRACK — OS-first, default-fallback, first-match-wins:
      scan os_cmd_map; first match_pattern() hit → command_found = &it; break.
      if not found: scan def_cmd_map; first hit → command_found = &it; break.
      (an OS-map match PREVENTS the default command map from being scanned)
5. LAYER TRACK — same rule, INDEPENDENT of the command track:
      scan os_layer_map; first hit → layer_found = it.layer; break.
      if not found: scan def_layer_map; first hit → layer_found; break.
6. deactivate_layer();                    ← ALWAYS (clears prev notifier layer)
7. if command_found: enable_command(it).  ← fires on_enable
8. if layer_found != LAYER_UNSET: activate_layer(it).  ← layer_on
9. (CONSOLE_ENABLE) print per-track match/miss; GS shown as '|'.
10. return (command_found || layer_found).
```

> **Ordering unchanged; scan now OS-first.** Disable-before-scan,
> deactivate-before-activate still hold (clean transitions, exactly one active
> layer). The only multi-OS change is that each scan consults the OS-specific
> map first and falls back to the default. A match in the OS map for a track
> **prevents** the default map for that *same* track from being consulted; the
> other track is unaffected (§2 F8.4/F8.5).

### 8.7 `notifier_set_os(os_variant_t os)` — the OS selector

```c
void notifier_set_os(os_variant_t os) {
    if (os == current_os) return;                 /* idempotent: no flap on repeat */
    #ifdef CONSOLE_ENABLE
    uprintf("notifier: OS %u -> %u; clearing state\n", (unsigned)current_os, (unsigned)os);
    #endif
    current_os = os;
    disable_command();      /* fires prev on_disable if a command was active */
    deactivate_layer();     /* turns off the active notifier layer if any     */
    /* Intentionally do NOT re-dispatch the last message. The next focus-change
     * message from the host re-establishes state under the new maps (F9.2). */
}
```

Called by the keymap, conventionally from `process_detected_host_os_kb` (see
§10.1 step 3). Guarantees F9: a stable OS change clears any layer/command
chosen under the previous OS's maps, so no stale cross-OS state persists. It is
the **sole** mutation point for `current_os` (§13-invariant-17).

### 8.8 `hid_notify(uint8_t *data, uint8_t length)` — the entry point

```
1. if (length < 2 || data[0] != 0x81 || data[1] != 0x9F) return;   ← coexistence guard
2. data += 2; length -= 2;                                            ← strip magic header
3. match = false;
4. for each byte c in data[0..length):
     if c == ETX (0x03):
         msg_buffer[msg_index] = '\0';
         sanitize_string(msg_buffer);
         msg_index = 0;
         match = process_full_message(msg_buffer);
         break;
     else:
         if msg_index < MSG_BUFFER_SIZE-1: msg_buffer[msg_index++] = c;
         else: msg_index = 0;            ← overflow: drop message, reset
5. uint8_t response[32] = {0}; response[0] = match ? 1 : 0;
   raw_hid_send(response, RAW_REPORT_SIZE);
```

Note: a single report may contain a partial message (no ETX) → bytes accumulate
in `msg_buffer` across successive `hid_notify` calls until ETX arrives. The
response is sent **once per report** (not once per message); only the report
that contained the ETX will have `match == true`.

---

## 9. File Specification: `rules.mk`

Exactly two lines:

```make
RAW_ENABLE = yes
SRC += qmk_notifier/notifier.c
```

`RAW_ENABLE` turns on QMK's Raw HID feature (usage page `0xFF60` / usage `0x61`,
32-byte reports). `SRC +=` compiles `notifier.c` (which itself `#include`s
`pattern_match.c`). The user's keymap pulls this in with:

```make
include keyboards/<...>/<keyboard>/qmk_notifier/rules.mk
```

Do **not** hand-write `SRC += lib/...` or point at a non-existent
`qmk_notifier.c` — that fails to link.

---

## 10. The User-Facing API & Reference Keymap

### 10.1 Integration (the end user's steps)

1. **Add the submodule** under the keyboard dir:
   ```bash
   cd <qmk_firmware>/keyboards/<your_keyboard>
   git submodule add https://github.com/dabstractor/qmk_notifier.git qmk_notifier
   ```
2. **In the keymap's `rules.mk`:**
   ```make
   include keyboards/<...>/<keyboard>/qmk_notifier/rules.mk
   ```
   **Multi-OS users only** add QMK's OS-detection feature (single-OS / default-only
   users skip this — multi-OS is inert without it):
   ```make
   OS_DETECTION_ENABLE = yes
   ```
3. **In `keymap.c`** — wire `raw_hid_receive → hid_notify` (everyone) and, for
   multi-OS, push the detected OS into the module:
   ```c
   #include QMK_KEYBOARD_H
   #include "./qmk_notifier/notifier.h"

   void raw_hid_receive(uint8_t *data, uint8_t length) {
       hid_notify(data, length);
       /* other Raw HID modules can be called here too */
   }

   /* Multi-OS only: the sole required call to feed the detected OS in. */
   bool process_detected_host_os_kb(os_variant_t os) {
       notifier_set_os(os);          /* ← enables DEFINE_*_OS map selection   */
       /* …your existing OS-specific logic (e.g. enable_vim_for_mac())… */
       return true;
   }
   ```
4. **Define rules** with the two macros (see reference keymap below).
5. `qmk compile -kb <kb> -km <km>` then `qmk flash`.

### 10.2 Reference keymap (validated against this spec)

The maintainer's Dactyl-Manuform 5×7 (RP2040, split, `SERIAL_DRIVER = vendor`)
exercises every construct. Rebuilding the matcher must keep all of these
matching exactly:

```c
DEFINE_SERIAL_COMMANDS({
    { "neovide", &disable_vim },
    { WT("*tty$", "^terminal$"), &disable_vim },
    { WT("*tty$", "*tty"), &disable_vim },
    { "*iterm*", &disable_vim },
    { WT("^Claude$", "^Claude$"), &vim_lazy_insert, &disable_vim },   // exact class+title
    { WT("*chrome*", "*claude*"), &vim_lazy_insert, &disable_vim },
    { WT("*chrome*", "*chatgpt*"), &vim_lazy_insert, &disable_vim },
    { WT("^brave-browser$", "*Claude - Brave$"), &vim_lazy_insert, &disable_vim },
    { "Mulletware Wiki", &vim_lazy_insert, &disable_vim },
    { WT("*", "*orderlands*"), &disable_vim },
    { WT("steam_app*", "*"), &disable_vim },
    { WT("cs2", "Counter-Strike 2"), &disable_vim },                  // case-insensitive default
});

DEFINE_SERIAL_LAYERS({
    { "*calculator", _NUMPAD },
    { WT("*chrome*", "*jitsi*"), _JITSI },
    { WT("tty$", "^terminal$"), _TERMINAL },
    { "*iterm*", _TERMINAL },
    { WT("*alacritty*", "*matterhorn*"), _MATTERHORN },
    { "*clickup*", _CLICKUP },
    { "*neovide*", _NEOVIM },
    { "chrome*", _BROWSER },
    { WT("brave-browser", "*"), _BROWSER },
    { WT("firefox", "*"), _BROWSER },
    { "*inkscape*", _INKSCAPE },
    { "blender", _BLENDER },
    { "borderlands*", _GAMING },
    { WT("steam_app*", "*orderlands*"), _GAMING },
    { "steam_app*", _GAMING },
    { WT("cs2", "Counter-Strike 2"), _GAMING },
});
```

**What this demonstrates (and therefore what the desktop must produce):**
- Bare patterns match the **class** alone: `"chrome*"`, `"blender"`, `"steam_app*"`.
- `WT(class, title)` matches both halves: `WT("brave-browser", "*Claude - Brave$")`.
- Anchors for precision: `WT("^Claude$", "^Claude$")` (so a browser tab titled
  "Claude" doesn't trip the desktop-app rule).
- `case_sensitive` defaults to `false` (`"Counter-Strike 2"` matches case-insensitively).
- Commands and layers are independent — one window can fire both.

### 10.3 Multi-OS reference configuration (opt-in)

Multi-OS is an **opt-in overlay** on §10.2. Enable OS detection in `rules.mk`
(`OS_DETECTION_ENABLE = yes`), push the detected OS into the module from the
keymap's `process_detected_host_os_kb` (§10.1 step 3), and define per-OS maps
with `DEFINE_SERIAL_*_OS`. Rules shared across OSes stay in the default
`DEFINE_SERIAL_*` maps and are consulted whenever an OS-specific map does not
match (§2 F8.4).

```c
/* keymap.c — excerpt */
bool process_detected_host_os_kb(os_variant_t os) {
    notifier_set_os(os);                       /* ← the one required call       */
    if (os == OS_MACOS) enable_vim_for_mac();  /* your existing mac logic       */
    return true;
}

/* Default maps: OS-AGNOSTIC rules live here (gaming, calculator, …). */
DEFINE_SERIAL_COMMANDS({
    { WT("steam_app*", "*"), &disable_vim },
    { WT("cs2", "Counter-Strike 2"), &disable_vim },
});
DEFINE_SERIAL_LAYERS({
    { "*calculator", _NUMPAD },
    { "blender", _BLENDER },
    { "steam_app*", _GAMING },
});

/* macOS-specific: same conceptual apps report different class strings
 * (Terminal/iTerm, "Google Chrome"). Scanned FIRST when current_os == OS_MACOS;
 * a match here prevents the default map for that track from running. */
DEFINE_SERIAL_COMMANDS_OS(OS_MACOS, {
    { "iTerm", &disable_vim },
    { "Terminal", &disable_vim },
    { WT("Google Chrome", "*claude*"), &vim_lazy_insert, &disable_vim },
});
DEFINE_SERIAL_LAYERS_OS(OS_MACOS, {
    { "iTerm", _TERMINAL },
    { "Terminal", _TERMINAL },
    { WT("Google Chrome", "*"), _BROWSER },
});

/* Linux-specific (Hyprland/X11 class names). */
DEFINE_SERIAL_LAYERS_OS(OS_LINUX, {
    { "*alacritty*", _TERMINAL },
    { "*kitty*", _TERMINAL },
    { "firefox", _BROWSER },
});
```

**Worked example — `current_os == OS_MACOS`, message `Google Chrome\x1DClaude`:**
- *Command track:* OS_MACOS map scanned first → `WT("Google Chrome", "*claude*")`
  matches → `vim_lazy_insert` fires; default command map **not consulted**.
- *Layer track:* OS_MACOS map scanned first → `WT("Google Chrome", "*")` matches
  → `_BROWSER`; default layer map **not consulted**.

**Worked example — `current_os == OS_MACOS`, message `blender\x1D`:**
- *Command track:* OS_MACOS map scanned → no match → **fall back** to default
  command map → no match → none.
- *Layer track:* OS_MACOS map scanned → no match → **fall back** to default layer
  map → `"blender"` matches → `_BLENDER`. *(Shows the two tracks falling back
  independently — exactly the F8.5 invariant.)*

**Worked example — `current_os == OS_UNSURE`** (pre-detection, or feature off):
- Both tracks use the **default** maps only — identical to the pre-multi-OS
  firmware. `DEFINE_*_OS` maps are inert.

> **Split-keyboard note.** Raw-HID receive is master-only, so the notifier
> effectively runs on the master half; the slave's layer state mirrors via the
> existing split transport. `process_detected_host_os_kb` runs on the master; if
> you need the detected OS on the slave too, set `SPLIT_DETECTED_OS_ENABLE`
> (the notifier itself needs no split-specific wiring).

---

## 11. Build & Test (the acceptance gate)

The tests are the spec. A from-scratch rebuild is **done** only when every test
suite reports `0 failures` and the pathological-NFA stress completes in < 50 ms.

### 11.1 Build all suites (exact flags — copy/paste)

```bash
gcc -o test_pattern_match             test_pattern_match.c             pattern_match.c
gcc -o test_char_classification       test_char_classification.c       pattern_match.c
gcc -o test_word_boundary_basic       test_word_boundary_basic.c       pattern_match.c
gcc -o test_word_boundary_integration test_word_boundary_integration.c pattern_match.c
gcc -o test_metachar_verification     test_metachar_verification.c     pattern_match.c
gcc -o test_comprehensive_integration test_comprehensive_integration.c pattern_match.c -std=c99 -DNOTIFIER_STUB
gcc -o test_error_handling            test_error_handling.c            pattern_match.c -I.
gcc -o test_memory_stress             test_memory_stress.c             pattern_match.c -I.
gcc -o test_invalid_patterns          test_invalid_patterns.c          pattern_match.c -I.
# notifier.c dispatch + reassembly + F4 + ack (stub-compiled; see run_notifier_stub_tests.sh):
gcc -o test_notifier_dispatch -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
        notifier.c qmk_stubs/qmk_stubs.c test_notifier_dispatch.c -std=c99
# multi-OS selection / fallback / clear (same stub harness):
gcc -o test_notifier_os      -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
        notifier.c qmk_stubs/qmk_stubs.c test_notifier_os.c -std=c99
```

Or simply `./run_all_tests.sh` (rebuilds the nine `pattern_match` suites, runs
them, prints a summary, then builds & runs a performance micro-benchmark), plus
`./run_notifier_stub_tests.sh` which stub-compiles `notifier.c` and runs the
`test_notifier_dispatch` and `test_notifier_os` suites.

### 11.2 Acceptance gate — all must be true

**(A) Every suite has 0 failures:**
```bash
for t in test_pattern_match test_char_classification test_word_boundary_basic \
         test_word_boundary_integration test_metachar_verification \
         test_comprehensive_integration test_error_handling test_memory_stress \
         test_invalid_patterns; do
  printf "%-36s fails=%s\n" "$t" "$(./$t 2>&1 | grep -c '^FAIL:')"
done
# expect fails=0 for every line
```

**(B) The pathological case that used to hang now finishes in milliseconds:**
```bash
cat > /tmp/nfa_stress.c <<'EOF'
#include <stdio.h>
#include <time.h>
#include "pattern_match.h"
int main(void){
  char s[200]; for(int i=0;i<199;i++) s[i]='a'; s[199]='\0';
  const char* p="a+a+a+a+a+a+a+a+a+a+b";
  clock_t t=clock(); int r=pattern_match(p,s,1);
  printf("result=%d  %.1f us\n", r, 1e6*(double)(clock()-t)/CLOCKS_PER_SEC);
  return 0;
}
EOF
gcc -O2 -w /tmp/nfa_stress.c pattern_match.c -I. -o /tmp/nfa_stress
timeout 5 /tmp/nfa_stress   # must print result=0 in < 50 ms
```

**(C) Realistic patterns still match (all six must print `1`):**
```bash
cat > /tmp/nfa_real.c <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  printf("%d\n", pattern_match("\\w+","hello",1));                       /* 1 */
  printf("%d\n", pattern_match("\\b\\w+\\b\\s+\\b\\w+\\b","hello world",1)); /* 1 */
  printf("%d\n", pattern_match("^\\w+@\\w+$","user@host",1));             /* 1 */
  printf("%d\n", pattern_match("v\\.code","v.code",1));                  /* 1 */
  printf("%d\n", pattern_match("a+b","aaab",1));                         /* 1 */
  printf("%d\n", pattern_match("*slack*","Slack - general",0));          /* 1 */
  return 0;
}
EOF
gcc -w /tmp/nfa_real.c pattern_match.c -I. -o /tmp/nfa_real && /tmp/nfa_real
```

**(D) Multi-OS selection (stub-compiled `notifier.c`):**

```bash
./run_notifier_stub_tests.sh   # must end: ✓ notifier stub-compile gate PASSED,
                               #   and test_notifier_dispatch + test_notifier_os
                               #   each report 0 FAIL: lines
```

`test_notifier_os` MUST verify, at minimum: (i) OS-specific map selected and the
default skipped when `current_os` is set and the OS map matches — per track; (ii)
default map used as fallback when the OS map is absent, matches nothing, or
`current_os == OS_UNSURE`; (iii) command and layer tracks fall back
**independently**; (iv) `notifier_set_os` idempotent on an unchanged value (no
spurious `on_disable`/deactivate); (v) `notifier_set_os` on a **changed** value
clears state (previous `on_disable` fires, active layer deactivated) and does
**not** re-dispatch; (vi) a default-only configuration (no `DEFINE_*_OS`) behaves
identically with/without `notifier_set_os` (backward compatibility).

### 11.3 Test inventory (what each suite covers)

| Suite | Compiles with | Counts / Style | Covers |
|---|---|---|---|
| `test_pattern_match` | `pattern_match.c` | 376 numbered cases | anchors, escapes, wildcards, case sensitivity, parsing, edge cases, metachars w/ anchors/wildcards, word-boundary escape processing |
| `test_char_classification` | `pattern_match.c` | 179 cases | digit/word/whitespace classification (indirect via metachars) |
| `test_word_boundary_basic` | `pattern_match.c` | 74 cases | `\b`/`\B` boundary semantics |
| `test_word_boundary_integration` | `pattern_match.c` | 189 cases | `\b`/`\B` integrated with anchors/wildcards/classes |
| `test_metachar_verification` | `pattern_match.c` | boolean PASS/FAIL printout | `\d \D \w \W \s \S` smoke test + combos |
| `test_comprehensive_integration` | `pattern_match.c -std=c99 -DNOTIFIER_STUB` | 10 categories | multi-feature combos, performance, memory management |
| `test_error_handling` | `pattern_match.c -I.` | survival/case counts | NULL/garbage inputs, malformed escapes |
| `test_memory_stress` | `pattern_match.c -I.` | survival | long strings, repeated alloc/free (no leaks/crashes) |
| `test_invalid_patterns` | `pattern_match.c -I.` | 1008 cases (920 combo + …) | 46 pathological patterns × many inputs |
| `test_notifier_dispatch` | `notifier.c` + `qmk_stubs/` (`-DQMK_KEYBOARD_H=…`) | PASS/FAIL cases | reassembly, ETX framing, F4 delimiter matrix, BUG-1 NULL safety, dispatcher ordering, ack, coexistence guard |
| `test_notifier_os` | `notifier.c` + `qmk_stubs/` (`-DQMK_KEYBOARD_H=…`) | PASS/FAIL cases | F8 merge/fallback per track, per-map-type independence, OS_UNSURE→default, F9 clear-on-change idempotence |

**Total assertion-level checks across all suites: ~1 826 (`pattern_match`
corpus) plus the `notifier` stub-compile gate cases, all must pass.** The 9
`pattern_match`-only suites remain the matcher regression gate; the
`notifier_*` suites gate the receiver / dispatcher / multi-OS selection.
(The README's older "1 992 / 2 048" figures and per-suite counts like "383/263"
are stale; the live counts above are authoritative — `./run_all_tests.sh` prints
the real totals.)

### 11.4 The test framework

Each counting suite uses the same pattern: a `test_case_t { pattern, input,
case_sensitive, expected, description }` table; a `run_test(t)` helper that
calls `pattern_match`, compares to `expected`, prints `PASS:`/`FAIL:`; and a
final summary (`Total tests run: N / Tests passed: P / Tests failed: F /
Success rate: …%`). `run_all_tests.sh` greps these summary lines to aggregate.
The exit code is non-zero iff any suite failed.

---

## 12. Non-Functional Requirements

- **Performance.** Pattern matching is sub-microsecond per call for realistic
  patterns (~0.1 µs measured). The NFA is linear in `states × strlen` for **all**
  inputs, with no catastrophic backtracking. The receive hot path does no dynamic
  allocation; `process_escapes` does exactly one `malloc`+`free` per
  `pattern_match` call (pre-existing; out of scope to remove).
- **Robustness.** NULL inputs → `false`. Non-ASCII bytes stripped. Oversized
  messages dropped. Malformed/unknown escapes kept literal. No input crashes the
  matcher (verified by `test_error_handling`, `test_memory_stress`,
  `test_invalid_patterns` — 920 pathological pattern/input combinations).
- **MCU footprint.** 256 B static reassembly buffer; ~6–8 KB stack for the NFA
  pool at `NFA_MAX_PATTERN=128` (lower for AVR). No `malloc` on the receive path.
- **Backward compatibility.** The matcher is a strict superset of the original
  glob matcher; unanchored patterns without escapes match identically to before.
- **Coexistence.** The `0x81 0x9F` guard means the module never reacts to other
  modules' messages and never breaks them.

---

## 13. Key Invariants a Dev Must Preserve

1. **Magic header is exactly `0x81 0x9F`.** The coexistence guard checks
   `data[0]==0x81 && data[1]==0x9F`. Never change without coordinating both halves.
2. **GS is `0x1D`; ETX is `0x03`.** The payload is `class\x1Dtitle`; ETX
   terminates the reassembled message. Never change unilaterally.
3. **`RAW_REPORT_SIZE` is 32.** This is the logical report on every QMK protocol;
   it is **not** `RAW_EPSIZE`. `raw_hid_send` must be called with `32`.
4. **Disable-before-scan, deactivate-before-activate.** `process_full_message`
   always calls `disable_command()` then scans, then `deactivate_layer()` then
   activates. This yields clean transitions and exactly-one-active-layer.
5. **First-match-wins** in each map (scan order = definition order).
6. **Exactly one notifier layer active at a time.** `LAYER_UNSET = 255`.
7. **An unmatched message clears state** (deactivates layer, disables command).
8. **Glob `*` matches any char including `\n`/`\r`;** dot `.` matches any char
   **except** `\n`/`\r`. Do not conflate them (§7.5, §7.7).
9. **`+` is a postfix quantifier (`X+`), `*` is a standalone wildcard token.**
   They compile differently in the NFA (§7.5) — do not conflate.
10. **Absolute position for `\b`/`\B`** must be computed from `string_start`
    (the original string base), not from the per-offset `str` (§7.5).
11. **The `lastlist` generation tag is mandatory** in the NFA — without it,
    `OP_SPLIT` and `\b\b` recurse infinitely.
12. **No observable result may change.** The test suites encode the exact
    intended semantics. If a test flips red, fix the matcher, not the test.
13. **Weak defaults must remain** so a keymap without `DEFINE_*` still links.
14. **`pattern_match.c` must compile both ways:** `#include`d by `notifier.c`
    (QMK build) and as a standalone translation unit (host tests).
15. **Multi-OS is merge/fallback, per track, OS-first.** For each of
    {command_map, layer_map}, `process_full_message` scans the OS-specific map
    for `current_os` first; a match there wins and the default map for that type
    is **not** scanned. No OS map (or no match in it) ⇒ scan the default map
    (§2 F8.4, §8.6). The two tracks decide independently (invariant 5 / F5.5
    preserved).
16. **`OS_UNSURE` ⇒ default maps only** (the boot state, unsure detection, and
    the `OS_DETECTION_ENABLE = no` case). No OS-specific map exists for
    `OS_UNSURE` by design.
17. **The OS signal is push-only.** The module never calls `detected_host_os()` —
    it has no link dependency on the OS-detection subsystem. `current_os` changes
    only via `notifier_set_os()`, called by the keymap (§2 F8.2).
18. **`notifier_set_os` is idempotent on an unchanged value** (no-op) and, on a
    changed value, clears state (`disable_command()` + `deactivate_layer()`)
    without re-dispatching (§2 F9). No layer/command chosen under a previous OS
    survives into a new OS.
19. **A default-only keymap is byte-identical to the pre-multi-OS firmware.**
    Zero `DEFINE_*_OS` macros ⇒ all per-OS accessors return `{NULL,0}` ⇒ every
    dispatch uses the default maps exactly as before. This is the prime
    backward-compatibility directive.
20. **Per-OS weak accessors must remain** so a keymap that omits some/all
    `DEFINE_*_OS` macros still links (F8.7). The symbol names generated by the
    `##os` token-paste (`_notifier_*_OS_MACOS`, etc.) must match the names in
    `select_*_map_os()` exactly.
21. **Multi-OS is orthogonal to the typed-command namespace.** OS
    selection only chooses board maps for the legacy *string* path; it never
    consumes the `0xF0` discriminator or touches the `host_layer`/
    host-callback trackers (§4.7, §14).

---

## 14. Host-Side Rules & Typed Commands

Host-side rules move app→layer and app→callback matching onto the desktop host
(editable `rules.toml`, no reflash). **The firmware's role is to receive typed
commands, expose a named callback registry, track a host layer + host-callback
state separate from its board state, and honor a per-window stack/replace
decision.** The host-side design is canonical in `qmkonnect/spec/HOST_RULES.md`;
the transport in the `qmk-notifier` crate `PRD.md`; the wire contract here in
§4.6.

> **Matcher location.** The host-side pattern matcher lives in **`qmkonnect`**
> (not the `qmk-notifier` crate), with full parity to this module's
> `pattern_match.c` (the firmware matcher + its test corpus are the single source
> of truth for match semantics). The crate is transport-only.

**Coexistence — host chooses "stack" vs "replace" per window via `clear_board`.**
The firmware offers **both**, and the host selects which on each window change
through the `clear_board` flag on `APPLY_HOST_CONTEXT` (§4.6). Board
(`DEFINE_*`/`DEFINE_*_OS`) rules are never silently discarded — the host decides,
per matched rule, whether the board runs:

- **Stack** (`clear_board = 0`): the host first sends the legacy **string** (the
  board runs its rules → sets `activated_layer` + current command → replies),
  *then* sends `APPLY_HOST_CONTEXT{layer, callbacks, clear_board=0}`. Board
  layer/command remain active; the host layer stacks above; board callbacks fire
  first, host callbacks after.
- **Replace** (`clear_board = 1`): the host sends **only**
  `APPLY_HOST_CONTEXT{layer, callbacks, clear_board=1}` (no string). The firmware
  `deactivate_layer()`s its board layer and `disable_command()`s its board
  command, then applies the host layer + callbacks. Board rules are inert for
  that window and re-engage normally on the host's next string send.

The host's per-rule `disable_firmware_config` flag (default `false`, in
`rules.toml`) drives this: a matched rule with `disable_firmware_config = true`
contributes to a **replace** decision; the window is replace iff **every** matched
rule is disabling (the string is shared by both board lanes, so it is sent iff the
board has rules **and** ≥1 matched rule is non-disabling). See
`qmkonnect/spec/HOST_RULES.md`.

**Firmware requirements:**

- **Named callback registry** — `DEFINE_HOST_CALLBACKS({ … })` + weak-default
  accessors, mirroring the existing `command_map` pattern. `ID = array index`,
  stable per build; the host re-queries names on every reconnect so cross-flash
  renumbering is harmless. Bounded by `HOST_CALLBACK_MAX` (static array);
  `QUERY_INFO.callback_count` reports the true count.
  ```c
  typedef struct { const char *name; callback_t on_enable; callback_t on_disable; } host_callback_t;
  host_callback_t* get_host_callbacks(void);
  size_t           get_host_callbacks_size(void);
  #define DEFINE_HOST_CALLBACKS(...)  /* defines user_host_callbacks[] + accessors */
  ```
- **Second layer tracker** `host_layer` (independent of board `activated_layer`)
  and a host-callback enable set `host_cb_enabled[]`. `set_host_layer(layer)`:
  `layer_on`/`layer_off` only the host tracker; `0xFF` ⇒ clear. Host layers are
  reserved **≥ 224** (`LAYER_UNSET = 255`).
  ```c
  #define LAYER_UNSET 255
  static uint8_t host_layer = LAYER_UNSET;
  static bool host_cb_enabled[HOST_CALLBACK_MAX];
  static void set_host_layer(uint8_t layer);          /* on/off/clear, host tracker only */
  static void apply_host_callbacks(const uint8_t *ids, uint8_t count); /* disable-before-enable diff */
  ```
- **Typed-command dispatch** at the top of `hid_notify()`: if `length >= 3 &&
  data[2] == 0xF0`, route to `handle_typed_command()` and return; else the legacy
  string path runs unchanged (§8.6). `QUERY_INFO`/`QUERY_CALLBACK` are answerable
  before any string has been seen. `APPLY_HOST_CONTEXT` honors `clear_board`
  (above) then calls `set_host_layer()` + `apply_host_callbacks()`. `SET_OS`
  updates `current_os` (authoritative while a host is connected, §4.7). A
  `has_been_queried` bool is set on the first `QUERY_INFO` serviced (§4.6
  handshake timing). `apply_host_callbacks` mirrors the board's
  disable-before-enable ordering: disable newly-out-of-set ids (fire
  `on_disable`), then enable newly-in-set ids (fire `on_enable`).
- **Backward compatibility:** legacy string sends have `data[2]` = a printable
  char (never `0xF0`), so the dispatch is transparent to keymaps that don't use
  host rules. `0xF0` can never begin a real matched string (sanitizer allows only
  `0x20–0x7E`), so a string-only host coexists; typed responses use marker
  `0x51` (≥2), distinct from the legacy `0`/`1` match-bool.

**Resolved design decisions.** `SET_OS` makes the host authoritative for
`current_os` while connected (§4.7), with the firmware `OS_DETECTION` heuristic
as the no-host fallback. Stack-vs-replace is not a global mode: the firmware
offers both and the host selects per window via `clear_board`, driven by the
host's per-rule `disable_firmware_config` (above). (Earlier drafts considered a
single global "replace" mode and a `dabstractor/qmkonnect/PRPs/002` artifact;
both are superseded by this per-window design.)

## 15. Appendix A — Pattern-Semantics Reference Table

Verified against the live test suite (`/tmp/probe` harness, §11.2C style).

| Pattern | Input | cs | Result | Why |
|---|---|---|---|---|
| `*` | `a\nb` | 1 | **true** | glob matches newline |
| `a.b` | `a\nb` | 1 | **false** | dot excludes `\n`/`\r` |
| `a.b` | `axb` | 1 | true | dot matches any other char |
| `a+b` | `aaab` | 1 | true | `+` = one-or-more |
| `a+b` | `b` | 0 | false | `+` needs ≥1 |
| `\^` | `^` | 1 | true | escaped caret literal |
| `a\+b` | `a+b` | 1 | true | escaped plus literal |
| `v\.code` | `v.code` | 1 | true | escaped dot literal |
| `\x` | `\x` | 1 | true | unknown escape kept literal (2 bytes) |
| `\bword\b` | `a word here` | 0 | true | word-boundary anchored (case-insensitive) |
| `^$` | `` | 1 | true | empty exact |
| `neovide` | `code\x1Dmain.rs` | 0 | false | bare pattern matches **class only** |
| `^hello` | `hello world` | 0 | true | start anchor |
| `world$` | `hello world` | 0 | true | end anchor |
| `^hello$` | `hello` | 1 | true | full anchor (exact) |
| `^hello$` | `hello world` | 1 | false | full anchor rejects non-exact |
| `sear*term` | `presearchtermpost` | 1 | true | unanchored wildcard substring |
| `abc` | `ABC` | 0 | true | case-insensitive substring |
| `abc` | `ABC` | 1 | false | case-sensitive no-match |

**Construct reference:**

| Construct | Meaning |
|---|---|
| `*` | Wildcard — any sequence (incl. empty, incl. `\n`/`\r`) |
| `^` at start | Anchor to beginning |
| `$` at end | Anchor to end |
| `^…$` | Exact full-string match |
| `\^ \$ \* \\` | Literal escaped char |
| `\. \+` | Literal dot / plus |
| `\d \D` | Digit / non-digit |
| `\w \W` | Word char `[A-Za-z0-9_]` / non-word |
| `\s \S` | Whitespace `[ \t\n\r\f\v]` / non-whitespace |
| `\b \B` | Word boundary / non-boundary (zero-width) |
| `.` | Any char except `\n`/`\r` |
| `X+` | One or more of element X (linear-time) |
| (no anchors) | Substring match (backward-compatible default) |

> **Host-rules matcher parity:** the host-side matcher (in `qmkonnect`) is a
> full-parity port of this matcher — `*`, `^`, `$`, two-part `WT`, `+`, and the
> classes (`\d \w \s \b .`) — all linear-time.

---

## 16. Appendix B — Constants Reference

| Constant | Value | Where | Meaning |
|---|---|---|---|
| Magic header | `0x81 0x9F` | first 2 payload bytes | qmk_notifier coexistence guard |
| Group Separator (GS) | `0x1D` (29) | `GS_DELIMITER` | class\|title delimiter in payload |
| End of Text (ETX) | `0x03` (3) | `ETX_TERMINATOR` | message terminator (appended by the transport crate) |
| `RAW_REPORT_SIZE` | `32` | notifier.c | logical HID report size (all QMK protocols) |
| `MSG_BUFFER_SIZE` | `256` | notifier.c | reassembly buffer |
| `LAYER_UNSET` | `255` | notifier.c | "no notifier layer active" sentinel |
| Payload per report | `30` | transport crate | `RAW_REPORT_SIZE - 2` (after magic) |
| Usage page | `0xFF60` | QMK `RAW_USAGE_PAGE` | auto-discovery primary id |
| Usage | `0x61` | QMK `RAW_USAGE_ID` | auto-discovery primary id |
| `NFA_MAX_PATTERN` | `128` | pattern_match.c | max processed-pattern length |
| `NFA_MAX_STATES` | `2*128+2=258` | pattern_match.c | NFA state-pool cap |
| Request discriminator | `0xF0` | §4.6 | typed-command marker (after `0x81 0x9F`) |
| Response marker | `0x51` | §4.6 | typed-response marker (vs legacy `0`/`1`) |
| `current_os` init | `OS_UNSURE` (0) | notifier.c | no OS known ⇒ default maps only |
| OS enum | `os_variant_t` | `os_detection.h` (QMK) | `OS_UNSURE/0 OS_LINUX/1 OS_WINDOWS/2 OS_MACOS/3 OS_IOS/4` — reused, not redefined |
| `SET_OS` typed cmd | `0x03` | §4.6, §4.7 | host-authoritative OS while connected |
| `proto_ver` | `1` (legacy string-only), `2` (typed-command capable) | §4.6 | protocol version (firmware-owned) |
| `APPLY_HOST_CONTEXT` `clear_board` | flags bit 0 | §4.6 | clear board layer/cmd before applying host context |
| Host layer block | `≥ 224` | §4.6, §14 | host layers resolve above board layers (`255 = LAYER_UNSET`) |

Placeholder bytes (processed pattern, §7.1): `0x01–0x04` escaped literals
(`^ $ * \`), `0x05–0x0A` classes (`\d \D \w \W \s \S`), `0x0B/0x0C`
(`\b`/`\B`), `0x0D` (dot), `0x0E` (`+` quantifier marker).

---

## 17. Appendix C — File Sizes & Live Source of Truth

Approximate line counts (a rebuild should be in this ballpark; exact text need
not match byte-for-byte as long as all tests pass and all invariants hold):

| File | Lines | Role |
|---|---|---|
| `notifier.h` | ~80 | public API (now incl. OS-selection decls + `os_detection.h`) |
| `notifier.c` | ~410 | receiver + dispatcher + multi-OS selection |
| `pattern_match.h` | ~53 | matcher decl + doc |
| `pattern_match.c` | ~514 | Thompson NFA matcher |
| `rules.mk` | 2 | QMK integration |
| `run_all_tests.sh` | ~181 | test runner + perf bench |
| `test_pattern_match.c` | 843 | 376-case main suite |
| `test_char_classification.c` | 299 | 179 cases |
| `test_word_boundary_basic.c` | 206 | 74 cases |
| `test_word_boundary_integration.c` | 382 | 189 cases |
| `test_metachar_verification.c` | 51 | smoke test |
| `test_comprehensive_integration.c` | 449 | 10 categories |
| `test_error_handling.c` | 566 | survival |
| `test_memory_stress.c` | 238 | survival |
| `test_invalid_patterns.c` | 292 | 1008 cases |
| `qmk_stubs/*` | ~60 | QMK header fakes for host-testing `notifier.c` (incl. header-only `os_detection.h`) |
| `run_notifier_stub_tests.sh` | ~45 | stub-compile gate for `notifier.c` + dispatch/OS suites |
| `test_notifier_dispatch.c` | ~90 | reassembly/F4/ordering/ack host tests |
| `test_notifier_os.c` | ~150 | multi-OS selection/fallback/clear host tests |

> **Living source of truth:** the production codebase itself
> (`notifier.c`, `notifier.h`, `pattern_match.c`, `pattern_match.h`, `rules.mk`)
> and the test corpus (`test_*.c`, `run_all_tests.sh`). Where this spec and the
> code disagree, **the code + the passing tests win**; report the drift. This
> spec captures the intended design at the current `HEAD`.

---

## 18. Community Module Distribution (Planned Migration)

> **Status: planned, not yet implemented.** This section specifies the
> requirements for redistributing this module as a **QMK Community Module**
> instead of a keymap-local git submodule. It is forward-looking; the current
> build (§9, §10.1) remains the submodule flow until this section is marked
> done. Nothing here changes the wire protocol (§4), the matcher (§7), or the
> runtime behavior (§8/§14).

### 18.1 Motivation

Today a user must (a) clone this repo *inside* their keyboard directory, (b)
hand-write a keyboard-relative `include .../rules.mk` line, and (c) hand-write
the `SRC +=` / `RAW_ENABLE` wiring. A Community Module makes the module
self-declaring: the user adds one `"modules"` entry to `keymap.json` and the
build discovers `rules.mk`, the source files, and the include path
automatically.

**The underscore rename is what makes this possible.** The QMK build system
derives a module's compile-time identity from the *leaf directory name*
(`COMMUNITY_MODULE_<NAME>_ENABLE`, `process_record_<name>` hooks). A hyphen is
not a valid C identifier, so the old `qmk-notifier` slug could never have been
a module leaf name. `qmk_notifier` (underscore) is a valid identifier, so it
can — the rename was a prerequisite for this migration.

### 18.2 Verified build-system mechanics (ground truth from `qmk_firmware`)

The generator `lib/python/qmk/cli/generate/community_modules.py` emits, per
module (using only the leaf directory name):

- `SRC += $(wildcard <module_path>/<leaf>.c)` — **auto-compiles `<leaf>.c`
  only**; any other `.c` must be added via the module's `rules.mk`.
- `VPATH += <module_path>` — and `VPATH` is on the compiler include path
  (`builddefs/build_keyboard.mk`: `..._INC := $(VPATH) $(EXTRAINCDIRS) ...`),
  so `#include "notifier.h"` from the user's keymap and `#include
  "pattern_match.c"` from `notifier.c` both resolve.
- `-include <module_path>/rules.mk` — so `RAW_ENABLE = yes` set there takes
  effect globally. `RAW_ENABLE` is **not** a data-driven feature key (there is
  no `rawhid` entry in `data/schemas/`), so it MUST be set in `rules.mk`, not
  in `qmk_module.json` `features`.
- `-DCOMMUNITY_MODULE_<LEAF>_ENABLE=TRUE` and module hooks `*_<leaf>` —
  require `<leaf>` to be a valid C identifier.

**Hard subsystem limit:** `raw_hid_receive` is **not** in the community-module
hook surface (`data/constants/module_hooks/*.hjson`). The module therefore
**cannot** auto-register on the Raw HID endpoint; the user must still define
`raw_hid_receive` in their keymap and call `hid_notify()`. This is the one
piece of glue that is irreducible.

### 18.3 Requirements

- **R1 — Manifest.** Add `qmk_module.json` at the repo root:
  ```json
  {
      "module_name": "QMK Notifier",
      "maintainer": "dabstractor",
      "license": "<SPDX — confirm the actual repo license; must be GPL-compatible>",
      "url": "https://github.com/dabstractor/qmk_notifier",
      "keycodes": []
  }
  ```
  No `features` block (the module declares no data-driven features). No
  `keycodes` (the public surface is macros + functions invoked from the keymap,
  not keymap-bindable keys).

- **R2 — `rules.mk` rewrite.** The module-context `rules.mk` becomes:
  ```make
  RAW_ENABLE = yes
  SRC += notifier.c
  ```
  `notifier.c` does **not** match the leaf name `qmk_notifier`, so it is not
  auto-compiled by the wildcard and must be listed explicitly.
  `pattern_match.c` is pulled in by `notifier.c`'s `#include "pattern_match.c"`
  (resolved via VPATH), so it is **not** a separate `SRC` entry. The current
  `SRC += qmk_notifier/notifier.c` line is a submodule-context path and is
  replaced, not retained.

- **R3 — API-version assertion, host-test-guarded.** Add at the top of
  `notifier.c`, immediately after `#include QMK_KEYBOARD_H`:
  ```c
  #ifdef COMMUNITY_MODULES_API_VERSION
  ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);
  #endif
  ```
  In a real module build, `QMK_KEYBOARD_H` → `quantum.h` → `community_modules.h`
  defines both the macro and `COMMUNITY_MODULES_API_VERSION`, so the assert
  fires. In the host/stub tests
  (`-DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"'`), neither is defined, so the
  guard skips it and all stub binaries stay green unchanged. Target `1.0.0`
  (the floor that provides `housekeeping_task` and `process_detected_host_os`).

- **R4 — Leaf-directory naming contract.** Users MUST clone to a hyphen-free
  leaf. Because the repo slug is `qmk_notifier` (a valid identifier), the
  recommended clone path is `modules/<org>/qmk_notifier`, giving `keymap.json`
  `"modules": ["<org>/qmk_notifier"]`. A hyphenated leaf is a hard failure
  (the generated `-D` define and any hooks would be invalid C tokens); this
  must be stated explicitly in the README.

- **R5 — README install rewrite.** Replace the "Setup" submodule flow with:
  ```bash
  cd /path/to/your/userspace
  git submodule add https://github.com/dabstractor/qmk_notifier.git modules/<org>/qmk_notifier
  ```
  ```json
  { "modules": ["<org>/qmk_notifier"] }
  ```
  ```c
  #include QMK_KEYBOARD_H
  #include "notifier.h"   /* module dir is on -I via VPATH — no relative path */

  void raw_hid_receive(uint8_t *data, uint8_t length) { hid_notify(data, length); }
  ```
  Gone vs. today: the in-keyboard clone, the `#include "./qmk_notifier/notifier.h"`
  relative path, the `include .../rules.mk` line, and the `SRC +=`/`RAW_ENABLE`
  hand-wiring. **Still required** (the R-limit above): the `raw_hid_receive`
  shim. Multi-OS users still set `OS_DETECTION_ENABLE = yes` and call
  `notifier_set_os` from `process_detected_host_os_kb`.

- **R6 — No behavioral change.** The wire protocol (§4), the matcher (§7), the
  dispatch / OS / host-rules logic (§8 / §14), and the public API in
  `notifier.h` are byte-for-byte unchanged. A keymap that worked under the
  submodule flow must work identically under the module flow, modulo the
  include path and the removal of the `rules.mk` include line.

### 18.4 Tradeoffs & open decisions

- **OS auto-wiring (deferred).** `process_detected_host_os` *is* a module hook,
  so we *could* ship `process_detected_host_os_qmk_notifier()` calling
  `notifier_set_os(detected_host_os())` and remove the user's manual OS push.
  This reverses the "push-only by design — the module never calls
  `detected_host_os()`" invariant (§1.1, §8.7) and forces `OS_DETECTION_ENABLE`
  on every user. **Decision required:** keep push-only (recommended) or
  auto-wire.
- **Legacy submodule flow.** The module-context `rules.mk` (R2) is
  incompatible with the old "clone into keyboard dir + `SRC += ...`" flow.
  **Decision required:** retire the submodule flow entirely (recommended) or
  maintain both.
- **Configurator.** Community Modules cannot be built by the QMK Configurator.
  Irrelevant here — this module requires a custom `raw_hid_receive` in
  `keymap.c`, which JSON keymaps cannot express, so Configurator support was
  never an option.

### 18.5 Acceptance

- [ ] `qmk_module.json` present and valid (`qmk lint` clean against a
      userspace build that lists the module).
- [ ] A clean keymap that adds `"modules": ["<org>/qmk_notifier"]`, includes
      `notifier.h`, defines `raw_hid_receive → hid_notify`, and uses
      `DEFINE_SERIAL_*` compiles and reacts to desktop focus changes
      identically to today.
- [ ] `RAW_ENABLE` is on and the `0x81 0x9F` endpoint is live, with no manual
      `rules.mk` include line in the keymap.
- [ ] Host test gates unchanged: `./run_all_tests.sh` and
      `./run_notifier_stub_tests.sh` pass with the R3 guard in place.
- [ ] README documents only the module flow (R4 leaf-name contract included).

---

## Definition of Done (one-shot rebuild)

A developer agent has correctly one-shot this codebase when **all** of the
following hold:

- [ ] The five source files (`notifier.{c,h}`, `pattern_match.{c,h}`, `rules.mk`)
      exist and reproduce the public API in §5–§9.
- [ ] `./run_all_tests.sh` reports **0 failures** across all 9 `pattern_match`
      suites (~1 826 assertions pass) and the performance micro-benchmark is
      sub-second.
- [ ] The pathological stress `/tmp/nfa_stress` (`a+a+a+a+a+a+a+a+a+a+b` vs
      199×`a`) prints `result=0` in **< 50 ms** (§11.2B).
- [ ] `/tmp/nfa_real` prints six `1`s (§11.2C).
- [ ] Every invariant in §13 holds; the wire-protocol bytes in §4 match exactly.
- [ ] No new compiler warnings beyond pre-existing ones.
- [ ] A keymap that `#include`s `notifier.h`, wires `raw_hid_receive → hid_notify`,
      `include`s `rules.mk`, and uses `DEFINE_SERIAL_*` + `WT(...)` compiles
      cleanly under QMK and reacts to desktop-sent focus changes by switching
      layers / firing callbacks.
- [ ] **Multi-OS (opt-in):** `./run_notifier_stub_tests.sh` passes (incl.
      `test_notifier_os`); a keymap using `DEFINE_SERIAL_*_OS` + `notifier_set_os`
      selects the OS map first (default fallback) per track, clears state on OS
      change, and degrades to default-only under `OS_UNSURE` — while a
      default-only keymap behaves byte-identically to the pre-multi-OS firmware.
- [ ] The typed-command namespace (§4.6), `SET_OS` (§4.7), and the host-side-rules
      support — `DEFINE_HOST_CALLBACKS`, `host_layer`/`host_cb_enabled`,
      `clear_board`, `has_been_queried`, and the
      `QUERY_INFO`/`QUERY_CALLBACK`/`SET_OS`/`APPLY_HOST_CONTEXT` handlers (§14) —
      are implemented and pass their tests.

---

*End of specification. This module is the keyboard half of a two-part system;
see the QMKonnect `PRD.md` and its companion specs (`PROTOCOL.md`,
`FIRMWARE.md`, `HOST_RULES.md`) for the desktop half and the host-side-rules
design.*
