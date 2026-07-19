#!/usr/bin/env bash
# P2 stub-compile validation gate for notifier.c (closes RISK-1).
#
# notifier.c cannot compile standalone: it does `#include QMK_KEYBOARD_H` (a
# -D-expanded header name) and pulls in QMK symbols (layer_on/layer_off,
# raw_hid_send) that the 9-suite corpus — which links only pattern_match.c —
# cannot provide. This harness substitutes minimal QMK stubs so the receiver /
# reassembler / F4 delimiter matcher / dispatcher ordering / hid_notify ack
# logic AND the multi-OS map-selection (F8) / OS-change-clear (F9) logic can be
# validated with plain gcc on a host. It builds BOTH the dispatch test
# (test_notifier_dispatch) and the multi-OS test (test_notifier_os) from a
# SINGLE stub-compiled notifier.o (PRD §11.1, §11.2D). See PRP P2 / P1.M2.T2.
set -u
cd "$(dirname "$0")"

OBJ=/tmp/notifier_stub.o
DRV=/tmp/test_notifier_dispatch
OST=/tmp/test_notifier_os

echo "[1/4] stub-compile notifier.c (shared by both test binaries) ..."
gcc -Wall -Wextra -std=c99 \
    -DQMK_KEYBOARD_H='"qmk_keyboard_stub.h"' \
    -Iqmk_stubs -I. \
    -c notifier.c -o "$OBJ"
if [ $? -ne 0 ]; then echo "COMPILE FAILED"; exit 2; fi

echo "[2/4] link dispatch driver (test_notifier_dispatch) ..."
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    "$OBJ" qmk_stubs/qmk_stubs.c test_notifier_dispatch.c \
    -o "$DRV"
if [ $? -ne 0 ]; then echo "LINK FAILED (dispatch)"; rm -f "$OBJ"; exit 3; fi

echo "[3/4] link multi-OS driver (test_notifier_os) ..."
gcc -Wall -std=c99 -Iqmk_stubs -I. \
    "$OBJ" qmk_stubs/qmk_stubs.c test_notifier_os.c \
    -o "$OST"
if [ $? -ne 0 ]; then echo "LINK FAILED (os)"; rm -f "$OBJ" "$DRV"; exit 4; fi

echo "[4/4] run both ..."
"$DRV"
rc_d=$?
fails_d=$("$DRV" 2>/dev/null | grep -c '^FAIL:' || true)
"$OST"
rc_o=$?
fails_o=$("$OST" 2>/dev/null | grep -c '^FAIL:' || true)
echo "------------------------------------------------"
echo "notifier dispatch fails=$fails_d  (exit=$rc_d)"
echo "notifier os fails=$fails_o  (exit=$rc_o)"
rm -f "$OBJ" "$DRV" "$OST"
if [ "$fails_d" -eq 0 ] && [ $rc_d -eq 0 ] && [ "$fails_o" -eq 0 ] && [ $rc_o -eq 0 ]; then
    echo "✓ notifier stub-compile gate PASSED"
    exit 0
fi
echo "✗ notifier stub-compile gate FAILED"
exit 1
