#include "shell.h"

#include "event_queue.h"
#include "events.h"
#include "paint.h"

#include <pthread.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AURA_MAX_WINDOWS 4
#define AURA_PENDING_ROUTE_CAPACITY 8
#define AURA_TEXT_BOUNDARY_LIMIT 1048576

typedef struct AuraTextBoundary {
    int64_t offset;
    double x;
    double y;
    double height;
} AuraTextBoundary;

typedef struct AuraAx {
    char *role;
    char *label;
    char *value;
    int checked;
    int heading;
    int64_t node_id;
    int64_t press;
    double x;
    double y;
    double w;
    double h;
} AuraAx;

typedef struct AuraWindow {
    int alive;
    int shown;
    int close_requested;
    int cursor_kind;
    int text_focus;
    double width;
    double height;
    double scale;
    char *title;
    char *text_contents;
    int64_t text_generation;
    int64_t text_revision;
    int64_t text_selection_start;
    int64_t text_selection_end;
    int64_t text_composing_start;
    int64_t text_composing_end;
    int64_t text_platform_revision;
    double text_caret_x;
    double text_caret_y;
    double text_caret_width;
    double text_caret_height;
    double text_editable_x;
    double text_editable_y;
    double text_editable_width;
    double text_editable_height;
    AuraTextBoundary *text_boundaries;
    int text_boundary_count;
    int64_t text_boundary_target;
    int64_t text_boundary_revision;
    AuraTextBoundary *text_boundaries_pending;
    int text_boundary_pending_count;
    int text_boundary_pending_written;
    int64_t text_boundary_pending_target;
    int64_t text_boundary_pending_revision;
    int text_input_type;
    int text_action;
    int text_multiline;
    int text_secure;
    int text_autocorrect;
    int text_capitalization;
    char *text_autofill_hints;
    AuraEventQueue events;
    int events_ready;
    uint64_t next_event_sequence;
    AuraAx *ax;
    int ax_count;
    int ax_cap;
    int ax_open;
    int ax_published;
    int64_t *ax_prev_ids;
    int ax_prev_count;
    uint8_t *pixels;
    int pixel_w;
    int pixel_h;
    int pixel_stride;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int frame_ready;
    double frame_time;
    int sync_ready;
    int metrics_set;
    int surface_ready;
    AuraSurfacePayload surface;
    uint64_t surface_timestamp_ns;
    char *pending_routes[AURA_PENDING_ROUTE_CAPACITY];
    int pending_route_sources[AURA_PENDING_ROUTE_CAPACITY];
    uint64_t pending_route_timestamps[AURA_PENDING_ROUTE_CAPACITY];
    int pending_route_count;
    uint64_t pending_route_dropped;
} AuraWindow;

static AuraWindow g_windows[AURA_MAX_WINDOWS];
static pthread_mutex_t g_table = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_table_once = PTHREAD_ONCE_INIT;
static char *g_clip = NULL;
static size_t g_clip_length = 0;
static char *g_ax_query = NULL;

static void initialize_table(void) {
    int i = 0;
    for (i = 0; i < AURA_MAX_WINDOWS; i++) {
        pthread_mutex_init(&g_windows[i].mu, NULL);
        pthread_cond_init(&g_windows[i].cv, NULL);
        g_windows[i].events_ready = aura_event_queue_init(&g_windows[i].events, AURA_EVENT_QUEUE_DEFAULT_CAPACITY);
        g_windows[i].scale = 1.0;
        g_windows[i].sync_ready = 1;
    }
}

static void ensure_table(void) {
    pthread_once(&g_table_once, initialize_table);
}

static AuraWindow *window_get(int64_t handle, int require_alive) {
    AuraWindow *window = NULL;
    ensure_table();
    if (handle < 1 || handle > AURA_MAX_WINDOWS) {
        return NULL;
    }
    window = &g_windows[handle - 1];
    if (require_alive && !window->alive) {
        return NULL;
    }
    return window;
}

static char *dup_text(const char *text) {
    size_t length = 0;
    char *copy = NULL;
    if (text == NULL) {
        text = "";
    }
    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

static char *dup_bytes(const char *text, size_t length) {
    char *copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    if (text != NULL && length > 0) {
        memcpy(copy, text, length);
    }
    copy[length] = 0;
    return copy;
}

static void reset_events(AuraWindow *window) {
    if (window->events_ready) {
        aura_event_queue_reset(&window->events);
    }
}

static void clear_ax(AuraWindow *window) {
    int i = 0;
    for (i = 0; i < window->ax_count; i++) {
        free(window->ax[i].role);
        free(window->ax[i].label);
        free(window->ax[i].value);
    }
    window->ax_count = 0;
}

static void snapshot_ax_ids(AuraWindow *window) {
    int64_t *ids = NULL;
    int i = 0;
    free(window->ax_prev_ids);
    window->ax_prev_ids = NULL;
    window->ax_prev_count = 0;
    if (window->ax_count <= 0) {
        return;
    }
    ids = (int64_t *)malloc((size_t)window->ax_count * sizeof(int64_t));
    if (ids == NULL) {
        window->ax_prev_count = -1;
        return;
    }
    for (i = 0; i < window->ax_count; i++) {
        ids[i] = window->ax[i].node_id;
    }
    window->ax_prev_ids = ids;
    window->ax_prev_count = window->ax_count;
}

static int ax_ids_changed(const AuraWindow *window) {
    int i = 0;
    if (window->ax_prev_count < 0 || window->ax_prev_count != window->ax_count) {
        return 1;
    }
    for (i = 0; i < window->ax_count; i++) {
        if (window->ax_prev_ids[i] != window->ax[i].node_id) {
            return 1;
        }
    }
    return 0;
}

static void clear_pending_text_geometry(AuraWindow *window) {
    free(window->text_boundaries_pending);
    window->text_boundaries_pending = NULL;
    window->text_boundary_pending_count = 0;
    window->text_boundary_pending_written = 0;
    window->text_boundary_pending_target = 0;
    window->text_boundary_pending_revision = 0;
}

static void clear_text_geometry(AuraWindow *window) {
    clear_pending_text_geometry(window);
    free(window->text_boundaries);
    window->text_boundaries = NULL;
    window->text_boundary_count = 0;
    window->text_boundary_target = 0;
    window->text_boundary_revision = 0;
}

static uint64_t monotonic_now_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) {
        return 0;
    }
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) + (uint64_t)value.tv_nsec;
}

static uint64_t source_timestamp_ns(uint64_t timestamp_ns) {
    if (timestamp_ns != 0) {
        return timestamp_ns;
    }
    return monotonic_now_ns();
}

static void push_platform_event_locked(AuraWindow *window, const AuraPlatformEvent *event) {
    AuraPlatformEvent queued;
    if (window == NULL || event == NULL || !window->alive || !window->events_ready) {
        return;
    }
    queued = *event;
    window->next_event_sequence += 1;
    queued.sequence = window->next_event_sequence;
    if (queued.timestamp_ns == 0) {
        queued.timestamp_ns = monotonic_now_ns();
    }
    aura_event_queue_push(&window->events, &queued);
}

static void push_event(AuraWindow *window, int kind, double x, double y, int64_t key, int64_t button, const char *text) {
    AuraPlatformEvent event;
    aura_platform_event_init(&event, (uint32_t)kind, 0, monotonic_now_ns());
    event.x = x;
    event.y = y;
    event.key_code = key;
    event.buttons = (uint32_t)button;
    event.text = text;
    event.text_length = text != NULL ? (uint32_t)strlen(text) : 0;
    event.text_ownership = AURA_TEXT_BORROWED;
    push_platform_event_locked(window, &event);
}

static AuraAx *ax_at(AuraWindow *window, int64_t index) {
    if (window == NULL || index < 0 || index >= window->ax_count) {
        return NULL;
    }
    return &window->ax[index];
}

static int require_window(int64_t handle) {
    return window_get(handle, 1) != NULL;
}

