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
