#ifndef AURA_MOBILE_EVENTS_H
#define AURA_MOBILE_EVENTS_H

#include <stddef.h>
#include <stdint.h>

#define AURA_PLATFORM_EVENT_ABI_VERSION 1u

/* 1-15 remain compatible with Aura's desktop event numbering. */
enum {
    AURA_POINTER_DOWN = 1,
    AURA_POINTER_MOVE = 2,
    AURA_POINTER_UP = 3,
    AURA_POINTER_CANCEL = 4,
    AURA_KEY_DOWN = 5,
    AURA_KEY_UP = 6,
    AURA_RESIZE = 7,
    AURA_SCROLL = 8,
    AURA_TEXT = 9,
    AURA_IME_INSERT = 10,
    AURA_IME_DELETE = 11,
    AURA_IME_MARK = 12,
    AURA_WINDOW_FOCUS = 13,
    AURA_FILE_DROP = 14,
    AURA_AX_ACTION = 15,
    AURA_METRICS = 16,
    AURA_LIFECYCLE = 17,
    AURA_MEMORY_PRESSURE = 18,
    AURA_SURFACE_CREATED = 19,
    AURA_SURFACE_CHANGED = 20,
    AURA_SURFACE_DESTROYED = 21,
    AURA_ENVIRONMENT_CHANGED = 22,
    AURA_SYSTEM_BACK = 23,
    AURA_PLATFORM_ROUTE = 24,
    AURA_QUEUE_OVERFLOW = 25,
    AURA_TEXT_EDIT = 26
};

enum {
    AURA_PHASE_BEGAN = 0,
    AURA_PHASE_MOVED = 1,
    AURA_PHASE_ENDED = 2,
    AURA_PHASE_CANCELLED = 3
};

enum {
    AURA_TEXT_EDIT_COMMIT = 1,
    AURA_TEXT_EDIT_SET_COMPOSING = 2,
    AURA_TEXT_EDIT_FINISH_COMPOSING = 3,
    AURA_TEXT_EDIT_SET_SELECTION = 4,
    AURA_TEXT_EDIT_DELETE_SURROUNDING = 5,
    AURA_TEXT_EDIT_SET_COMPOSING_REGION = 6,
    AURA_TEXT_EDIT_REPLACE_RANGE = 7,
    AURA_TEXT_EDIT_PERFORM_ACTION = 8
};

enum {
    AURA_TEXT_EDIT_FIELD_OPERATION = 1,
    AURA_TEXT_EDIT_FIELD_GENERATION = 2,
    AURA_TEXT_EDIT_FIELD_REVISION = 3,
    AURA_TEXT_EDIT_FIELD_FIRST = 4,
    AURA_TEXT_EDIT_FIELD_SECOND = 5
};

enum {
    AURA_BACK_PHASE_START = 1,
    AURA_BACK_PHASE_UPDATE = 2,
    AURA_BACK_PHASE_CANCEL = 3,
    AURA_BACK_PHASE_COMMIT = 4
};

enum {
    AURA_POINTER_KIND_UNKNOWN = 0,
    AURA_POINTER_KIND_TOUCH = 1,
    AURA_POINTER_KIND_MOUSE = 2,
    AURA_POINTER_KIND_STYLUS = 3,
    AURA_POINTER_KIND_TRACKPAD = 4
};

enum {
    AURA_TEXT_BORROWED = 0,
    AURA_TEXT_OWNED = 1
};

enum {
    AURA_ROUTE_APP_LINK = 1,
    AURA_ROUTE_UNIVERSAL_LINK = 2,
    AURA_ROUTE_INTENT = 3
};

/* Logical keyboard values are USB HID keyboard usages. Printable text remains
   a separate TEXT/IME event so layouts, dead keys and composition stay native. */
enum {
    AURA_KEY_A = 4,
    AURA_KEY_C = 6,
    AURA_KEY_V = 25,
    AURA_KEY_X = 27,
    AURA_KEY_Z = 29,
    AURA_KEY_ENTER = 40,
    AURA_KEY_ESCAPE = 41,
    AURA_KEY_BACKSPACE = 42,
    AURA_KEY_TAB = 43,
    AURA_KEY_SPACE = 44,
    AURA_KEY_HOME = 74,
    AURA_KEY_DELETE = 76,
    AURA_KEY_END = 77,
    AURA_KEY_RIGHT = 79,
    AURA_KEY_LEFT = 80,
    AURA_KEY_DOWN_ARROW = 81,
    AURA_KEY_UP_ARROW = 82
};

