#!/usr/bin/env bash
# =============================================================================
# validate.sh — Comprehensive validation for the qmk_notifier firmware module.
#
# This project is a QMK firmware C module (not an app). Its "tests" are:
#   - A 10-suite host pattern-matcher corpus (gcc, links pattern_match.c)
#   - A 3-binary notifier stub gate (stub-compiles notifier.c, tests the
#     receiver/reassembler/dispatcher/OS-selection/host-rules logic)
#   - A real QMK Community-Module build (if the qmk CLI + firmware are present)
#
# The script is READ-ONLY with respect to the repo: it rebuilds the test
# binaries into a temp dir and runs them, performs the PRD §11.2 acceptance
# micro-tests, and validates the Community Module distribution surface
# (manifest presence/validity + README consistency + QMK discovery E2E).
# It writes nothing into the repo working tree.
#
# Phases:
#   1. Build hygiene            (strict -Wall -Wextra compiles, no warnings)
#   2. Pattern-matcher corpus   (run_all_tests.sh — 10 suites, ~2029 assertions)
#   3. Notifier stub gate       (run_notifier_stub_tests.sh — dispatch/os/host)
#   4. PRD §11.2 acceptance     (pathological NFA < 50ms; 6 realistic = 1; NULL-safe)
#   5. Community-Module E2E     (manifest presence + schema + QMK discovery + build)
#   6. Doc/contract consistency (README stale refs; wire-protocol constants)
#
# Exit non-zero if ANY phase fails. Each phase prints PASS/FAIL/WARN summaries.
# =============================================================================
set -u
cd "$(dirname "$0")"
REPO="$(pwd)"
FAILS=0
WARNS=0
TMP="$(mktemp -d /tmp/qmk_notifier_validate.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

mark_fail() { echo "  ✗ FAIL: $*"; FAILS=$((FAILS+1)); }
mark_pass() { echo "  ✓ PASS: $*"; }
mark_warn() { echo "  ⚠ WARN: $*"; WARNS=$((WARNS+1)); }
section()   { printf '\n========== %s ==========\n' "$*"; }

have() { command -v "$1" >/dev/null 2>&1; }

# Detect the QMK toolchain (optional — enables the Community-Module E2E phase)
QMK_FW=""
if have qmk; then
  for c in "$HOME/projects/qmk_firmware" "$HOME/qmk_firmware"; do
    if [ -d "$c/lib/python/qmk" ]; then QMK_FW="$c"; break; fi
  done
fi

echo "Repo:        $REPO"
echo "Temp build:  $TMP"
echo "qmk CLI:     $(have qmk && qmk --version 2>/dev/null | head -1 || echo 'not found (Community-Module E2E phase will be limited)')"
echo "qmk_firmware: ${QMK_FW:-(not found — Community-Module E2E phase will be limited)}"

# =============================================================================
section "PHASE 1 — Build hygiene (strict -Wall -Wextra, zero warnings expected)"
# =============================================================================
# pattern_match.c must compile clean both as a standalone TU and (implicitly)
# when #included by notifier.c. notifier.c is stub-compiled against qmk_stubs/.
if gcc -Wall -Wextra -std=c99 -c pattern_match.c -o "$TMP/pm.o" 2>"$TMP/pm.warn"; then
  if [ -s "$TMP/pm.warn" ]; then mark_fail "pattern_match.c emitted warnings"; cat "$TMP/pm.warn"; else mark_pass "pattern_match.c compiles clean (-Wall -Wextra)"; fi
else mark_fail "pattern_match.c failed to compile"; cat "$TMP/pm.warn"; fi

if gcc -Wall -Wextra -std=c99 -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' -Iqmk_stubs -I. \
      -c notifier.c -o "$TMP/notifier.o" 2>"$TMP/notifier.warn"; then
  if [ -s "$TMP/notifier.warn" ]; then mark_fail "notifier.c (stub) emitted warnings"; cat "$TMP/notifier.warn"; else mark_pass "notifier.c stub-compiles clean (-Wall -Wextra)"; fi
else mark_fail "notifier.c (stub) failed to compile"; cat "$TMP/notifier.warn"; fi