static void push_route_locked(AuraWindow *window, int source, const char *location, uint64_t timestamp_ns) {
    AuraPlatformEvent event;
    aura_platform_event_init(&event, AURA_PLATFORM_ROUTE, 0, timestamp_ns);
    event.key_code = source;
    event.text = location != NULL ? location : "";
    event.text_length = (uint32_t)strlen(event.text);
    event.text_ownership = AURA_TEXT_BORROWED;
    push_platform_event_locked(window, &event);
}

void aura_shell_platform_route(int slot, int source, const char *location, uint64_t timestamp_ns) {
    AuraWindow *window = NULL;
    char *copy = NULL;
    int i = 0;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS || location == NULL || location[0] == 0) {
        return;
    }
    timestamp_ns = source_timestamp_ns(timestamp_ns);
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    if (window->alive) {
        push_route_locked(window, source, location, timestamp_ns);
        pthread_mutex_unlock(&window->mu);
        return;
    }
    copy = dup_text(location);
    if (copy == NULL) {
        pthread_mutex_unlock(&window->mu);
        return;
    }
    if (window->pending_route_count == AURA_PENDING_ROUTE_CAPACITY) {
        free(window->pending_routes[0]);
        for (i = 1; i < AURA_PENDING_ROUTE_CAPACITY; i++) {
            window->pending_routes[i - 1] = window->pending_routes[i];
            window->pending_route_sources[i - 1] = window->pending_route_sources[i];
            window->pending_route_timestamps[i - 1] = window->pending_route_timestamps[i];
        }
        window->pending_route_count -= 1;
        window->pending_route_dropped += 1;
    }
    window->pending_routes[window->pending_route_count] = copy;
    window->pending_route_sources[window->pending_route_count] = source;
    window->pending_route_timestamps[window->pending_route_count] = timestamp_ns;
    window->pending_route_count += 1;
    pthread_mutex_unlock(&window->mu);
}

void aura_shell_metrics(int slot, double width, double height, double scale, uint64_t timestamp_ns) {
    AuraMetricsPayload metrics;
    memset(&metrics, 0, sizeof(metrics));
    metrics.logical_width = width;
    metrics.logical_height = height;
    metrics.physical_width = (int64_t)(width * scale);
    metrics.physical_height = (int64_t)(height * scale);
    metrics.device_pixel_ratio = scale;
    metrics.refresh_rate = 60.0;
    metrics.orientation = width > height ? 2 : 1;
    metrics.font_scale = 1.0;
    aura_shell_metrics_v1(slot, &metrics, timestamp_ns);
}

void aura_shell_metrics_v1(int slot, const AuraMetricsPayload *metrics, uint64_t timestamp_ns) {
    AuraWindow *window = NULL;
    AuraPlatformEvent event;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS || metrics == NULL) {
        return;
    }
    timestamp_ns = source_timestamp_ns(timestamp_ns);
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    if (metrics->logical_width > 0.0) {
        window->width = metrics->logical_width;
    }
    if (metrics->logical_height > 0.0) {
        window->height = metrics->logical_height;
    }
    if (metrics->device_pixel_ratio > 0.0) {
        window->scale = metrics->device_pixel_ratio;
    }
    window->metrics_set = 1;
    aura_platform_event_init(&event, AURA_METRICS, 0, timestamp_ns);
    event.payload.metrics = *metrics;
    push_platform_event_locked(window, &event);
    window->frame_ready = 1;
    pthread_cond_signal(&window->cv);
    pthread_mutex_unlock(&window->mu);
}

int aura_shell_environment(int slot, const AuraEnvironmentPayload *environment, const char *locales, uint64_t timestamp_ns) {
    AuraWindow *window = NULL;
    AuraPlatformEvent event;
    int accepted = 0;
    ensure_table();
    if (environment == NULL || slot < 0 || slot >= AURA_MAX_WINDOWS) {
        return 0;
    }
    timestamp_ns = source_timestamp_ns(timestamp_ns);
    aura_platform_event_init(&event, AURA_ENVIRONMENT_CHANGED, 0, timestamp_ns);
    event.payload.environment = *environment;
    event.text = locales != NULL ? locales : "";
    event.text_length = (uint32_t)strlen(event.text);
    event.text_ownership = AURA_TEXT_BORROWED;
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    accepted = window->alive && window->events_ready;
    if (accepted) {
        push_platform_event_locked(window, &event);
        window->frame_ready = 1;
        pthread_cond_signal(&window->cv);
    }
    pthread_mutex_unlock(&window->mu);
    return accepted;
}

void aura_shell_memory_pressure(int slot, int level, uint64_t timestamp_ns) {
    AuraPlatformEvent event;
    if (level < AURA_MEMORY_PRESSURE_LOW || level > AURA_MEMORY_PRESSURE_CRITICAL) {
        return;
    }
    aura_platform_event_init(&event, AURA_MEMORY_PRESSURE, 0, source_timestamp_ns(timestamp_ns));
    event.payload.memory_pressure.level = level;
    aura_shell_post_event(slot, &event);
}

void aura_shell_lifecycle(int slot, int state, int reason, uint64_t timestamp_ns) {
    AuraPlatformEvent event;
    if (state < AURA_LIFECYCLE_DETACHED || state > AURA_LIFECYCLE_TERMINATED) {
        return;
    }
    aura_platform_event_init(&event, AURA_LIFECYCLE, 0, source_timestamp_ns(timestamp_ns));
    event.payload.lifecycle.state = state;
    event.payload.lifecycle.reason = reason;
    aura_shell_post_event(slot, &event);
}

int aura_shell_surface(int slot, int kind, int64_t surface_id, int64_t physical_width, int64_t physical_height, int format, int generation, uint64_t timestamp_ns) {
    AuraWindow *window = NULL;
    AuraPlatformEvent event;
    int accepted = 0;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS || generation <= 0) {
        return -1;
    }
    if (kind != AURA_SURFACE_CREATED && kind != AURA_SURFACE_CHANGED && kind != AURA_SURFACE_DESTROYED) {
        return -1;
    }
    timestamp_ns = source_timestamp_ns(timestamp_ns);
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    if (kind == AURA_SURFACE_CREATED) {
        if (generation > window->surface.generation) {
            window->surface_ready = 1;
            accepted = 1;
        }
    } else if (window->surface_ready && generation == window->surface.generation) {
        accepted = 1;
        if (kind == AURA_SURFACE_DESTROYED) {
            window->surface_ready = 0;
        }
    }
    if (accepted) {
        window->surface.surface_id = surface_id;
        window->surface.physical_width = physical_width;
        window->surface.physical_height = physical_height;
        window->surface.format = format;
        window->surface.generation = generation;
        window->surface_timestamp_ns = timestamp_ns;
        aura_platform_event_init(&event, (uint32_t)kind, 0, timestamp_ns);
        event.payload.surface = window->surface;
        push_platform_event_locked(window, &event);
        window->frame_ready = 1;
        pthread_cond_signal(&window->cv);
    }
    pthread_mutex_unlock(&window->mu);
    return accepted ? 0 : -1;
}

void aura_shell_request_close(int slot) {
    AuraWindow *window = NULL;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS) {
        return;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    if (window->alive) {
        window->close_requested = 1;
        window->frame_ready = 1;
        pthread_cond_signal(&window->cv);
    }
    pthread_mutex_unlock(&window->mu);
}

void aura_shell_request_close_all(void) {
    int i = 0;
    ensure_table();
    pthread_mutex_lock(&g_table);
    for (i = 0; i < AURA_MAX_WINDOWS; i++) {
        AuraWindow *window = &g_windows[i];
        pthread_mutex_lock(&window->mu);
        if (window->alive) {
            window->close_requested = 1;
            window->frame_ready = 1;
            pthread_cond_broadcast(&window->cv);
        }
        pthread_mutex_unlock(&window->mu);
    }
    pthread_mutex_unlock(&g_table);
}