enum {
    AURA_MOD_META = 1u << 0,
    AURA_MOD_SHIFT = 1u << 1,
    AURA_MOD_CONTROL = 1u << 2,
    AURA_MOD_ALT = 1u << 3
};

/* Stable cross-platform lifecycle states. These values intentionally match
   aura/runtime/lifecycle.nox and are part of the mobile event ABI. */
enum {
    AURA_LIFECYCLE_DETACHED = 0,
    AURA_LIFECYCLE_INACTIVE = 1,
    AURA_LIFECYCLE_RESUMED = 2,
    AURA_LIFECYCLE_PAUSED = 3,
    AURA_LIFECYCLE_HIDDEN = 4,
    AURA_LIFECYCLE_TERMINATED = 5
};

enum {
    AURA_LIFECYCLE_REASON_NONE = 0,
    AURA_LIFECYCLE_REASON_SYSTEM = 1,
    AURA_LIFECYCLE_REASON_INTERRUPTION = 2,
    AURA_LIFECYCLE_REASON_USER = 3
};

enum {
    AURA_SURFACE_FORMAT_UNKNOWN = 0,
    AURA_SURFACE_FORMAT_RGBA8888 = 1,
    AURA_SURFACE_FORMAT_BGRA8888 = 2
};

enum {
    AURA_SURFACE_FIELD_ID = 1,
    AURA_SURFACE_FIELD_PHYSICAL_WIDTH = 2,
    AURA_SURFACE_FIELD_PHYSICAL_HEIGHT = 3,
    AURA_SURFACE_FIELD_FORMAT = 4,
    AURA_SURFACE_FIELD_GENERATION = 5
};

enum {
    AURA_METRIC_LOGICAL_WIDTH = 1,
    AURA_METRIC_LOGICAL_HEIGHT = 2,
    AURA_METRIC_PHYSICAL_WIDTH = 3,
    AURA_METRIC_PHYSICAL_HEIGHT = 4,
    AURA_METRIC_DEVICE_PIXEL_RATIO = 5,
    AURA_METRIC_REFRESH_RATE = 6,
    AURA_METRIC_VIEW_PADDING_LEFT = 7,
    AURA_METRIC_VIEW_PADDING_TOP = 8,
    AURA_METRIC_VIEW_PADDING_RIGHT = 9,
    AURA_METRIC_VIEW_PADDING_BOTTOM = 10,
    AURA_METRIC_VIEW_INSET_LEFT = 11,
    AURA_METRIC_VIEW_INSET_TOP = 12,
    AURA_METRIC_VIEW_INSET_RIGHT = 13,
    AURA_METRIC_VIEW_INSET_BOTTOM = 14,
    AURA_METRIC_SYSTEM_GESTURE_LEFT = 15,
    AURA_METRIC_SYSTEM_GESTURE_TOP = 16,
    AURA_METRIC_SYSTEM_GESTURE_RIGHT = 17,
    AURA_METRIC_SYSTEM_GESTURE_BOTTOM = 18,
    AURA_METRIC_ORIENTATION = 19,
    AURA_METRIC_ROTATION_DEGREES = 20,
    AURA_METRIC_FONT_SCALE = 21,
    AURA_METRIC_KEYBOARD_DURATION = 22,
    AURA_METRIC_KEYBOARD_CURVE = 23,
    AURA_METRIC_CUTOUT_COUNT = 24,
    AURA_METRIC_CUTOUT_TRUNCATED = 25
};

enum {
    AURA_CUTOUT_X = 1,
    AURA_CUTOUT_Y = 2,
    AURA_CUTOUT_WIDTH = 3,
    AURA_CUTOUT_HEIGHT = 4
};

enum {
    AURA_KEYBOARD_CURVE_UNSPECIFIED = 0,
    AURA_KEYBOARD_CURVE_LINEAR = 1,
    AURA_KEYBOARD_CURVE_EASE_IN = 2,
    AURA_KEYBOARD_CURVE_EASE_OUT = 3,
    AURA_KEYBOARD_CURVE_EASE_IN_OUT = 4
};

enum {
    AURA_ENV_BRIGHTNESS = 1,
    AURA_ENV_HIGH_CONTRAST = 2,
    AURA_ENV_REDUCE_MOTION = 3,
    AURA_ENV_ACCESSIBLE_NAVIGATION = 4,
    AURA_ENV_TEXT_DIRECTION = 5,
    AURA_ENV_BOLD_TEXT = 6,
    AURA_ENV_FONT_SCALE = 7
};

