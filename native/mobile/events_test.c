#include "events.h"

#include <stddef.h>
#include <stdint.h>

int main(void) {
    AuraMobileEvent event;
    AuraMobileEvent next;

    if (!aura_mobile_pointer(&event, AURA_PHASE_BEGAN, 100.0, 20.0, 2.0, 10.0, 10.0)) return 1;
    if (event.struct_size != sizeof(AuraPlatformEvent)) return 2;
    if (event.abi_version != AURA_PLATFORM_EVENT_ABI_VERSION) return 3;
    if (event.kind != AURA_POINTER_DOWN) return 4;
    if (event.x != 50.0 || event.y != 10.0) return 5;
    if (event.pointer_kind != AURA_POINTER_KIND_TOUCH) return 6;
    if (event.sequence == 0 || event.timestamp_ns == 0) return 7;

    if (aura_mobile_pointer(&next, AURA_PHASE_MOVED, 0.0, 0.0, 0.0, 0.0, 0.0)) return 8;
    if (!aura_mobile_pointer(&next, AURA_PHASE_CANCELLED, 4.0, 6.0, 2.0, 0.0, 0.0)) return 9;
    if (next.kind != AURA_POINTER_CANCEL) return 10;
    if (next.sequence <= event.sequence) return 11;

    if (!aura_mobile_pointer_v1(
            &event,
            AURA_PHASE_MOVED,
            30.0,
            50.0,
            2.0,
            77,
            123456,
            9,
            12,
            AURA_POINTER_KIND_STYLUS,
            3,
            5,
            0.75,
            0.1,
            -0.2)) return 12;
    if (event.kind != AURA_POINTER_MOVE) return 13;
    if (event.sequence != 77 || event.timestamp_ns != 123456) return 14;
    if (event.device_id != 9 || event.pointer_id != 12) return 15;
    if (event.pointer_kind != AURA_POINTER_KIND_STYLUS) return 16;
    if (event.buttons != 3 || event.modifiers != 5) return 17;
    if (event.x != 15.0 || event.y != 25.0) return 18;
    if (event.pressure != 0.75 || event.tilt_x != 0.1 || event.tilt_y != -0.2) return 19;

    if (!aura_mobile_key_v1(&event, 1, AURA_KEY_TAB, AURA_MOD_SHIFT, 9, 10, 11)) return 20;
    if (event.kind != AURA_KEY_DOWN || event.key_code != AURA_KEY_TAB) return 21;
    if (event.modifiers != AURA_MOD_SHIFT || event.device_id != 11) return 22;
    if (aura_mobile_key_v1(&event, 0, 0, 0, 0, 0, 0)) return 23;

    aura_platform_event_init(&event, AURA_METRICS, 78, 123457);
    event.payload.metrics.logical_width = 390.0;
    event.payload.metrics.view_padding.top = 47.0;
    if (event.kind != AURA_METRICS) return 24;
    if (event.payload.metrics.logical_width != 390.0) return 25;
    if (event.payload.metrics.view_padding.top != 47.0) return 26;

    aura_platform_event_init(&event, AURA_LIFECYCLE, 79, 123458);
    event.payload.lifecycle.state = 3;
    if (event.payload.lifecycle.state != 3) return 27;

    return 0;
}