void aura_shell_vsync(int slot) {
    AuraWindow *window = NULL;
    struct timespec ts;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS) {
        return;
    }
    window = &g_windows[slot];
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pthread_mutex_lock(&window->mu);
    window->frame_time = (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
    window->frame_ready = 1;
    pthread_cond_signal(&window->cv);
    pthread_mutex_unlock(&window->mu);
}

void aura_shell_post(int slot, int kind, double x, double y, int64_t key, int64_t button, const char *text) {
    AuraPlatformEvent event;
    aura_platform_event_init(&event, (uint32_t)kind, 0, monotonic_now_ns());
    event.x = x;
    event.y = y;
    event.key_code = key;
    event.buttons = (uint32_t)button;
    event.text = text;
    event.text_length = text != NULL ? (uint32_t)strlen(text) : 0;
    event.text_ownership = AURA_TEXT_BORROWED;
    aura_shell_post_event(slot, &event);
}

void aura_shell_post_event(int slot, const AuraPlatformEvent *event) {
    AuraWindow *window = NULL;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS || event == NULL) {
        return;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    push_platform_event_locked(window, event);
    if (event->kind == AURA_LIFECYCLE || event->kind == AURA_SURFACE_CREATED || event->kind == AURA_SURFACE_CHANGED || event->kind == AURA_SURFACE_DESTROYED || event->kind == AURA_ENVIRONMENT_CHANGED || event->kind == AURA_MEMORY_PRESSURE || event->kind == AURA_METRICS) {
        window->frame_ready = 1;
        pthread_cond_signal(&window->cv);
    }
    pthread_mutex_unlock(&window->mu);
}

void aura_shell_text_edit(int slot, int operation, const char *text, int64_t first, int64_t second, uint64_t timestamp_ns) {
    const char *value = text != NULL ? text : "";
    aura_shell_text_edit_n(slot, operation, value, strlen(value), first, second, timestamp_ns);
}

void aura_shell_text_edit_n(int slot, int operation, const char *text, size_t length, int64_t first, int64_t second, uint64_t timestamp_ns) {
    AuraWindow *window = NULL;
    AuraPlatformEvent event;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS || operation < AURA_TEXT_EDIT_COMMIT || operation > AURA_TEXT_EDIT_PERFORM_ACTION || length > 16u * 1024u * 1024u) {
        return;
    }
    timestamp_ns = source_timestamp_ns(timestamp_ns);
    window = &g_windows[slot];
    aura_platform_event_init(&event, AURA_TEXT_EDIT, 0, timestamp_ns);
    event.text = text != NULL ? text : "";
    event.text_length = (uint32_t)length;
    event.text_ownership = AURA_TEXT_BORROWED;
    event.payload.text_editing.operation = operation;
    event.payload.text_editing.first = first;
    event.payload.text_editing.second = second;
    pthread_mutex_lock(&window->mu);
    if (window->alive && window->text_focus && window->text_generation > 0) {
        window->text_platform_revision += 1;
        event.payload.text_editing.generation = window->text_generation;
        event.payload.text_editing.revision = window->text_platform_revision;
        push_platform_event_locked(window, &event);
    }
    pthread_mutex_unlock(&window->mu);
}

int aura_shell_ax_count(int slot) {
    AuraWindow *window = NULL;
    int count = 0;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS) {
        return 0;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    count = window->ax_count;
    pthread_mutex_unlock(&window->mu);
    return count;
}

static const char *ax_field(int slot, int index, int field) {
    AuraWindow *window = NULL;
    AuraAx *node = NULL;
    const char *text = "";
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS) {
        return "";
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    node = ax_at(window, index);
    if (node != NULL) {
        if (field == 0 && node->role != NULL) {
            text = node->role;
        } else if (field == 1 && node->label != NULL) {
            text = node->label;
        } else if (field == 2 && node->value != NULL) {
            text = node->value;
        }
    }
    pthread_mutex_unlock(&window->mu);
    return text;
}

const char *aura_shell_ax_role(int slot, int index) {
    return ax_field(slot, index, 0);
}

const char *aura_shell_ax_label(int slot, int index) {
    return ax_field(slot, index, 1);
}

const char *aura_shell_ax_value(int slot, int index) {
    return ax_field(slot, index, 2);
}

int aura_shell_ax_checked(int slot, int index) {
    AuraWindow *window = NULL;
    AuraAx *node = NULL;
    int checked = 0;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS) {
        return 0;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    node = ax_at(window, index);
    if (node != NULL) {
        checked = node->checked;
    }
    pthread_mutex_unlock(&window->mu);
    return checked;
}

int64_t aura_shell_ax_press(int slot, int index) {
    AuraWindow *window = NULL;
    AuraAx *node = NULL;
    int64_t press = -1;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS) {
        return -1;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    node = ax_at(window, index);
    if (node != NULL) {
        press = node->press;
    }
    pthread_mutex_unlock(&window->mu);
    return press;
}

int aura_shell_ax_heading(int slot, int index) {
    AuraWindow *window = NULL;
    AuraAx *node = NULL;
    int heading = 0;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS) {
        return 0;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    node = ax_at(window, index);
    if (node != NULL) {
        heading = node->heading;
    }
    pthread_mutex_unlock(&window->mu);
    return heading;
}

int64_t aura_shell_ax_node_id(int slot, int index) {
    AuraWindow *window = NULL;
    AuraAx *node = NULL;
    int64_t node_id = 0;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS) {
        return 0;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    node = ax_at(window, index);
    if (node != NULL) {
        node_id = node->node_id;
    }
    pthread_mutex_unlock(&window->mu);
    return node_id;
}

int aura_shell_activate_ax(int slot, int index) {
    int64_t press = aura_shell_ax_press(slot, index);
    if (press < 0) {
        return 0;
    }
    aura_shell_post(slot, AURA_AX_ACTION, 0.0, 0.0, press, 0, NULL);
    return 1;
}

int aura_shell_ax_adjust(int slot, int index, int direction) {
    int64_t node_id = 0;
    AuraWindow *window = NULL;
    AuraAx *node = NULL;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS || direction == 0) {
        return 0;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    node = ax_at(window, index);
    if (node == NULL) {
        pthread_mutex_unlock(&window->mu);
        return 0;
    }
    node_id = node->node_id;
    pthread_mutex_unlock(&window->mu);
    aura_shell_post(slot, AURA_AX_ACTION, direction > 0 ? 1.0 : -1.0, (double)node_id, -1, 0, NULL);
    return 1;
}