# =============================================================================
section "PHASE 2 — Pattern-matcher corpus (run_all_tests.sh — 10 suites)"
# =============================================================================
# This is the project's own test runner. It rebuilds all 10 suites, runs them,
# and aggregates a Total/Passed/Failed summary plus a performance micro-bench.
if ./run_all_tests.sh >"$TMP/all.out" 2>&1; then
  agg=$(grep "Total tests failed:" "$TMP/all.out" | grep -oE "[0-9]+")
  total=$(grep "Total tests run across" "$TMP/all.out" | grep -oE "[0-9]+")
  if [ "${agg:-1}" -eq 0 ]; then mark_pass "all 10 pattern_match suites green ($total assertions, 0 failures)";
  else mark_fail "run_all_tests.sh reported $agg failures"; fi
else mark_fail "run_all_tests.sh exited non-zero"; tail -25 "$TMP/all.out"; fi

# =============================================================================
section "PHASE 3 — Notifier stub gate (run_notifier_stub_tests.sh)"
# =============================================================================
# Stub-compiles notifier.c once into 3 host binaries: dispatch (reassembly /
# F4 delimiter / ordering / ack / NULL-safety), os (multi-OS selection F8/F9),
# and host (typed-command namespace §4.6/§4.7/§14). This is the gate for the
# receiver/dispatcher/host-rules logic that the pattern_match corpus cannot reach.
if ./run_notifier_stub_tests.sh >"$TMP/stub.out" 2>&1; then
  if grep -q "✓ notifier stub-compile gate PASSED" "$TMP/stub.out"; then
    mark_pass "notifier stub-compile gate PASSED (dispatch + os + host all 0 FAIL:)"
  else mark_fail "notifier stub gate did not report PASSED"; tail -30 "$TMP/stub.out"; fi
else mark_fail "run_notifier_stub_tests.sh exited non-zero"; tail -30 "$TMP/stub.out"; fi

# =============================================================================
section "PHASE 4 — PRD §11.2 acceptance micro-tests"
# =============================================================================
# (B) The pathological Thompson-NFA case that used to hang the backtracker must
#     finish in < 50 ms and return 0 (no match).
cat > "$TMP/nfa_stress.c" <<'EOF'
#include <stdio.h>
#include <time.h>
#include "pattern_match.h"
int main(void){
  char s[200]; for(int i=0;i<199;i++) s[i]='a'; s[199]='\0';
  clock_t t=clock(); int r=pattern_match("a+a+a+a+a+a+a+a+a+a+b",s,1);
  double us=1e6*(double)(clock()-t)/CLOCKS_PER_SEC;
  printf("%d %.1f\n", r, us);
  return 0;
}
EOF
if gcc -O2 -w "$TMP/nfa_stress.c" pattern_match.c -I. -o "$TMP/nfa_stress" 2>/dev/null; then
  read -r res us <<<"$(timeout 5 "$TMP/nfa_stress" 2>/dev/null)"
  if [ "${res:-X}" = "0" ] && awk "BEGIN{exit !($us < 50000)}"; then
    mark_pass "pathological NFA stress: result=$res in ${us}µs (< 50ms gate)"
  else mark_fail "pathological NFA stress: result=${res:-timeout} time=${us:-?}µs (need result=0 <50000µs)"; fi
else mark_fail "could not build nfa_stress"; fi

# (C) Six realistic patterns must all match (print 1).
cat > "$TMP/nfa_real.c" <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  printf("%d\n", pattern_match("\\w+","hello",1));
  printf("%d\n", pattern_match("\\b\\w+\\b\\s+\\b\\w+\\b","hello world",1));
  printf("%d\n", pattern_match("^\\w+@\\w+$","user@host",1));
  printf("%d\n", pattern_match("v\\.code","v.code",1));
  printf("%d\n", pattern_match("a+b","aaab",1));
  printf("%d\n", pattern_match("*slack*","Slack - general",0));
  return 0;
}
EOF
if gcc -w "$TMP/nfa_real.c" pattern_match.c -I. -o "$TMP/nfa_real" 2>/dev/null; then
  out=$("$TMP/nfa_real" 2>/dev/null | tr '\n' ',')
  if [ "$out" = "1,1,1,1,1,1," ]; then mark_pass "six realistic patterns all match ($out)";
  else mark_fail "realistic patterns mismatched: '$out' (expected 1,1,1,1,1,1,)"; fi
else mark_fail "could not build nfa_real"; fi

