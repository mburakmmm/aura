#include "ax_platform.h"
#include "shell.h"

#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int64_t aura_window_create(int64_t width, int64_t height, const char *title, int64_t visible);
int64_t aura_window_destroy(int64_t handle);
int64_t aura_window_poll(int64_t handle);
int64_t aura_window_event_count(int64_t handle);
int64_t aura_window_event_kind(int64_t handle, int64_t index);
int64_t aura_window_event_key(int64_t handle, int64_t index);
double aura_window_event_x(int64_t handle, int64_t index);
double aura_window_event_y(int64_t handle, int64_t index);
int64_t aura_ax_set_node(int64_t handle, int64_t node_id, int64_t heading);
int aura_shell_ax_heading(int slot, int index);
int64_t aura_shell_ax_node_id(int slot, int index);
int aura_shell_ax_adjust(int slot, int index, int direction);
int aura_test_ax_change(void);
int64_t aura_window_event_text_len(int64_t handle, int64_t index);
int64_t aura_window_event_text_byte(int64_t handle, int64_t index, int64_t offset);
double aura_window_event_metrics_value(int64_t handle, int64_t index, int64_t field);
double aura_window_event_environment_value(int64_t handle, int64_t index, int64_t field);
int64_t aura_window_event_memory_level(int64_t handle, int64_t index);
double aura_window_event_cutout_value(int64_t handle, int64_t index, int64_t rect_index, int64_t field);
int aura_shell_environment(int slot, const AuraEnvironmentPayload *environment, const char *locales, uint64_t timestamp_ns);
void aura_shell_memory_pressure(int slot, int level, uint64_t timestamp_ns);
void aura_shell_text_edit_n(int slot, int operation, const char *text, size_t length, int64_t first, int64_t second, uint64_t timestamp_ns);
int64_t aura_window_event_lifecycle_state(int64_t handle, int64_t index);
int64_t aura_window_event_lifecycle_reason(int64_t handle, int64_t index);
int64_t aura_window_event_surface_value(int64_t handle, int64_t index, int64_t field);
int64_t aura_window_event_text_edit_value(int64_t handle, int64_t index, int64_t field);
int64_t aura_window_surface_available(int64_t handle);
int64_t aura_window_event_device_id(int64_t handle, int64_t index);
int64_t aura_window_event_pointer_id(int64_t handle, int64_t index);
int64_t aura_window_event_pointer_kind(int64_t handle, int64_t index);
int64_t aura_window_event_timestamp_ns(int64_t handle, int64_t index);
int64_t aura_window_events_clear(int64_t handle);
int64_t aura_post_mouse(int64_t handle, int64_t kind, double x, double y, int64_t button);
int64_t aura_post_text(int64_t handle, const char *utf8);
int64_t aura_window_system_back_result(int64_t handle, int64_t handled);
int64_t aura_window_capabilities(int64_t handle);
int64_t aura_text_focus(int64_t handle, int64_t enabled);
int64_t aura_clipboard_set(const char *text);
int64_t aura_clipboard_len(void);
void aura_test_clipboard_fail(int fail);
int64_t aura_text_editing_state(int64_t handle, const char *text, int64_t generation, int64_t revision, int64_t selection_start, int64_t selection_end, int64_t composing_start, int64_t composing_end);
int64_t aura_text_geometry(int64_t handle, double caret_x, double caret_y, double caret_width, double caret_height, double editable_x, double editable_y, double editable_width, double editable_height);
int64_t aura_text_geometry_begin(int64_t handle, int64_t target, int64_t revision, int64_t count);
int64_t aura_text_geometry_boundary(int64_t handle, int64_t target, int64_t revision, int64_t index, int64_t offset, double x, double y, double height);
int64_t aura_text_geometry_end(int64_t handle, int64_t target, int64_t revision);
int64_t aura_text_configuration(int64_t handle, int64_t input_type, int64_t action, int64_t multiline, int64_t secure, int64_t autocorrect, int64_t capitalization, const char *autofill_hints);
int aura_test_system_back_slot(void);
int aura_test_system_back_handled(void);
int64_t aura_test_text_generation(void);
int64_t aura_test_text_revision(void);
int64_t aura_test_text_selection_start(void);
int64_t aura_test_text_selection_end(void);
double aura_test_text_caret_x(void);
double aura_test_text_editable_width(void);
int aura_test_text_input_type(void);
int aura_test_text_action(void);
int aura_test_text_secure(void);
int aura_test_text_capitalization(void);
int64_t aura_test_text_snapshot_target(void);
int64_t aura_test_text_snapshot_revision(void);
int aura_test_text_snapshot_count(void);
int64_t aura_ax_begin(int64_t handle);
int64_t aura_ax_add(int64_t handle, const char *role, const char *label, const char *value, int64_t checked, double x, double y, double w, double h, int64_t press);
int64_t aura_ax_commit(int64_t handle);

