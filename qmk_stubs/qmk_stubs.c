#include <stdint.h>
#include <stdio.h>

/* Observable QMK symbol implementations for the host stub harness.
 * In production these are provided by QMK (action_layer.c, raw_hid.c). */
static uint8_t g_active_layer = 255;

void layer_on(uint8_t layer) {
    g_active_layer = layer;
    fprintf(stderr, "[stub] layer_on(%u) -> active=%u\n", layer, g_active_layer);
}
void layer_off(uint8_t layer) {
    (void)layer;
    g_active_layer = 255;
    fprintf(stderr, "[stub] layer_off -> active=255\n");
}
void raw_hid_send(uint8_t *data, uint8_t length) {
    (void)length;
    fprintf(stderr, "[stub] raw_hid_send response[0]=%u\n", data[0]);
}

/* Test-harness observable (NOT production code): exposes the file-static
 * g_active_layer so host tests (test_notifier_os.c) can assert WHICH layer
 * won. In a real QMK build layer_state provides this; the stub tracks it
 * locally. Added for the multi-OS test harness (findings F6). */
uint8_t stub_get_active_layer(void) { return g_active_layer; }
