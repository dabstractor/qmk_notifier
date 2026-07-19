# Research Notes — P1.M2.T3.S1 (Document has_been_queried as write-only)

## Task
Documentation-only fix (Issue 4, PRD h3.3): add a clear comment in `notifier.c`
explaining why `has_been_queried` is intentionally WRITE-ONLY. No functional
change. The comment IS the deliverable.

## CRITICAL: actual current line numbers differ from the item spec
The item spec says lines **186** (decl) and **706** (assignment). The ACTUAL
current lines (verified by `grep -n has_been_queried notifier.c`) are **194** and
**714**. The shift is because P1.M1.T1.S1 (typed_awaiting_terminator watchdog,
COMPLETE) inserted code above both sites. **Anchor edits on exact text, not line
numbers.**

## The two sites (verbatim, as they exist NOW)
- **notifier.c:194 (declaration)** — in the "HOST STATE" block:
  `static bool    has_been_queried = false;   /* set on first QUERY_INFO service (§4.6 handshake-timing rule) */`
- **notifier.c:714 (assignment)** — inside `handle_typed_command`, `case
  NOTIFY_CMD_QUERY_INFO`:
  `has_been_queried = true;   /* §4.6 handshake-timing: set on first QUERY_INFO service */`

## has_been_queried is read NOWHERE (confirmed)
`grep -rn has_been_queried *.c *.h qmk_stubs/*.c` → only the decl (194) + the
assignment (714). No production read site exists. (test_notifier_host.c:13-15,
126-127 reference it in COMMENTS only, and already describe it as write-only —
supporting context; do NOT modify the test.)

## PRD §4.6 exact rationale (PRD.md:466-475) — quote it in the comment
> The firmware therefore sets a **`has_been_queried`** bool on the first
> `QUERY_INFO` it services, and the host handshakes **at most once per board
> boot** (never on a mere HID re-enumeration/reconnect), so a mid-session
> reconnect against legacy firmware cannot clear an active board layer.

So: PRD requires only that the firmware SET it; the HOST enforces
at-most-once-per-boot. A firmware read site is therefore dead state by design.

## The QUERY_INFO reply is EXACTLY 4 bytes (the wire-compat constraint)
notifier.c:715-721 (the QUERY_INFO case body):
```c
uint8_t payload[4];
payload[0] = NOTIFY_PROTO_VER;                                  /* proto_ver      */
payload[1] = NOTIFY_FEATURE_APPLY_HOST_CONTEXT | (...);         /* feature_flags  */
payload[2] = (uint8_t)get_host_callbacks_size();                /* callback_count */
payload[3] = board_rules_present() ? 1 : 0;                     /* board_rules_present */
send_typed_response(NOTIFY_CMD_QUERY_INFO, payload, 4);
```
Exposing has_been_queried here would require a 5th payload byte → changes the
wire-protocol response size → risks host compatibility. NOT recommended for a
Minor issue (item RESEARCH NOTE + findings_and_risks.md). The comment must state
this.

## Scope boundaries (no conflicts)
- P1.M2.T2.S1 (parallel): modifies `run_all_tests.sh` + creates
  `test_fidelity_nfa128.c`. Does NOT touch `notifier.c`. No overlap.
- P1.M2.T4.S1 (later): README changeset-level docs sync. This task is
  in-source (notifier.c) — different file. No overlap.
- This task: ONLY notifier.c, ONLY the two comment lines. No code change.

## The exact replacement comment text (drafted, accurate, §4.6-cited)
Declaration (194) — replace the trailing comment with a fuller explanation:
```c
/* has_been_queried — WRITE-ONLY BY DESIGN (PRD §4.6 handshake-timing rule).
 * Set to true on the first QUERY_INFO (0x01) serviced; NEVER read by any code
 * path. The PRD requires only that the firmware SET it — the HOST enforces the
 * "at most once per board boot" handshake semantics itself (§4.6), so a read
 * site here would be dead state. The flag is reserved for future firmware-side
 * observability/debugging and is intentionally NOT exposed in the QUERY_INFO
 * reply: that payload is a fixed 4 bytes ([proto_ver][feature_flags]
 * [callback_count][board_rules_present]), and adding a 5th byte would change
 * the wire-protocol response size and risk host compatibility. */
static bool    has_been_queried = false;
```
Assignment (714) — keep concise, point to the decl:
```c
has_been_queried = true;   /* WRITE-ONLY (§4.6): host enforces at-most-once-per-boot; never read here. See declaration comment. */
```

## Validation approach (comment-only change)
No code change => no test can break from logic. Validate by:
1. grep the new comment phrases present at both sites; confirm has_been_queried
   is still ONLY decl+assigned (no accidental read introduced).
2. `git diff notifier.c` shows ONLY comment changes (no functional lines touched).
3. Build still compiles (run_notifier_stub_tests.sh compiles notifier.c clean —
   a sanity check that the comment syntax is valid C).
4. Full gate still green (run_all_tests.sh + run_notifier_stub_tests.sh).

## Risk
NONE. Comment/documentation-only. No behavior, wire, build, or test change.