void aura_shell_ax_frame(int slot, int index, double *x, double *y, double *w, double *h) {
    AuraWindow *window = NULL;
    AuraAx *node = NULL;
    ensure_table();
    if (x != NULL) {
        *x = 0.0;
    }
    if (y != NULL) {
        *y = 0.0;
    }
    if (w != NULL) {
        *w = 0.0;
    }
    if (h != NULL) {
        *h = 0.0;
    }
    if (slot < 0 || slot >= AURA_MAX_WINDOWS) {
        return;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    node = ax_at(window, index);
    if (node != NULL) {
        if (x != NULL) {
            *x = node->x;
        }
        if (y != NULL) {
            *y = node->y;
        }
        if (w != NULL) {
            *w = node->w;
        }
        if (h != NULL) {
            *h = node->h;
        }
    }
    pthread_mutex_unlock(&window->mu);
}

int aura_shell_copy_pixels(int slot, uint8_t **out, int *width, int *height, int *stride) {
    AuraWindow *window = NULL;
    size_t bytes = 0;
    uint8_t *copy = NULL;
    ensure_table();
    if (out == NULL) {
        return -1;
    }
    *out = NULL;
    if (slot < 0 || slot >= AURA_MAX_WINDOWS) {
        return -1;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    if (window->pixels == NULL || window->pixel_w <= 0 || window->pixel_h <= 0 || window->pixel_stride <= 0) {
        pthread_mutex_unlock(&window->mu);
        return -1;
    }
    bytes = (size_t)window->pixel_stride * (size_t)window->pixel_h;
    copy = (uint8_t *)malloc(bytes);
    if (copy != NULL) {
        memcpy(copy, window->pixels, bytes);
        if (width != NULL) {
            *width = window->pixel_w;
        }
        if (height != NULL) {
            *height = window->pixel_h;
        }
        if (stride != NULL) {
            *stride = window->pixel_stride;
        }
    }
    pthread_mutex_unlock(&window->mu);
    if (copy == NULL) {
        return -1;
    }
    *out = copy;
    return 0;
}

int64_t aura_window_create(int64_t width, int64_t height, const char *title, int64_t visible) {
    int slot = -1;
    int i = 0;
    AuraWindow *window = NULL;
    ensure_table();
    pthread_mutex_lock(&g_table);
    for (i = 0; i < AURA_MAX_WINDOWS; i++) {
        if (!g_windows[i].alive) {
            slot = i;
            g_windows[i].alive = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_table);
    if (slot < 0) {
        return 0;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    window->shown = visible != 0;
    window->close_requested = 0;
    window->cursor_kind = 0;
    window->text_focus = 0;
    if (!window->metrics_set) {
        window->width = width > 0 ? (double)width : 800.0;
        window->height = height > 0 ? (double)height : 600.0;
        window->scale = window->scale > 0.0 ? window->scale : 1.0;
    }
    free(window->title);
    window->title = dup_text(title != NULL ? title : "Aura");
    reset_events(window);
    window->next_event_sequence = 0;
    clear_ax(window);
    free(window->ax_prev_ids);
    window->ax_prev_ids = NULL;
    window->ax_prev_count = 0;
    window->ax_published = 0;
    window->ax_open = 0;
    if (window->surface_ready) {
        AuraPlatformEvent surface_event;
        aura_platform_event_init(&surface_event, AURA_SURFACE_CREATED, 0, window->surface_timestamp_ns != 0 ? window->surface_timestamp_ns : monotonic_now_ns());
        surface_event.payload.surface = window->surface;
        push_platform_event_locked(window, &surface_event);
    }
    for (i = 0; i < window->pending_route_count; i++) {
        push_route_locked(window, window->pending_route_sources[i], window->pending_routes[i], window->pending_route_timestamps[i]);
        free(window->pending_routes[i]);
        window->pending_routes[i] = NULL;
    }
    window->pending_route_count = 0;
    if (window->pending_route_dropped > 0) {
        AuraPlatformEvent overflow_event;
        aura_platform_event_init(&overflow_event, AURA_QUEUE_OVERFLOW, 0, monotonic_now_ns());
        overflow_event.payload.queue_overflow.dropped_count = window->pending_route_dropped;
        overflow_event.payload.queue_overflow.overflow_count = 1;
        overflow_event.payload.queue_overflow.capacity = AURA_PENDING_ROUTE_CAPACITY;
        push_platform_event_locked(window, &overflow_event);
        window->pending_route_dropped = 0;
    }
    {
        double open_w = window->width;
        double open_h = window->height;
        int open_shown = window->shown;
        char *open_title = dup_text(window->title);
        pthread_mutex_unlock(&window->mu);
        aura_host_open(slot, open_w, open_h, open_title != NULL ? open_title : "Aura", open_shown);
        free(open_title);
    }
    return (int64_t)slot + 1;
}

int64_t aura_window_destroy(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_host_close((int)handle - 1);
    pthread_mutex_lock(&window->mu);
    window->alive = 0;
    window->shown = 0;
    reset_events(window);
    clear_ax(window);
    free(window->ax_prev_ids);
    window->ax_prev_ids = NULL;
    window->ax_prev_count = 0;
    window->ax_published = 0;
    free(window->pixels);
    window->pixels = NULL;
    free(window->title);
    window->title = NULL;
    free(window->text_contents);
    window->text_contents = NULL;
    free(window->text_autofill_hints);
    window->text_autofill_hints = NULL;
    clear_text_geometry(window);
    pthread_mutex_unlock(&window->mu);
    return 0;
}

int64_t aura_window_poll(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    size_t count = 0;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (window->events_ready) {
        count = aura_event_queue_begin_snapshot(&window->events);
    }
    pthread_mutex_unlock(&window->mu);
    return (int64_t)count;
}

int64_t aura_window_event_count(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    size_t count = 0;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (window->events_ready) {
        count = aura_event_queue_snapshot_count(&window->events);
    }
    pthread_mutex_unlock(&window->mu);
    return (int64_t)count;
}

int64_t aura_window_event_kind(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    int kind = -1;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event)) {
        kind = (int)event.kind;
    }
    pthread_mutex_unlock(&window->mu);
    return kind;
}

double aura_window_event_x(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    double value = 0.0;
    if (window == NULL) {
        return 0.0;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event)) {
        value = event.x;
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

double aura_window_event_y(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    double value = 0.0;
    if (window == NULL) {
        return 0.0;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event)) {
        value = event.y;
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_event_key(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    int64_t value = 0;
    if (window == NULL) {
        return 0;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event)) {
        value = event.key_code;
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_event_button(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    int64_t value = 0;
    if (window == NULL) {
        return 0;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event)) {
        value = event.buttons;
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_event_modifiers(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    int64_t value = 0;
    if (window == NULL) {
        return 0;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event)) {
        value = event.modifiers;
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_event_device_id(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    int64_t value = 0;
    if (window == NULL) return 0;
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event)) value = event.device_id;
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_event_pointer_id(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    int64_t value = 0;
    if (window == NULL) return 0;
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event)) value = event.pointer_id;
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_event_pointer_kind(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    int64_t value = 0;
    if (window == NULL) return 0;
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event)) value = event.pointer_kind;
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_event_timestamp_ns(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    int64_t value = 0;
    if (window == NULL) return 0;
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event)) value = (int64_t)event.timestamp_ns;
    pthread_mutex_unlock(&window->mu);
    return value;
}

double aura_window_event_metrics_value(int64_t handle, int64_t index, int64_t field) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    AuraMetricsPayload metrics;
    double value = 0.0;
    if (window == NULL) {
        return 0.0;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event) && event.kind == AURA_METRICS) {
        metrics = event.payload.metrics;
        switch (field) {
        case AURA_METRIC_LOGICAL_WIDTH: value = metrics.logical_width; break;
        case AURA_METRIC_LOGICAL_HEIGHT: value = metrics.logical_height; break;
        case AURA_METRIC_PHYSICAL_WIDTH: value = (double)metrics.physical_width; break;
        case AURA_METRIC_PHYSICAL_HEIGHT: value = (double)metrics.physical_height; break;
        case AURA_METRIC_DEVICE_PIXEL_RATIO: value = metrics.device_pixel_ratio; break;
        case AURA_METRIC_REFRESH_RATE: value = metrics.refresh_rate; break;
        case AURA_METRIC_VIEW_PADDING_LEFT: value = metrics.view_padding.left; break;
        case AURA_METRIC_VIEW_PADDING_TOP: value = metrics.view_padding.top; break;
        case AURA_METRIC_VIEW_PADDING_RIGHT: value = metrics.view_padding.right; break;
        case AURA_METRIC_VIEW_PADDING_BOTTOM: value = metrics.view_padding.bottom; break;
        case AURA_METRIC_VIEW_INSET_LEFT: value = metrics.view_insets.left; break;
        case AURA_METRIC_VIEW_INSET_TOP: value = metrics.view_insets.top; break;
        case AURA_METRIC_VIEW_INSET_RIGHT: value = metrics.view_insets.right; break;
        case AURA_METRIC_VIEW_INSET_BOTTOM: value = metrics.view_insets.bottom; break;
        case AURA_METRIC_SYSTEM_GESTURE_LEFT: value = metrics.system_gesture_insets.left; break;
        case AURA_METRIC_SYSTEM_GESTURE_TOP: value = metrics.system_gesture_insets.top; break;
        case AURA_METRIC_SYSTEM_GESTURE_RIGHT: value = metrics.system_gesture_insets.right; break;
        case AURA_METRIC_SYSTEM_GESTURE_BOTTOM: value = metrics.system_gesture_insets.bottom; break;
        case AURA_METRIC_ORIENTATION: value = (double)metrics.orientation; break;
        case AURA_METRIC_ROTATION_DEGREES: value = (double)metrics.rotation_degrees; break;
        case AURA_METRIC_FONT_SCALE: value = metrics.font_scale; break;
        case AURA_METRIC_KEYBOARD_DURATION: value = metrics.keyboard_duration_s; break;
        case AURA_METRIC_KEYBOARD_CURVE: value = (double)metrics.keyboard_curve; break;
        case AURA_METRIC_CUTOUT_COUNT: value = (double)metrics.cutout_count; break;
        case AURA_METRIC_CUTOUT_TRUNCATED: value = (double)metrics.cutout_truncated; break;
        default: break;
        }
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

double aura_window_event_cutout_value(int64_t handle, int64_t index, int64_t rect_index, int64_t field) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    AuraRect rect;
    double value = 0.0;
    if (window == NULL || rect_index < 0) {
        return 0.0;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event) && event.kind == AURA_METRICS && rect_index < event.payload.metrics.cutout_count && rect_index < AURA_MAX_DISPLAY_CUTOUTS) {
        rect = event.payload.metrics.cutouts[rect_index];
        switch (field) {
        case AURA_CUTOUT_X: value = rect.x; break;
        case AURA_CUTOUT_Y: value = rect.y; break;
        case AURA_CUTOUT_WIDTH: value = rect.width; break;
        case AURA_CUTOUT_HEIGHT: value = rect.height; break;
        default: break;
        }
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

double aura_window_event_environment_value(int64_t handle, int64_t index, int64_t field) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    AuraEnvironmentPayload environment;
    double value = 0.0;
    if (window == NULL) {
        return 0.0;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event) && event.kind == AURA_ENVIRONMENT_CHANGED) {
        environment = event.payload.environment;
        switch (field) {
        case AURA_ENV_BRIGHTNESS: value = (double)environment.brightness; break;
        case AURA_ENV_HIGH_CONTRAST: value = (double)environment.high_contrast; break;
        case AURA_ENV_REDUCE_MOTION: value = (double)environment.reduce_motion; break;
        case AURA_ENV_ACCESSIBLE_NAVIGATION: value = (double)environment.accessible_navigation; break;
        case AURA_ENV_TEXT_DIRECTION: value = (double)environment.text_direction; break;
        case AURA_ENV_BOLD_TEXT: value = (double)environment.bold_text; break;
        case AURA_ENV_FONT_SCALE: value = environment.font_scale; break;
        default: break;
        }
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_event_memory_level(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    int64_t value = AURA_MEMORY_PRESSURE_NONE;
    if (window == NULL) {
        return value;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event) && event.kind == AURA_MEMORY_PRESSURE) {
        value = event.payload.memory_pressure.level;
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_event_surface_value(int64_t handle, int64_t index, int64_t field) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    AuraSurfacePayload surface;
    int64_t value = 0;
    if (window == NULL) {
        return 0;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event) &&
        (event.kind == AURA_SURFACE_CREATED || event.kind == AURA_SURFACE_CHANGED || event.kind == AURA_SURFACE_DESTROYED)) {
        surface = event.payload.surface;
        switch (field) {
        case AURA_SURFACE_FIELD_ID: value = surface.surface_id; break;
        case AURA_SURFACE_FIELD_PHYSICAL_WIDTH: value = surface.physical_width; break;
        case AURA_SURFACE_FIELD_PHYSICAL_HEIGHT: value = surface.physical_height; break;
        case AURA_SURFACE_FIELD_FORMAT: value = surface.format; break;
        case AURA_SURFACE_FIELD_GENERATION: value = surface.generation; break;
        default: break;
        }
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_event_text_edit_value(int64_t handle, int64_t index, int64_t field) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    AuraTextEditingPayload edit;
    int64_t value = 0;
    if (window == NULL) {
        return 0;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event) && event.kind == AURA_TEXT_EDIT) {
        edit = event.payload.text_editing;
        switch (field) {
        case AURA_TEXT_EDIT_FIELD_OPERATION: value = edit.operation; break;
        case AURA_TEXT_EDIT_FIELD_GENERATION: value = edit.generation; break;
        case AURA_TEXT_EDIT_FIELD_REVISION: value = edit.revision; break;
        case AURA_TEXT_EDIT_FIELD_FIRST: value = edit.first; break;
        case AURA_TEXT_EDIT_FIELD_SECOND: value = edit.second; break;
        default: break;
        }
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_surface_available(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    int available = 0;
    if (window == NULL) {
        return 0;
    }
    pthread_mutex_lock(&window->mu);
    available = window->surface_ready;
    pthread_mutex_unlock(&window->mu);
    return available;
}

int64_t aura_window_event_lifecycle_state(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    int64_t value = AURA_LIFECYCLE_DETACHED;
    if (window == NULL) {
        return value;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event) && event.kind == AURA_LIFECYCLE) {
        value = event.payload.lifecycle.state;
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_event_lifecycle_reason(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    int64_t value = AURA_LIFECYCLE_REASON_NONE;
    if (window == NULL) {
        return value;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event) && event.kind == AURA_LIFECYCLE) {
        value = event.payload.lifecycle.reason;
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_events_clear(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (window->events_ready) {
        aura_event_queue_clear_snapshot(&window->events);
    }
    pthread_mutex_unlock(&window->mu);
    return 0;
}

const char *aura_window_event_text(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraPlatformEvent event;
    const char *text = "";
    if (window == NULL) {
        return "";
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready && aura_event_queue_snapshot_copy(&window->events, (size_t)index, &event) && event.text != NULL) {
        text = event.text;
    }
    pthread_mutex_unlock(&window->mu);
    return text;
}

int64_t aura_window_event_text_len(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    size_t length = 0;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && window->events_ready) {
        length = aura_event_queue_snapshot_text_length(&window->events, (size_t)index);
    }
    pthread_mutex_unlock(&window->mu);
    return (int64_t)length;
}

int64_t aura_window_event_text_byte(int64_t handle, int64_t index, int64_t offset) {
    AuraWindow *window = window_get(handle, 1);
    int64_t value = 0;
    if (window == NULL) {
        return 0;
    }
    pthread_mutex_lock(&window->mu);
    if (index >= 0 && offset >= 0 && window->events_ready) {
        value = aura_event_queue_snapshot_text_byte(&window->events, (size_t)index, (size_t)offset);
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_text_focus(int64_t handle, int64_t enabled) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    window->text_focus = enabled != 0;
    pthread_mutex_unlock(&window->mu);
    aura_host_text_focus((int)handle - 1, enabled != 0);
    return 0;
}

int64_t aura_window_system_back_result(int64_t handle, int64_t handled) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_host_system_back_result((int)handle - 1, handled != 0);
    return 0;
}

int64_t aura_window_capabilities(int64_t handle) {
    if (!require_window(handle)) {
        return 0;
    }
    return aura_host_capabilities((int)handle - 1);
}

int64_t aura_text_selection(int64_t handle, int64_t start, int64_t length) {
    (void)start;
    (void)length;
    if (!require_window(handle)) {
        return -1;
    }
    return 0;
}

int64_t aura_text_contents(int64_t handle, const char *utf8) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    free(window->text_contents);
    window->text_contents = dup_text(utf8);
    pthread_mutex_unlock(&window->mu);
    return window->text_contents == NULL ? -1 : 0;
}

int64_t aura_text_caret(int64_t handle, double x, double y, double height) {
    (void)x;
    (void)y;
    (void)height;
    if (!require_window(handle)) {
        return -1;
    }
    return 0;
}

int64_t aura_text_editing_state(int64_t handle, const char *text, int64_t generation, int64_t revision, int64_t selection_start, int64_t selection_end, int64_t composing_start, int64_t composing_end) {
    AuraWindow *window = window_get(handle, 1);
    char *copy = NULL;
    char *host_copy = NULL;
    int generation_changed = 0;
    if (window == NULL || generation < 0 || revision < 0) {
        return -1;
    }
    copy = dup_text(text != NULL ? text : "");
    host_copy = dup_text(text != NULL ? text : "");
    if (copy == NULL || host_copy == NULL) {
        free(copy);
        free(host_copy);
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    generation_changed = window->text_generation != generation;
    free(window->text_contents);
    window->text_contents = copy;
    window->text_generation = generation;
    window->text_revision = revision;
    window->text_selection_start = selection_start;
    window->text_selection_end = selection_end;
    window->text_composing_start = composing_start;
    window->text_composing_end = composing_end;
    if (generation_changed || window->text_platform_revision < revision) {
        window->text_platform_revision = revision;
    }
    pthread_mutex_unlock(&window->mu);
    aura_host_text_state((int)handle - 1, host_copy, generation, revision, selection_start, selection_end, composing_start, composing_end);
    free(host_copy);
    return 0;
}

int64_t aura_text_geometry(int64_t handle, double caret_x, double caret_y, double caret_width, double caret_height, double editable_x, double editable_y, double editable_width, double editable_height) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || caret_width < 0.0 || caret_height < 0.0 || editable_width < 0.0 || editable_height < 0.0) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    window->text_caret_x = caret_x;
    window->text_caret_y = caret_y;
    window->text_caret_width = caret_width;
    window->text_caret_height = caret_height;
    window->text_editable_x = editable_x;
    window->text_editable_y = editable_y;
    window->text_editable_width = editable_width;
    window->text_editable_height = editable_height;
    pthread_mutex_unlock(&window->mu);
    aura_host_text_geometry((int)handle - 1, caret_x, caret_y, caret_width, caret_height, editable_x, editable_y, editable_width, editable_height);
    return 0;
}

int64_t aura_text_geometry_begin(int64_t handle, int64_t target, int64_t revision, int64_t count) {
    AuraWindow *window = window_get(handle, 1);
    AuraTextBoundary *pending = NULL;
    if (window == NULL || target <= 0 || revision <= 0 || count <= 0 || count > AURA_TEXT_BOUNDARY_LIMIT || (uint64_t)count > SIZE_MAX / sizeof(AuraTextBoundary)) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (window->text_boundary_target == target && window->text_boundary_revision == revision) {
        pthread_mutex_unlock(&window->mu);
        return 0;
    }
    if (window->text_boundary_target == target && revision < window->text_boundary_revision) {
        pthread_mutex_unlock(&window->mu);
        return -1;
    }
    pending = (AuraTextBoundary *)calloc((size_t)count, sizeof(AuraTextBoundary));
    if (pending == NULL) {
        pthread_mutex_unlock(&window->mu);
        return -1;
    }
    clear_pending_text_geometry(window);
    window->text_boundaries_pending = pending;
    window->text_boundary_pending_count = (int)count;
    window->text_boundary_pending_target = target;
    window->text_boundary_pending_revision = revision;
    pthread_mutex_unlock(&window->mu);
    return 1;
}

int64_t aura_text_geometry_boundary(int64_t handle, int64_t target, int64_t revision, int64_t index, int64_t offset, double x, double y, double height) {
    AuraWindow *window = window_get(handle, 1);
    AuraTextBoundary *boundary = NULL;
    if (window == NULL || target <= 0 || revision <= 0 || index < 0 || offset < 0 || !isfinite(x) || !isfinite(y) || !isfinite(height) || height <= 0.0) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (window->text_boundaries_pending == NULL || window->text_boundary_pending_target != target || window->text_boundary_pending_revision != revision || index != window->text_boundary_pending_written || index >= window->text_boundary_pending_count) {
        pthread_mutex_unlock(&window->mu);
        return -1;
    }
    if (index > 0 && offset <= window->text_boundaries_pending[index - 1].offset) {
        pthread_mutex_unlock(&window->mu);
        return -1;
    }
    boundary = &window->text_boundaries_pending[index];
    boundary->offset = offset;
    boundary->x = x;
    boundary->y = y;
    boundary->height = height;
    window->text_boundary_pending_written += 1;
    pthread_mutex_unlock(&window->mu);
    return 0;
}

int64_t aura_text_geometry_end(int64_t handle, int64_t target, int64_t revision) {
    AuraWindow *window = window_get(handle, 1);
    int count = 0;
    if (window == NULL || target <= 0 || revision <= 0) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (window->text_boundaries_pending == NULL || window->text_boundary_pending_target != target || window->text_boundary_pending_revision != revision || window->text_boundary_pending_written != window->text_boundary_pending_count) {
        clear_pending_text_geometry(window);
        pthread_mutex_unlock(&window->mu);
        return -1;
    }
    free(window->text_boundaries);
    window->text_boundaries = window->text_boundaries_pending;
    window->text_boundary_count = window->text_boundary_pending_count;
    window->text_boundary_target = target;
    window->text_boundary_revision = revision;
    count = window->text_boundary_count;
    window->text_boundaries_pending = NULL;
    window->text_boundary_pending_count = 0;
    window->text_boundary_pending_written = 0;
    window->text_boundary_pending_target = 0;
    window->text_boundary_pending_revision = 0;
    pthread_mutex_unlock(&window->mu);
    aura_host_text_geometry_snapshot((int)handle - 1, target, revision, count);
    return 0;
}

int aura_shell_text_geometry_boundary(int slot, int index, int64_t *offset, double *x, double *y, double *height) {
    AuraWindow *window = NULL;
    AuraTextBoundary boundary;
    ensure_table();
    if (slot < 0 || slot >= AURA_MAX_WINDOWS || offset == NULL || x == NULL || y == NULL || height == NULL) {
        return -1;
    }
    window = &g_windows[slot];
    pthread_mutex_lock(&window->mu);
    if (!window->alive || window->text_boundaries == NULL || index < 0 || index >= window->text_boundary_count) {
        pthread_mutex_unlock(&window->mu);
        return -1;
    }
    boundary = window->text_boundaries[index];
    pthread_mutex_unlock(&window->mu);
    *offset = boundary.offset;
    *x = boundary.x;
    *y = boundary.y;
    *height = boundary.height;
    return 0;
}

int64_t aura_text_configuration(int64_t handle, int64_t input_type, int64_t action, int64_t multiline, int64_t secure, int64_t autocorrect, int64_t capitalization, const char *autofill_hints) {
    AuraWindow *window = window_get(handle, 1);
    char *copy = NULL;
    char *host_copy = NULL;
    int changed = 0;
    if (window == NULL || input_type < 1 || input_type > 5 || action < 0 || action > 6 || capitalization < 0 || capitalization > 3) {
        return -1;
    }
    copy = dup_text(autofill_hints != NULL ? autofill_hints : "");
    host_copy = dup_text(autofill_hints != NULL ? autofill_hints : "");
    if (copy == NULL || host_copy == NULL) {
        free(copy);
        free(host_copy);
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    changed = window->text_input_type != input_type || window->text_action != action || window->text_multiline != (multiline != 0) || window->text_secure != (secure != 0) || window->text_autocorrect != (autocorrect != 0) || window->text_capitalization != capitalization || window->text_autofill_hints == NULL || strcmp(window->text_autofill_hints, copy) != 0;
    free(window->text_autofill_hints);
    window->text_autofill_hints = copy;
    window->text_input_type = (int)input_type;
    window->text_action = (int)action;
    window->text_multiline = multiline != 0;
    window->text_secure = secure != 0;
    window->text_autocorrect = autocorrect != 0;
    window->text_capitalization = (int)capitalization;
    pthread_mutex_unlock(&window->mu);
    if (changed) {
        aura_host_text_configuration((int)handle - 1, (int)input_type, (int)action, multiline != 0, secure != 0, autocorrect != 0, (int)capitalization, host_copy);
    }
    free(host_copy);
    return 0;
}

int64_t aura_post_text(int64_t handle, const char *utf8) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    push_event(window, AURA_TEXT, 0, 0, 0, 0, utf8 != NULL ? utf8 : "");
    pthread_mutex_unlock(&window->mu);
    return 0;
}

int64_t aura_window_should_close(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    int close_requested = 0;
    if (window == NULL) {
        return 1;
    }
    pthread_mutex_lock(&window->mu);
    close_requested = window->close_requested;
    pthread_mutex_unlock(&window->mu);
    return close_requested;
}

double aura_window_width(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    double value = 0.0;
    if (window == NULL) {
        return 0.0;
    }
    pthread_mutex_lock(&window->mu);
    value = window->width;
    pthread_mutex_unlock(&window->mu);
    return value;
}

double aura_window_height(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    double value = 0.0;
    if (window == NULL) {
        return 0.0;
    }
    pthread_mutex_lock(&window->mu);
    value = window->height;
    pthread_mutex_unlock(&window->mu);
    return value;
}

double aura_window_scale(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    double value = 1.0;
    if (window == NULL) {
        return 1.0;
    }
    pthread_mutex_lock(&window->mu);
    value = window->scale > 0.0 ? window->scale : 1.0;
    pthread_mutex_unlock(&window->mu);
    return value;
}

double aura_frame_timestamp(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    double value = 0.0;
    if (window == NULL) {
        return 0.0;
    }
    pthread_mutex_lock(&window->mu);
    value = window->frame_time;
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_window_wait_frame(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    struct timespec deadline;
    int ready = 0;
    if (window == NULL) {
        return -1;
    }
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_nsec += 500000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&window->mu);
    while (!window->frame_ready) {
        if (pthread_cond_timedwait(&window->cv, &window->mu, &deadline) != 0) {
            break;
        }
    }
    ready = window->frame_ready;
    window->frame_ready = 0;
    pthread_mutex_unlock(&window->mu);
    return ready ? 1 : 0;
}

int64_t aura_frame_begin(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    double width = 0.0;
    double height = 0.0;
    double scale = 1.0;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    width = window->width;
    height = window->height;
    scale = window->scale > 0.0 ? window->scale : 1.0;
    pthread_mutex_unlock(&window->mu);
    return aura_paint_begin((int64_t)(width * scale), (int64_t)(height * scale), scale);
}

static void copy_frame_pixels(AuraWindow *window) {
    const uint8_t *source = aura_paint_pixels();
    int64_t width = aura_paint_pixel_width();
    int64_t height = aura_paint_pixel_height();
    int64_t stride = aura_paint_stride();
    size_t bytes = 0;
    uint8_t *next = NULL;
    if (source == NULL || width <= 0 || height <= 0 || stride <= 0) {
        return;
    }
    bytes = (size_t)stride * (size_t)height;
    next = (uint8_t *)malloc(bytes);
    if (next == NULL) {
        return;
    }
    memcpy(next, source, bytes);
    free(window->pixels);
    window->pixels = next;
    window->pixel_w = (int)width;
    window->pixel_h = (int)height;
    window->pixel_stride = (int)stride;
}

int64_t aura_frame_end(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    int painted = 0;
    if (window == NULL) {
        return -1;
    }
    painted = aura_paint_end();
    pthread_mutex_lock(&window->mu);
    copy_frame_pixels(window);
    pthread_mutex_unlock(&window->mu);
    aura_host_present((int)handle - 1);
    return painted;
}

int64_t aura_cmd_clear(int64_t handle, int64_t argb) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_clear(argb);
}

int64_t aura_cmd_rect(int64_t handle, double x, double y, double w, double h, int64_t argb) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_rect(x, y, w, h, argb);
}