static int expect_role(const char *role, const char *ios_trait, const char *android_class) {
    const char *trait = aura_ax_ios_trait(role);
    const char *klass = aura_ax_android_class(role);
    return trait != NULL && klass != NULL && strcmp(trait, ios_trait) == 0 && strcmp(klass, android_class) == 0;
}

int main(void) {
    int64_t handle = 0;
    int64_t route_handle = 0;
    int64_t count = 0;
    int i = 0;
    if (aura_shell_surface(0, AURA_SURFACE_CREATED, 7, 960, 1920, AURA_SURFACE_FORMAT_RGBA8888, 7, 424242424) != 0) return 1;
    handle = aura_window_create(320, 640, "queue", 0);
    if (handle <= 0) return 2;
    if ((aura_window_capabilities(handle) & AURA_CAPABILITY_INTERACTIVE_EDGE_BACK) == 0) return 31;
    aura_shell_platform_route(1, AURA_ROUTE_UNIVERSAL_LINK, "https://example.test/cold", 777777777);
    route_handle = aura_window_create(320, 640, "route", 0);
    if (route_handle != 2) return 32;
    if (aura_window_poll(route_handle) != 1) return 33;
    if (aura_window_event_kind(route_handle, 0) != AURA_PLATFORM_ROUTE) return 34;
    if (aura_window_event_key(route_handle, 0) != AURA_ROUTE_UNIVERSAL_LINK) return 35;
    if (aura_window_event_text_len(route_handle, 0) != 25) return 36;
    if (aura_window_event_timestamp_ns(route_handle, 0) != 777777777) return 107;
    if (aura_window_destroy(route_handle) != 0) return 37;
    if (aura_window_poll(handle) != 1) return 3;
    if (aura_window_event_kind(handle, 0) != AURA_SURFACE_CREATED) return 4;
    if (aura_window_event_surface_value(handle, 0, AURA_SURFACE_FIELD_GENERATION) != 7) return 5;
    if (aura_window_event_timestamp_ns(handle, 0) != 424242424) return 106;
    if (aura_window_surface_available(handle) != 1) return 6;
    aura_window_events_clear(handle);
    if (aura_shell_surface(0, AURA_SURFACE_CHANGED, 7, 1080, 1920, AURA_SURFACE_FORMAT_RGBA8888, 7, 666666666) != 0) return 7;
    if (aura_shell_surface(0, AURA_SURFACE_DESTROYED, 6, 0, 0, AURA_SURFACE_FORMAT_RGBA8888, 6, 0) == 0) return 8;
    if (aura_window_poll(handle) != 1) return 9;
    if (aura_window_event_kind(handle, 0) != AURA_SURFACE_CHANGED) return 10;
    if (aura_window_event_surface_value(handle, 0, AURA_SURFACE_FIELD_PHYSICAL_WIDTH) != 1080) return 11;
    if (aura_window_event_timestamp_ns(handle, 0) != 666666666) return 108;
    aura_window_events_clear(handle);
    if (aura_shell_surface(0, AURA_SURFACE_DESTROYED, 7, 0, 0, AURA_SURFACE_FORMAT_RGBA8888, 7, 0) != 0) return 12;
    if (aura_window_surface_available(handle) != 0) return 13;
    if (aura_window_poll(handle) != 1 || aura_window_event_kind(handle, 0) != AURA_SURFACE_DESTROYED) return 14;
    aura_window_events_clear(handle);
    if (aura_shell_surface(0, AURA_SURFACE_CREATED, 7, 1080, 1920, AURA_SURFACE_FORMAT_RGBA8888, 7, 0) == 0) return 15;
    if (aura_shell_surface(0, AURA_SURFACE_CREATED, 8, 1080, 1920, AURA_SURFACE_FORMAT_RGBA8888, 8, 0) != 0) return 16;
    if (aura_window_poll(handle) != 1 || aura_window_event_kind(handle, 0) != AURA_SURFACE_CREATED) return 17;
    aura_window_events_clear(handle);
    if (aura_post_mouse(handle, AURA_POINTER_DOWN, 1.0, 2.0, 1) != 0) return 2;
    aura_post_mouse(handle, AURA_POINTER_MOVE, 10.0, 2.0, 1);
    aura_post_mouse(handle, AURA_POINTER_MOVE, 20.0, 2.0, 1);
    aura_post_text(handle, "ok");
    count = aura_window_poll(handle);
    if (count != 3) return 3;
    if (aura_window_event_count(handle) != 3) return 4;
    if (aura_window_event_kind(handle, 0) != AURA_POINTER_DOWN) return 5;
    if (aura_window_event_kind(handle, 1) != AURA_POINTER_MOVE) return 6;
    if (aura_window_event_x(handle, 1) != 20.0) return 7;
    if (aura_window_event_kind(handle, 2) != AURA_TEXT) return 8;
    if (aura_window_event_text_len(handle, 2) != 2) return 9;
    if (aura_window_event_text_byte(handle, 2, 0) != 'o') return 10;

    aura_post_mouse(handle, AURA_POINTER_UP, 20.0, 2.0, 1);
    if (aura_window_event_count(handle) != 3) return 11;
    aura_window_events_clear(handle);
    if (aura_window_poll(handle) != 1) return 12;
    if (aura_window_event_kind(handle, 0) != AURA_POINTER_UP) return 13;
    aura_window_events_clear(handle);

    if (aura_text_focus(handle, 1) != 0) return 42;
    if (aura_text_editing_state(handle, "hello", 3, 7, 2, 4, -1, -1) != 0) return 43;
    if (aura_test_text_generation() != 3 || aura_test_text_revision() != 7) return 56;
    if (aura_test_text_selection_start() != 2 || aura_test_text_selection_end() != 4) return 57;
    if (aura_text_geometry(handle, 12.5, 20.0, 1.0, 18.0, 4.0, 8.0, 300.0, 40.0) != 0) return 58;
    if (aura_test_text_caret_x() != 12.5 || aura_test_text_editable_width() != 300.0) return 59;
    if (aura_text_geometry_begin(handle, 71, 1, 3) != 1) return 67;
    if (aura_text_geometry_boundary(handle, 71, 1, 0, 0, 8.0, 8.0, 18.0) != 0) return 68;
    if (aura_text_geometry_end(handle, 71, 1) != -1) return 69;
    if (aura_test_text_snapshot_count() != -1) return 70;
    if (aura_text_geometry_begin(handle, 71, 1, 3) != 1) return 71;
    if (aura_text_geometry_boundary(handle, 71, 1, 0, 0, 8.0, 8.0, 18.0) != 0) return 72;
    if (aura_text_geometry_boundary(handle, 71, 1, 1, 2, 20.0, 8.0, 18.0) != 0) return 73;
    if (aura_text_geometry_boundary(handle, 71, 1, 2, 5, 8.0, 26.0, 18.0) != 0) return 74;
    if (aura_text_geometry_end(handle, 71, 1) != 0) return 75;
    if (aura_test_text_snapshot_target() != 71 || aura_test_text_snapshot_revision() != 1 || aura_test_text_snapshot_count() != 3) return 76;
    {
        int64_t offset = -1;
        double x = 0.0;
        double y = 0.0;
        double height = 0.0;
        if (aura_shell_text_geometry_boundary(0, 1, &offset, &x, &y, &height) != 0) return 77;
        if (offset != 2 || x != 20.0 || y != 8.0 || height != 18.0) return 78;
    }
    if (aura_text_geometry_begin(handle, 71, 1, 3) != 0) return 79;
    if (aura_text_geometry_begin(handle, 71, 0, 3) != -1) return 80;
    if (aura_text_geometry_begin(handle, 71, 2, 1) != 1) return 81;
    if (aura_text_geometry_boundary(handle, 71, 2, 0, 0, NAN, 0.0, 18.0) != -1) return 82;
    if (aura_text_geometry_end(handle, 71, 2) != -1) return 83;
    if (aura_text_configuration(handle, 2, 5, 0, 1, 0, 2, "username\nemailAddress") != 0) return 60;
    if (aura_test_text_input_type() != 2 || aura_test_text_action() != 5 || aura_test_text_secure() != 1 || aura_test_text_capitalization() != 2) return 61;
    aura_shell_text_edit(0, AURA_TEXT_EDIT_SET_SELECTION, "", 1, 5, 0);
    if (aura_window_poll(handle) != 1) return 44;
    if (aura_window_event_kind(handle, 0) != AURA_TEXT_EDIT) return 45;
    if (aura_window_event_text_edit_value(handle, 0, AURA_TEXT_EDIT_FIELD_OPERATION) != AURA_TEXT_EDIT_SET_SELECTION) return 46;
    if (aura_window_event_text_edit_value(handle, 0, AURA_TEXT_EDIT_FIELD_GENERATION) != 3) return 47;
    if (aura_window_event_text_edit_value(handle, 0, AURA_TEXT_EDIT_FIELD_REVISION) != 8) return 48;
    if (aura_window_event_text_edit_value(handle, 0, AURA_TEXT_EDIT_FIELD_FIRST) != 1) return 49;
    if (aura_window_event_text_edit_value(handle, 0, AURA_TEXT_EDIT_FIELD_SECOND) != 5) return 50;
    aura_window_events_clear(handle);
    if (aura_text_editing_state(handle, "new", 4, 2, 3, 3, -1, -1) != 0) return 51;
    aura_shell_text_edit(0, AURA_TEXT_EDIT_COMMIT, "!", 0, 0, 0);
    if (aura_window_poll(handle) != 1) return 52;
    if (aura_window_event_text_edit_value(handle, 0, AURA_TEXT_EDIT_FIELD_GENERATION) != 4) return 53;
    if (aura_window_event_text_edit_value(handle, 0, AURA_TEXT_EDIT_FIELD_REVISION) != 3) return 54;
    if (aura_window_event_text_len(handle, 0) != 1 || aura_window_event_text_byte(handle, 0, 0) != '!') return 55;
    aura_window_events_clear(handle);
    aura_shell_text_edit(0, AURA_TEXT_EDIT_PERFORM_ACTION, "", 5, 0, 0);
    if (aura_window_poll(handle) != 1) return 62;
    if (aura_window_event_text_edit_value(handle, 0, AURA_TEXT_EDIT_FIELD_OPERATION) != AURA_TEXT_EDIT_PERFORM_ACTION) return 63;
    if (aura_window_event_text_edit_value(handle, 0, AURA_TEXT_EDIT_FIELD_FIRST) != 5) return 64;
    aura_window_events_clear(handle);

    aura_test_clipboard_fail(1);
    if (aura_clipboard_set("unavailable") != -1) return 65;
    if (aura_clipboard_len() != -1) return 66;
    aura_test_clipboard_fail(0);

    {
        AuraPlatformEvent identity;
        aura_platform_event_init(&identity, AURA_POINTER_MOVE, 0, 123456789);
        identity.device_id = 44;
        identity.pointer_id = 55;
        identity.pointer_kind = AURA_POINTER_KIND_STYLUS;
        aura_shell_post_event(0, &identity);
        if (aura_window_poll(handle) != 1) return 26;
        if (aura_window_event_device_id(handle, 0) != 44) return 27;
        if (aura_window_event_pointer_id(handle, 0) != 55) return 28;
        if (aura_window_event_pointer_kind(handle, 0) != AURA_POINTER_KIND_STYLUS) return 29;
        if (aura_window_event_timestamp_ns(handle, 0) != 123456789) return 30;
        aura_window_events_clear(handle);
    }
    aura_shell_platform_route(0, AURA_ROUTE_INTENT, "aura://warm", 888888888);
    if (aura_window_poll(handle) != 1) return 38;
    if (aura_window_event_kind(handle, 0) != AURA_PLATFORM_ROUTE) return 39;
    if (aura_window_event_key(handle, 0) != AURA_ROUTE_INTENT) return 40;
    if (aura_window_event_text_len(handle, 0) != 11) return 41;
    if (aura_window_event_timestamp_ns(handle, 0) != 888888888) return 109;
    aura_window_events_clear(handle);

    {
        AuraMetricsPayload metrics = {0};
        metrics.logical_width = 390.0;
        metrics.logical_height = 844.0;
        metrics.physical_width = 1170;
        metrics.physical_height = 2532;
        metrics.device_pixel_ratio = 3.0;
        metrics.refresh_rate = 120.0;
        metrics.view_padding.top = 47.0;
        metrics.view_insets.bottom = 301.0;
        metrics.font_scale = 1.2;
        metrics.rotation_degrees = 90;
        metrics.keyboard_duration_s = 0.25;
        metrics.keyboard_curve = AURA_KEYBOARD_CURVE_EASE_IN_OUT;
        metrics.cutout_count = 1;
        metrics.cutouts[0].x = 12.0;
        metrics.cutouts[0].y = 0.0;
        metrics.cutouts[0].width = 36.0;
        metrics.cutouts[0].height = 18.0;
        aura_shell_metrics_v1(0, &metrics, 222222222);
        if (aura_window_poll(handle) != 1) return 14;
        if (aura_window_event_kind(handle, 0) != AURA_METRICS) return 15;
        if (aura_window_event_metrics_value(handle, 0, AURA_METRIC_LOGICAL_WIDTH) != 390.0) return 16;
        if (aura_window_event_metrics_value(handle, 0, AURA_METRIC_VIEW_PADDING_TOP) != 47.0) return 17;
        if (aura_window_event_metrics_value(handle, 0, AURA_METRIC_VIEW_INSET_BOTTOM) != 301.0) return 18;
        if (aura_window_event_metrics_value(handle, 0, AURA_METRIC_ROTATION_DEGREES) != 90.0) return 84;
        if (aura_window_event_metrics_value(handle, 0, AURA_METRIC_KEYBOARD_DURATION) != 0.25) return 85;
        if (aura_window_event_metrics_value(handle, 0, AURA_METRIC_KEYBOARD_CURVE) != AURA_KEYBOARD_CURVE_EASE_IN_OUT) return 86;
        if (aura_window_event_metrics_value(handle, 0, AURA_METRIC_CUTOUT_COUNT) != 1.0) return 87;
        if (aura_window_event_cutout_value(handle, 0, 0, AURA_CUTOUT_X) != 12.0) return 88;
        if (aura_window_event_cutout_value(handle, 0, 0, AURA_CUTOUT_HEIGHT) != 18.0) return 89;
        if (aura_window_event_timestamp_ns(handle, 0) != 222222222) return 110;
        aura_window_events_clear(handle);
    }

    {
        AuraEnvironmentPayload environment = {0};
        char raw[3] = {'a', '\0', 'b'};
        environment.brightness = 1;
        environment.high_contrast = 1;
        environment.reduce_motion = 1;
        environment.accessible_navigation = 1;
        environment.text_direction = 1;
        environment.bold_text = 1;
        environment.font_scale = 1.35;
        aura_shell_environment(0, &environment, "tr\nen", 333333333);
        if (aura_window_poll(handle) != 1) return 90;
        if (aura_window_event_kind(handle, 0) != AURA_ENVIRONMENT_CHANGED) return 91;
        if (aura_window_event_environment_value(handle, 0, AURA_ENV_BRIGHTNESS) != 1.0) return 92;
        if (aura_window_event_environment_value(handle, 0, AURA_ENV_BOLD_TEXT) != 1.0) return 93;
        if (aura_window_event_environment_value(handle, 0, AURA_ENV_FONT_SCALE) != 1.35) return 94;
        if (aura_window_event_text_len(handle, 0) != 5) return 95;
        if (aura_window_event_text_byte(handle, 0, 2) != '\n') return 96;
        if (aura_window_event_timestamp_ns(handle, 0) != 333333333) return 111;
        aura_window_events_clear(handle);
        aura_shell_memory_pressure(0, AURA_MEMORY_PRESSURE_CRITICAL, 444444444);
        if (aura_window_poll(handle) != 1) return 97;
        if (aura_window_event_kind(handle, 0) != AURA_MEMORY_PRESSURE) return 98;
        if (aura_window_event_memory_level(handle, 0) != AURA_MEMORY_PRESSURE_CRITICAL) return 99;
        if (aura_window_event_timestamp_ns(handle, 0) != 444444444) return 112;
        aura_window_events_clear(handle);
        aura_shell_memory_pressure(0, AURA_MEMORY_PRESSURE_NONE, 0);
        if (aura_window_poll(handle) != 0) return 100;
        aura_shell_text_edit_n(0, AURA_TEXT_EDIT_COMMIT, raw, 3, 0, 0, 555555555);
        if (aura_window_poll(handle) != 1) return 101;
        if (aura_window_event_text_len(handle, 0) != 3) return 102;
        if (aura_window_event_text_byte(handle, 0, 0) != 'a') return 103;
        if (aura_window_event_text_byte(handle, 0, 1) != 0) return 104;
        if (aura_window_event_text_byte(handle, 0, 2) != 'b') return 105;
        if (aura_window_event_timestamp_ns(handle, 0) != 555555555) return 113;
        aura_window_events_clear(handle);
    }

    aura_shell_lifecycle(0, AURA_LIFECYCLE_PAUSED, AURA_LIFECYCLE_REASON_SYSTEM, 111111111);
    if (aura_window_poll(handle) != 1) return 19;
    if (aura_window_event_kind(handle, 0) != AURA_LIFECYCLE) return 20;
    if (aura_window_event_lifecycle_state(handle, 0) != AURA_LIFECYCLE_PAUSED) return 21;
    if (aura_window_event_lifecycle_reason(handle, 0) != AURA_LIFECYCLE_REASON_SYSTEM) return 22;
    if (aura_window_event_timestamp_ns(handle, 0) != 111111111) return 114;
    aura_window_events_clear(handle);
    aura_shell_lifecycle(0, AURA_LIFECYCLE_RESUMED, AURA_LIFECYCLE_REASON_SYSTEM, 0);
    if (aura_window_poll(handle) != 1) return 115;
    if (aura_window_event_timestamp_ns(handle, 0) == 0) return 116;
    aura_window_events_clear(handle);

    {
        AuraPlatformEvent back = {0};
        aura_platform_event_init(&back, AURA_SYSTEM_BACK, 0, 987654321);
        back.key_code = AURA_BACK_PHASE_COMMIT;
        back.x = 1.0;
        aura_shell_post_event(0, &back);
        if (aura_window_poll(handle) != 1) return 31;
        if (aura_window_event_kind(handle, 0) != AURA_SYSTEM_BACK) return 32;
        aura_window_events_clear(handle);
        if (aura_window_system_back_result(handle, 1) != 0) return 33;
        if (aura_test_system_back_slot() != 0 || aura_test_system_back_handled() != 1) return 34;
    }

    if (aura_ax_begin(handle) != 0) return 120;
    if (aura_ax_add(handle, "text", "Title", "", 0, 0, 0, 10, 10, -1) != 0) return 121;
    if (aura_ax_set_node(handle, 41, 1) != 0) return 121;
    if (aura_ax_add(handle, "button", "Save", "Go", 0, 0, 20, 40, 40, 7) != 0) return 122;
    if (aura_ax_set_node(handle, 42, 0) != 0) return 122;
    if (aura_ax_commit(handle) < 0) return 123;
    if (aura_test_ax_change() != 2) return 123;
    if (aura_shell_ax_node_id(0, 0) != 41 || aura_shell_ax_heading(0, 0) != 1) return 123;
    if (aura_shell_ax_node_id(0, 1) != 42 || aura_shell_ax_heading(0, 1) != 0) return 123;
    if (aura_shell_activate_ax(0, 0) != 0) return 124;
    if (aura_shell_activate_ax(0, 1) != 1) return 125;
    if (aura_window_poll(handle) != 1) return 126;
    if (aura_window_event_kind(handle, 0) != AURA_AX_ACTION || aura_window_event_key(handle, 0) != 7) return 127;
    aura_window_events_clear(handle);
    if (aura_ax_begin(handle) != 0) return 137;
    if (aura_ax_add(handle, "text", "Title", "", 0, 0, 0, 10, 10, -1) != 0) return 137;
    if (aura_ax_set_node(handle, 41, 1) != 0) return 137;
    if (aura_ax_add(handle, "button", "Save", "Go", 0, 0, 20, 40, 40, 7) != 0) return 137;
    if (aura_ax_set_node(handle, 42, 0) != 0) return 137;
    if (aura_ax_commit(handle) < 0 || aura_test_ax_change() != 0) return 138;
    if (aura_ax_begin(handle) != 0) return 139;
    if (aura_ax_add(handle, "text", "Title", "changed", 0, 0, 0, 10, 10, -1) != 0) return 139;
    if (aura_ax_set_node(handle, 99, 0) != 0) return 139;
    if (aura_ax_commit(handle) < 0 || aura_test_ax_change() != 1) return 140;
    if (aura_shell_ax_adjust(0, 0, 1) != 1) return 141;
    if (aura_window_poll(handle) != 1) return 142;
    if (aura_window_event_kind(handle, 0) != AURA_AX_ACTION || aura_window_event_key(handle, 0) != -1) return 143;
    if (aura_window_event_x(handle, 0) != 1.0 || aura_window_event_y(handle, 0) != 99.0) return 144;
    aura_window_events_clear(handle);
    if (!expect_role("button", "button", "android.widget.Button")) return 128;
    if (!expect_role("toggle", "button", "android.widget.Switch")) return 129;
    if (!expect_role("text", "static-text", "android.widget.TextView")) return 130;
    if (!expect_role("text-field", "text-field", "android.widget.EditText")) return 131;
    if (!expect_role("image", "image", "android.widget.ImageView")) return 132;
    if (!expect_role("scroll-view", "none", "android.widget.ScrollView")) return 133;
    if (!expect_role("adjustable", "adjustable", "android.widget.SeekBar")) return 134;
    if (!expect_role("unknown", "none", "android.view.View")) return 135;
    if (strcmp(aura_ax_ios_trait(NULL), "none") != 0 || strcmp(aura_ax_android_class(NULL), "android.view.View") != 0) return 136;

    for (i = 0; i < 1030; i++) {
        aura_post_text(handle, "x");
    }
    count = aura_window_poll(handle);
    if (count != 1025) return 23;
    if (aura_window_event_kind(handle, count - 1) != AURA_QUEUE_OVERFLOW) return 24;
    aura_window_events_clear(handle);
    if (aura_window_destroy(handle) != 0) return 25;
    printf("mobile shell queue ok\n");
    return 0;
}
