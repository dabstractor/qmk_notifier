# Research — README.md structure & conventions (the file being edited)

## Current section order (top → bottom), with anchors

1. `# QMK-Notifier`
2. `## Features` — bullet list. Has a `**Optional per-OS maps**` bullet that links to
   `#multi-os-configuration`. **PATTERN TO MIRROR** for the new host-rules bullet.
3. `## How It Works` — prose; contains `### Wire format` (the `0x81 0x9F` header +
   `GS_DELIMITER` + `ETX` framing — the legacy contract the new section must say is
   *unchanged*).
4. `## Setup` — `### 1.` / `### 2.` / `### 3.` numbered subsections. `### 2.` shows
   the `raw_hid_receive → hid_notify` wiring + a cross-link to Multi-OS for the
   `process_detected_host_os_kb` line. **THIS IS WHERE the host-rules Setup note
   goes** (per item_description (b): "host-rules users add
   `DEFINE_HOST_CALLBACKS({ … })` and the host negotiates via QUERY_INFO").
5. `## Usage` — `### Define Command Mappings`, `### Define Layer Mappings`,
   `### Pattern Matching Syntax` (the table).
6. `## Multi-OS Configuration` — the opt-in-overlay sibling section. Has these
   subsections: `### How to enable`, `### Defining per-OS maps`,
   `### How matching works (per-OS, then default)`, `### Backward compatibility
   (the guarantee)`, `### Push-only by design`, `### What this does NOT change`.
   - **CRITICAL EDIT TARGET**: the `### What this does NOT change` list has a THIRD
     bullet: *"Host-provided OS and host-side rules are planned future work, not
     implemented."* — this is now STALE (the feature ships in plan 003) and MUST be
     removed/replaced with a pointer to the new section. Leaving it contradicts the
     new feature docs.
7. `## Companion Projects` — links QMKonnect (`https://github.com/dabstractor/qmkonnect`)
   + the `qmk_notifier` transport crate. Has a `> **Naming hazard:**` callout.
8. `## Compatibility with Other Raw HID Modules`
9. `## Documentation` (one-liner linking QMK RawHID docs)
10. `## Performance Considerations`
11. `## Running Tests` — `### Quick Test`, `### Comprehensive Test Suite` (the 9-row
    table + the `run_notifier_stub_tests.sh` two-binary description), `### Current
    Test Status` (claims "all suites green ... 2023/2023 ... 14/14 + 31/31").
    - **EDIT TARGET**: the `run_notifier_stub_tests.sh` description currently says
      "links it into **two** host test binaries" and lists dispatch + os only.
      Must mention the THIRD binary `test_notifier_host`. `### Current Test Status`
      must reflect the actual run (see test_status_state.md).
12. `## Contributing`

## Placement decision for the new section

Insert `## Host-Side Rules & Typed Commands` **immediately after** `## Multi-OS
Configuration` and **before** `## Companion Projects`. Rationale: it is the third
opt-in capability layer (basic → multi-OS → host-rules), it builds on the OS
concept (SET_OS supersedes OS_DETECTION), and it lets the Multi-OS "What this does
NOT change" stale bullet point forward to the very next section.

## Style conventions to match (verified from the existing Multi-OS section)

- Section lead = one short paragraph stating **opt-in nature** first ("X is an
  **opt-in overlay** / **strictly opt-in**"), then the 3-step enable summary.
- Bold lead-in labels in prose: `**Backward compatibility (the guarantee):**`,
  `**Push-only by design:**`, `**What this does NOT change:**`.
- Code fences are ```c / ```make / ```bash with NO language on shell output.
- Cross-links use the GitHub-style auto-anchor: `[Multi-OS Configuration](#multi-os-configuration)`.
- The Features bullet style: a `**Bolded phrase**` then an em-dash-free sentence,
  ending with `See [Section Name](#anchor).`
- "What this does NOT change" is a bulleted list with **bold lead-in** per bullet.
- Inline backticks for every symbol: `DEFINE_HOST_CALLBACKS`, `clear_board`,
  `data[2]`, `0xF0`, `0x51`, `host_layer`, `notifier_set_os`, etc.

## GitHub anchor for the new heading (verified)

`## Host-Side Rules & Typed Commands` → GitHub slugger: lowercase, strip non-
`[a-z0-9 -]` (the `&` is removed, its surrounding spaces remain), spaces→hyphens.
Result anchor = `#host-side-rules--typed-commands` (NOTE the **double hyphen**
where `&` was). The Features bullet + the Multi-OS forward-pointer MUST use this
exact anchor. Verify after writing by clicking the heading link in rendered MD.