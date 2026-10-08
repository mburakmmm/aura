#include <stdint.h>

static int64_t g_open = 1;

int64_t aura_plugin_probe_frame(int64_t width, int64_t height) {
    if (!g_open) {
        return -1;
    }
    return width + height;
}

int64_t aura_plugin_probe_event(int64_t kind, double x, double y) {
    (void)x;
    (void)y;
    if (!g_open) {
        return -1;
    }
    return kind;
}

int64_t aura_plugin_probe_close(void) {
    g_open = 0;
    return 0;
}