# Robustness: NULL/garbage must NOT crash and must return false (PRD §12 / F3).
cat > "$TMP/robust.c" <<'EOF'
#include <stdio.h>
#include "pattern_match.h"
int main(void){
  int ok=1;
  ok &= (pattern_match(NULL,NULL,1)==0);
  ok &= (pattern_match("abc",NULL,1)==0);
  ok &= (pattern_match(NULL,"abc",1)==0);
  printf("%d\n", ok);
  return 0;
}
EOF
if gcc -w "$TMP/robust.c" pattern_match.c -I. -o "$TMP/robust" 2>/dev/null; then
  r=$("$TMP/robust" 2>/dev/null)
  if [ "$r" = "1" ]; then mark_pass "NULL inputs all return false, no crash";
  else mark_fail "NULL robustness check returned $r (expected 1)"; fi
else mark_fail "could not build robustness probe"; fi

# =============================================================================
section "PHASE 5 — Community-Module distribution E2E (PRD §18 R1–R5)"
# =============================================================================
# R1: qmk_module.json manifest must be present at the repo root. QMK's module
#     discovery globs for this exact filename; without it the documented
#     "one keymap.json modules entry" install flow fails with
#     FileNotFoundError at build time.
if [ -f qmk_module.json ]; then mark_pass "qmk_module.json present at repo root (R1)"
else mark_fail "qmk_module.json MISSING at repo root (PRD §18.3 R1) — documented Community Module install flow is broken"; fi

# R1 schema validity + the 'keycodes' pitfall: the schema requires keycodes to
# have minItems:1, so "keycodes":[] is INVALID. The correct form OMITS keycodes.
if [ -f qmk_module.json ]; then
  if python3 -c "import json,sys; d=json.load(open('qmk_module.json')); sys.exit(0 if d.get('module_name') and d.get('maintainer') else 1)"; then
    mark_pass "qmk_module.json has required module_name + maintainer"
  else mark_fail "qmk_module.json missing required module_name/maintainer"; fi
  if grep -q '"keycodes"' qmk_module.json; then
    mark_warn "qmk_module.json includes a 'keycodes' field — empty [] is schema-INVALID (minItems:1); omit it instead"
  fi
fi

# R2: rules.mk must be the module-context form (SRC += notifier.c), not the old
#     submodule form (SRC += qmk_notifier/notifier.c).
if grep -qE '^SRC \+= notifier\.c$' rules.mk && grep -qE '^RAW_ENABLE = yes' rules.mk; then
  mark_pass "rules.mk is module-context (R2: SRC += notifier.c; RAW_ENABLE = yes)"
else mark_fail "rules.mk is not the module-context form (R2) — got:"; cat rules.mk; fi
if grep -qE 'SRC \+= qmk_notifier/notifier\.c' rules.mk; then
  mark_fail "rules.mk still has the old submodule SRC path"
fi

# R3: notifier.c must carry the #ifdef-gated API-version assertion after the
#     QMK include. (In stub builds neither symbol is defined, so the guard is a
#     no-op — verified by Phase 3 passing.)
if grep -q 'COMMUNITY_MODULES_API_VERSION' notifier.c && grep -q 'ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0)' notifier.c; then
  mark_pass "notifier.c carries the #ifdef-gated Community Module API-version guard (R3)"
else mark_fail "notifier.c missing the R3 API-version assertion guard"; fi

# If the full QMK toolchain is available, validate the manifest against the real
# schema and exercise end-to-end module discovery (the actual user journey).
if [ -n "$QMK_FW" ]; then
  SCHEMA="$QMK_FW/data/schemas/community_module.jsonschema"
  if [ -f qmk_module.json ] && [ -f "$SCHEMA" ]; then
    # Validate the manifest against QMK's real community_module.v1 schema.
    # Also catches the 'keycodes':[] pitfall (minItems:1 => must omit keycodes).
    val=$(cd "$QMK_FW"; python3 - "$REPO" <<'PY' 2>/dev/null
import json, sys
sys.path.insert(0,'lib/python')
try:
    from qmk.json_schema import validate
    d=json.load(open(sys.argv[1]+'/qmk_module.json'))
    validate(d,'qmk.community_module.v1'); print("VALID")
except Exception as e:
    print("INVALID:",type(e).__name__,"-",e)
PY
    )
    if [ "${val:0:5}" = "VALID" ]; then mark_pass "qmk_module.json validates against the real qmk.community_module.v1 schema"
    else mark_fail "qmk_module.json fails schema validation: $val"; fi
  fi

  # E2E: does QMK actually DISCOVER the module from a userspace? This is the
  # exact code path a user hits when they follow the README install flow.
  U="$(mktemp -d "$TMP/uspace.XXXXXX")"
  mkdir -p "$U/modules/dabstractor"
  printf '{"userspace_version":"1.1","build_targets":[]}' > "$U/qmk.json"
  # Real dir copy (rglob does not always descend into symlinked module dirs in
  # the test harness); in production a git submodule clone is a real dir.
  cp -r "$REPO" "$U/modules/dabstractor/qmk_notifier"
  res=$(cd "$QMK_FW"; ORIG_CWD="$U" SKIP_SCHEMA_VALIDATION=1 python3 - "$U" <<'PY' 2>/dev/null
import sys, os
u=sys.argv[1]; sys.path.insert(0,'lib/python')
try:
    from qmk.community_modules import find_module_path
    p=find_module_path('dabstractor/qmk_notifier')
    print("DISCOVERED" if p else "NOTFOUND")
except FileNotFoundError:
    print("NOTFOUND")
except Exception as e:
    print("ERR",type(e).__name__,e)
PY
)
  if [ "$res" = "DISCOVERED" ]; then mark_pass "QMK module discovery finds qmk_notifier from a userspace (README install flow works)"
  elif [ "$res" = "NOTFOUND" ]; then
    if [ -f qmk_module.json ]; then mark_fail "QMK module discovery did NOT find qmk_notifier (investigate userspace/module layout)"
    else mark_fail "QMK module discovery did NOT find qmk_notifier — because qmk_module.json is absent (the R1 blocker)"; fi
  else mark_warn "QMK module-discovery probe inconclusive ($res)"; fi