int64_t aura_cmd_rrect(int64_t handle, double x, double y, double w, double h, double radius, int64_t argb) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_rrect(x, y, w, h, radius, argb);
}

int64_t aura_cmd_rrect_stroke(int64_t handle, double x, double y, double w, double h, double radius, double stroke, int64_t argb) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_rrect_stroke(x, y, w, h, radius, stroke, argb);
}

int64_t aura_cmd_oval(int64_t handle, double x, double y, double w, double h, int64_t argb) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_oval(x, y, w, h, argb);
}

int64_t aura_cmd_line(int64_t handle, double x, double y, double x2, double y2, double stroke, int64_t argb) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_line(x, y, x2, y2, stroke, argb);
}

int64_t aura_cmd_shadow(int64_t handle, double x, double y, double w, double h, double radius, double dx, double dy, double blur, int64_t argb) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_shadow(x, y, w, h, radius, dx, dy, blur, argb);
}

int64_t aura_cmd_path_begin(int64_t handle) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_path_begin();
}

int64_t aura_cmd_path_move(int64_t handle, double x, double y) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_path_move(x, y);
}

int64_t aura_cmd_path_line(int64_t handle, double x, double y) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_path_line(x, y);
}

int64_t aura_cmd_path_quad(int64_t handle, double cx, double cy, double x, double y) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_path_quad(cx, cy, x, y);
}

