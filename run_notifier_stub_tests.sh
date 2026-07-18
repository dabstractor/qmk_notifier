#!/usr/bin/env bash
# P2 stub-compile validation gate for notifier.c (closes RISK-1).
#
# notifier.c cannot compile standalone: it does `#include QMK_KEYBOARD_H` (a
# -D-expanded header name) and pulls in QMK symbols (layer_on/layer_off,
# raw_hid_send) that the 9-suite corpus — which links only pattern_match.c —
# cannot provide. This harness substitutes minimal QMK stubs so the receiver /
# reassembler / F4 delimiter matcher / dispatcher ordering / hid_notify ack
# logic can be validated with plain gcc on a host. See PRP P2.
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