else
  mark_warn "qmk CLI / qmk_firmware not available — skipped real-schema + module-discovery E2E (run on a host with QMK installed for full confidence)"
fi

# =============================================================================
section "PHASE 6 — Documentation & wire-protocol contract consistency"
# =============================================================================
# README: the Community-Module Setup section uses '#include "notifier.h"' (VPATH).
# The Multi-OS section must NOT still show the old submodule relative include,
# which is a hard build failure under the module flow.
if grep -q 'include "\./qmk_notifier/notifier\.h"' README.md; then
  mark_fail "README still references the old submodule relative include '#include \"./qmk_notifier/notifier.h\"' (hard build failure under the Community Module flow — R5/P1.M2.T3)"
else mark_pass "README has no stale './qmk_notifier/notifier.h' relative include"
fi

# Wire-protocol constants are fixed forever (PRD §13 invariants 1–3, §4, §16).
check_const() { # file pattern label
  if grep -q "$2" "$1"; then mark_pass "$3"; else mark_fail "$3 (missing '$2' in $1)"; fi
}
grep -q '0x81' notifier.c && grep -q '0x9F' notifier.c && mark_pass "magic header 0x81 0x9F present (coexistence guard)" || mark_fail "magic header 0x81 0x9F missing"
check_const notifier.h '"\\x1D"' "GS_DELIMITER = 0x1D (class|title delimiter)"
check_const notifier.h '"\\x03"' "ETX_TERMINATOR = 0x03 (message terminator)"
check_const notifier.c 'define RAW_REPORT_SIZE 32' "RAW_REPORT_SIZE = 32 (logical HID report, all QMK protocols)"
check_const notifier.c 'define MSG_BUFFER_SIZE 256' "MSG_BUFFER_SIZE = 256 (reassembly buffer)"
check_const notifier.c 'define LAYER_UNSET 255' "LAYER_UNSET = 255 (no-layer sentinel)"
check_const notifier.h 'define NOTIFY_CMD_DISCRIMINATOR      0xF0' "typed-command discriminator 0xF0 (§4.6)"
check_const notifier.h 'define NOTIFY_RESPONSE_MARKER        0x51' "typed-response marker 0x51 (§4.6)"

# LICENSE: a GPL license is claimed by the planned manifest but no LICENSE file
# exists in the repo. (The manifest is currently absent; flagged here so the
# implementer does not ship a license claim without license text.)
if ls LICENSE* COPYING* >/dev/null 2>&1; then mark_pass "LICENSE/COPYING file present"
else mark_warn "No LICENSE/COPYING file in repo — the planned manifest claims GPL-2.0-or-later without license text (legal/distribution gap)"; fi

# =============================================================================
section "VALIDATION SUMMARY"
# =============================================================================
echo "Failures: $FAILS"
echo "Warnings: $WARNS"
if [ "$FAILS" -eq 0 ]; then
  echo "✓ ALL HARD CHECKS PASSED"
  [ "$WARNS" -gt 0 ] && echo "  ($WARNS warning(s) — review above; warnings do not fail the gate)"
  exit 0
else
  echo "✗ $FAILS HARD CHECK(S) FAILED — see details above"
  exit 1
fi