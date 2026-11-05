#include "events.h"

#include <stdatomic.h>
#include <string.h>
#include <time.h>

static _Atomic uint64_t g_compat_sequence = 0;

static uint64_t monotonic_now_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0;
    }
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

void aura_platform_event_init(AuraPlatformEvent *out, uint32_t kind, uint64_t sequence, uint64_t timestamp_ns) {
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->struct_size = (uint32_t)sizeof(*out);
    out->abi_version = AURA_PLATFORM_EVENT_ABI_VERSION;
    out->kind = kind;
    out->sequence = sequence;
    out->timestamp_ns = timestamp_ns;
}

int aura_mobile_key_v1(
    AuraPlatformEvent *out,
    int is_down,
    int64_t logical_key,
    uint32_t modifiers,
    uint64_t sequence,
    uint64_t timestamp_ns,
    int64_t device_id
) {
    if (out == NULL || logical_key <= 0) {
        return 0;
    }
    aura_platform_event_init(out, is_down ? AURA_KEY_DOWN : AURA_KEY_UP, sequence, timestamp_ns);
    out->device_id = device_id;
    out->key_code = logical_key;
    out->modifiers = modifiers;
    return 1;
}

int aura_mobile_pointer_v1(
    AuraPlatformEvent *out,
    int phase,
    double pixel_x,
    double pixel_y,
    double scale,
    uint64_t sequence,
    uint64_t timestamp_ns,
    int64_t device_id,
    int64_t pointer_id,
    uint32_t pointer_kind,
    uint32_t buttons,
    uint32_t modifiers,
    double pressure,
    double tilt_x,
    double tilt_y
) {
    uint32_t kind = 0;
    if (out == NULL || !(scale > 0.0)) {
        return 0;
    }
    switch (phase) {
    case AURA_PHASE_BEGAN:
        kind = AURA_POINTER_DOWN;
        break;
    case AURA_PHASE_MOVED:
        kind = AURA_POINTER_MOVE;
        break;
    case AURA_PHASE_ENDED:
        kind = AURA_POINTER_UP;
        break;
    case AURA_PHASE_CANCELLED:
        kind = AURA_POINTER_CANCEL;
        break;
    default:
        return 0;
    }
    aura_platform_event_init(out, kind, sequence, timestamp_ns);
    out->device_id = device_id;
    out->pointer_id = pointer_id;
    out->pointer_kind = pointer_kind;
    out->buttons = buttons;
    out->modifiers = modifiers;
    out->pressure = pressure;
    out->tilt_x = tilt_x;
    out->tilt_y = tilt_y;
    out->x = pixel_x / scale;
    out->y = pixel_y / scale;
    return 1;
}

int aura_mobile_pointer(AuraMobileEvent *out, int phase, double pixel_x, double pixel_y, double scale, double safe_left, double safe_top) {
    uint64_t sequence = atomic_fetch_add_explicit(&g_compat_sequence, 1, memory_order_relaxed) + 1;
    (void)safe_left;
    (void)safe_top;
    return aura_mobile_pointer_v1(
        out,
        phase,
        pixel_x,
        pixel_y,
        scale,
        sequence,
        monotonic_now_ns(),
        0,
        0,
        AURA_POINTER_KIND_TOUCH,
        1,
        0,
        0.0,
        0.0,
        0.0
    );
}