int64_t aura_cmd_path_cubic(int64_t handle, double c1x, double c1y, double c2x, double c2y, double x, double y) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_path_cubic(c1x, c1y, c2x, c2y, x, y);
}

int64_t aura_cmd_path_close(int64_t handle) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_path_close();
}

int64_t aura_cmd_path_fill(int64_t handle, int64_t argb) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_path_fill(argb);
}

int64_t aura_cmd_path_stroke(int64_t handle, double stroke, int64_t argb) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_path_stroke(stroke, argb);
}

int64_t aura_cmd_text(int64_t handle, double x, double y, const char *text, double size, int64_t weight, int64_t argb, const char *family) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_text(x, y, text, size, weight, argb, family);
}

int64_t aura_cmd_text_style(int64_t handle, double x, double y, const char *text, double size, int64_t weight, int64_t argb, const char *family, int64_t italic) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_text_style(x, y, text, size, weight, argb, family, italic);
}

int64_t aura_cmd_opacity(int64_t handle, double opacity) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_opacity(opacity);
}

int64_t aura_cmd_opacity_pop(int64_t handle) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_opacity_pop();
}

int64_t aura_cmd_clip(int64_t handle, double x, double y, double w, double h) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_clip(x, y, w, h);
}

int64_t aura_cmd_clip_pop(int64_t handle) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_clip_pop();
}