/* Memory pressure is a host signal, not a cache policy. Critical is the only
   level iOS publishes. Android trim levels collapse onto this scale. */
enum {
    AURA_MEMORY_PRESSURE_NONE = 0,
    AURA_MEMORY_PRESSURE_LOW = 1,
    AURA_MEMORY_PRESSURE_MODERATE = 2,
    AURA_MEMORY_PRESSURE_CRITICAL = 3
};

#define AURA_MAX_DISPLAY_CUTOUTS 8

typedef struct AuraPlatformInsets {
    double left;
    double top;
    double right;
    double bottom;
} AuraPlatformInsets;

typedef struct AuraRect {
    double x;
    double y;
    double width;
    double height;
} AuraRect;

typedef struct AuraMetricsPayload {
    double logical_width;
    double logical_height;
    int64_t physical_width;
    int64_t physical_height;
    double device_pixel_ratio;
    double refresh_rate;
    AuraPlatformInsets view_padding;
    AuraPlatformInsets view_insets;
    AuraPlatformInsets system_gesture_insets;
    int32_t orientation;
    int32_t rotation_degrees;
    double font_scale;
    double keyboard_duration_s;
    int32_t keyboard_curve;
    int32_t cutout_count;
    int32_t cutout_truncated;
    AuraRect cutouts[AURA_MAX_DISPLAY_CUTOUTS];
} AuraMetricsPayload;

typedef struct AuraLifecyclePayload {
    int32_t state;
    int32_t reason;
} AuraLifecyclePayload;

typedef struct AuraMemoryPressurePayload {
    int32_t level;
    int32_t reserved;
} AuraMemoryPressurePayload;

typedef struct AuraSurfacePayload {
    int64_t surface_id;
    int64_t physical_width;
    int64_t physical_height;
    int32_t format;
    int32_t generation;
} AuraSurfacePayload;

typedef struct AuraEnvironmentPayload {
    int32_t brightness;
    int32_t high_contrast;
    int32_t reduce_motion;
    int32_t accessible_navigation;
    int32_t text_direction;
    int32_t bold_text;
    double font_scale;
} AuraEnvironmentPayload;

typedef struct AuraQueueOverflowPayload {
    uint64_t dropped_count;
    uint64_t overflow_count;
    uint32_t capacity;
    uint32_t high_water_mark;
} AuraQueueOverflowPayload;

typedef struct AuraTextEditingPayload {
    int64_t generation;
    int64_t revision;
    int64_t first;
    int64_t second;
    int32_t operation;
    int32_t reserved;
} AuraTextEditingPayload;

typedef union AuraPlatformPayload {
    AuraMetricsPayload metrics;
    AuraLifecyclePayload lifecycle;
    AuraMemoryPressurePayload memory_pressure;
    AuraSurfacePayload surface;
    AuraEnvironmentPayload environment;
    AuraQueueOverflowPayload queue_overflow;
    AuraTextEditingPayload text_editing;
} AuraPlatformPayload;

typedef struct AuraPlatformEvent {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t kind;
    uint32_t flags;
    uint64_t sequence;
    uint64_t timestamp_ns;
    int64_t device_id;
    int64_t pointer_id;
    uint32_t pointer_kind;
    uint32_t buttons;
    uint32_t modifiers;
    uint32_t scroll_phase;
    double x;
    double y;
    double delta_x;
    double delta_y;
    double pressure;
    double tilt_x;
    double tilt_y;
    int64_t key_code;
    const char *text;
    uint32_t text_length;
    uint32_t text_ownership;
    AuraPlatformPayload payload;
} AuraPlatformEvent;

/* Compatibility name used by the current iOS and Android preview adapters. */
typedef AuraPlatformEvent AuraMobileEvent;

void aura_platform_event_init(AuraPlatformEvent *out, uint32_t kind, uint64_t sequence, uint64_t timestamp_ns);

int aura_mobile_key_v1(
    AuraPlatformEvent *out,
    int is_down,
    int64_t logical_key,
    uint32_t modifiers,
    uint64_t sequence,
    uint64_t timestamp_ns,
    int64_t device_id
);

/* Coordinates are normalized to logical surface pixels. Safe-area is layout
   padding in WindowMetrics and is deliberately not subtracted here. */
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
);

/* Transitional preview API. New adapters must call aura_mobile_pointer_v1
   with the platform callback timestamp and their per-scene sequence. */
int aura_mobile_pointer(AuraMobileEvent *out, int phase, double pixel_x, double pixel_y, double scale, double safe_left, double safe_top);

#endif