int64_t aura_sample(int64_t handle, double x, double y) {
    AuraWindow *window = window_get(handle, 1);
    int px = 0;
    int py = 0;
    uint8_t *pixel = NULL;
    int a = 0;
    int r = 0;
    int g = 0;
    int b = 0;
    int64_t value = -1;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (window->pixels != NULL) {
        px = (int)(x * window->scale);
        py = (int)(y * window->scale);
        if (px >= 0 && py >= 0 && px < window->pixel_w && py < window->pixel_h) {
            pixel = window->pixels + (size_t)py * (size_t)window->pixel_stride + (size_t)px * 4;
            r = pixel[0];
            g = pixel[1];
            b = pixel[2];
            a = pixel[3];
            if (a > 0 && a < 255) {
                r = r * 255 / a;
                g = g * 255 / a;
                b = b * 255 / a;
            }
            value = ((int64_t)a << 24) | ((int64_t)r << 16) | ((int64_t)g << 8) | (int64_t)b;
        }
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_drag_file(int64_t handle, const char *path) {
    if (!require_window(handle) || path == NULL || path[0] == 0) {
        return -1;
    }
    return 0;
}

int64_t aura_post_drop(int64_t handle, double x, double y, const char *path) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    push_event(window, AURA_FILE_DROP, x, y, 0, 0, path != NULL ? path : "");
    pthread_mutex_unlock(&window->mu);
    return 0;
}

int64_t aura_post_platform_route(int64_t handle, int64_t source, const char *location) {
    if (window_get(handle, 1) == NULL || location == NULL || location[0] == 0) {
        return -1;
    }
    aura_shell_platform_route((int)handle - 1, (int)source, location, 0);
    return 0;
}

int64_t aura_post_mouse(int64_t handle, int64_t kind, double x, double y, int64_t button) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    push_event(window, (int)kind, x, y, 0, button, NULL);
    pthread_mutex_unlock(&window->mu);
    return 0;
}

int64_t aura_clipboard_set(const char *text) {
    return aura_host_clipboard_set(text != NULL ? text : "");
}

int64_t aura_clipboard_len(void) {
    const char *text = aura_host_clipboard_get();
    size_t length = 0;
    if (text == NULL) {
        free(g_clip);
        g_clip = NULL;
        g_clip_length = 0;
        return -1;
    }
    length = aura_host_clipboard_length();
    free(g_clip);
    g_clip = dup_bytes(text, length);
    if (g_clip == NULL) {
        g_clip_length = 0;
        return -1;
    }
    g_clip_length = length;
    return (int64_t)length;
}

int64_t aura_clipboard_byte(int64_t offset) {
    if (g_clip == NULL || offset < 0 || offset >= (int64_t)g_clip_length) {
        return 0;
    }
    return (unsigned char)g_clip[offset];
}

int64_t aura_cmd_image(int64_t handle, int64_t image, double x, double y, double w, double h) {
    if (!require_window(handle)) {
        return -1;
    }
    return aura_paint_image(image, x, y, w, h);
}

int64_t aura_plugin_place(int64_t handle, int64_t plugin, double x, double y, double w, double h) {
    (void)plugin;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    if (!require_window(handle)) {
        return -1;
    }
    return 0;
}

int64_t aura_ax_begin(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    snapshot_ax_ids(window);
    clear_ax(window);
    window->ax_open = 1;
    pthread_mutex_unlock(&window->mu);
    return 0;
}

int64_t aura_ax_set_node(int64_t handle, int64_t node_id, int64_t heading) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (!window->ax_open || window->ax_count <= 0) {
        pthread_mutex_unlock(&window->mu);
        return -1;
    }
    window->ax[window->ax_count - 1].node_id = node_id;
    window->ax[window->ax_count - 1].heading = heading != 0;
    pthread_mutex_unlock(&window->mu);
    return 0;
}

int64_t aura_ax_add(int64_t handle, const char *role, const char *label, const char *value, int64_t checked, double x, double y, double w, double h, int64_t press) {
    AuraWindow *window = window_get(handle, 1);
    AuraAx *node = NULL;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (!window->ax_open) {
        pthread_mutex_unlock(&window->mu);
        return -1;
    }
    if (window->ax_count >= window->ax_cap) {
        int cap = window->ax_cap == 0 ? 16 : window->ax_cap * 2;
        AuraAx *next = (AuraAx *)realloc(window->ax, (size_t)cap * sizeof(AuraAx));
        if (next == NULL) {
            pthread_mutex_unlock(&window->mu);
            return -1;
        }
        window->ax = next;
        window->ax_cap = cap;
    }
    node = &window->ax[window->ax_count];
    memset(node, 0, sizeof(*node));
    node->role = dup_text(role);
    node->label = dup_text(label);
    node->value = dup_text(value);
    node->checked = checked != 0;
    node->press = press;
    node->x = x;
    node->y = y;
    node->w = w;
    node->h = h;
    if (node->role == NULL || node->label == NULL || node->value == NULL) {
        free(node->role);
        free(node->label);
        free(node->value);
        pthread_mutex_unlock(&window->mu);
        return -1;
    }
    window->ax_count += 1;
    pthread_mutex_unlock(&window->mu);
    return 0;
}

int64_t aura_ax_commit(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    int count = 0;
    int change = 2;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    window->ax_open = 0;
    count = window->ax_count;
    if (window->ax_published) {
        change = ax_ids_changed(window);
    }
    window->ax_published = 1;
    pthread_mutex_unlock(&window->mu);
    aura_host_accessibility_update((int)handle - 1, change);
    return count;
}

int64_t aura_ax_count(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    int count = 0;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    count = window->ax_count;
    pthread_mutex_unlock(&window->mu);
    return count;
}

int64_t aura_ax_text_len(int64_t handle, int64_t index, int64_t field) {
    const char *text = "";
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    if (field == 0) {
        text = aura_shell_ax_role((int)handle - 1, (int)index);
    } else if (field == 1) {
        text = aura_shell_ax_label((int)handle - 1, (int)index);
    } else {
        text = aura_shell_ax_value((int)handle - 1, (int)index);
    }
    free(g_ax_query);
    g_ax_query = dup_text(text);
    if (g_ax_query == NULL) {
        return 0;
    }
    return (int64_t)strlen(g_ax_query);
}

int64_t aura_ax_text_byte(int64_t offset) {
    if (g_ax_query == NULL || offset < 0 || offset >= (int64_t)strlen(g_ax_query)) {
        return 0;
    }
    return (unsigned char)g_ax_query[offset];
}

int64_t aura_ax_checked(int64_t handle, int64_t index) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_shell_ax_checked((int)handle - 1, (int)index);
}

int64_t aura_ax_press(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    AuraAx *node = NULL;
    int64_t press = -1;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    node = ax_at(window, index);
    if (node != NULL) {
        press = node->press;
    }
    pthread_mutex_unlock(&window->mu);
    return press;
}

double aura_ax_x(int64_t handle, int64_t index) {
    double x = -1.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    if (window_get(handle, 1) == NULL) {
        return -1.0;
    }
    aura_shell_ax_frame((int)handle - 1, (int)index, &x, &y, &w, &h);
    return x;
}

double aura_ax_y(int64_t handle, int64_t index) {
    double x = 0.0;
    double y = -1.0;
    double w = 0.0;
    double h = 0.0;
    if (window_get(handle, 1) == NULL) {
        return -1.0;
    }
    aura_shell_ax_frame((int)handle - 1, (int)index, &x, &y, &w, &h);
    return y;
}

double aura_ax_w(int64_t handle, int64_t index) {
    double x = 0.0;
    double y = 0.0;
    double w = -1.0;
    double h = 0.0;
    if (window_get(handle, 1) == NULL) {
        return -1.0;
    }
    aura_shell_ax_frame((int)handle - 1, (int)index, &x, &y, &w, &h);
    return w;
}

double aura_ax_h(int64_t handle, int64_t index) {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = -1.0;
    if (window_get(handle, 1) == NULL) {
        return -1.0;
    }
    aura_shell_ax_frame((int)handle - 1, (int)index, &x, &y, &w, &h);
    return h;
}

int64_t aura_post_ax(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    push_event(window, AURA_AX_ACTION, 0, 0, index, 0, NULL);
    pthread_mutex_unlock(&window->mu);
    return 0;
}

int64_t aura_set_cursor(int64_t handle, int64_t kind) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    window->cursor_kind = (int)kind;
    pthread_mutex_unlock(&window->mu);
    return 0;
}

int64_t aura_window_title_len(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    int64_t length = 0;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    if (window->title != NULL) {
        length = (int64_t)strlen(window->title);
    }
    pthread_mutex_unlock(&window->mu);
    return length;
}

int64_t aura_window_title_byte(int64_t handle, int64_t offset) {
    AuraWindow *window = window_get(handle, 1);
    int64_t value = 0;
    if (window == NULL) {
        return 0;
    }
    pthread_mutex_lock(&window->mu);
    if (window->title != NULL && offset >= 0 && offset < (int64_t)strlen(window->title)) {
        value = (unsigned char)window->title[offset];
    }
    pthread_mutex_unlock(&window->mu);
    return value;
}

int64_t aura_cursor(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    int64_t kind = -1;
    if (window == NULL) {
        return -1;
    }
    pthread_mutex_lock(&window->mu);
    kind = window->cursor_kind;
    pthread_mutex_unlock(&window->mu);
    return kind;
}
