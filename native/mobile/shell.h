#ifndef AURA_MOBILE_SHELL_H
#define AURA_MOBILE_SHELL_H

#include <stddef.h>
#include <stdint.h>

#include "events.h"

#define AURA_CAPABILITY_INTERACTIVE_EDGE_BACK 1

/* Platform side. The Nox thread calls these; implementations hop to the UI thread. */

void aura_host_open(int slot, double width, double height, const char *title, int visible);
void aura_host_close(int slot);
void aura_host_present(int slot);
void aura_host_accessibility(int slot);
void aura_host_accessibility_update(int slot, int change);
void aura_host_text_focus(int slot, int enabled);
void aura_host_text_state(int slot, const char *text, int64_t generation, int64_t revision, int64_t selection_start, int64_t selection_end, int64_t composing_start, int64_t composing_end);
void aura_host_text_geometry(int slot, double caret_x, double caret_y, double caret_width, double caret_height, double editable_x, double editable_y, double editable_width, double editable_height);
void aura_host_text_geometry_snapshot(int slot, int64_t target, int64_t revision, int count);
void aura_host_text_configuration(int slot, int input_type, int action, int multiline, int secure, int autocorrect, int capitalization, const char *autofill_hints);
void aura_host_system_back_result(int slot, int handled);
int aura_host_capabilities(int slot);
int aura_host_clipboard_set(const char *text);
const char *aura_host_clipboard_get(void);
size_t aura_host_clipboard_length(void);

/* UI thread side. slot is the zero-based window index. */

void aura_shell_metrics(int slot, double width, double height, double scale, uint64_t timestamp_ns);
void aura_shell_metrics_v1(int slot, const AuraMetricsPayload *metrics, uint64_t timestamp_ns);
int aura_shell_environment(int slot, const AuraEnvironmentPayload *environment, const char *locales, uint64_t timestamp_ns);
void aura_shell_memory_pressure(int slot, int level, uint64_t timestamp_ns);
void aura_shell_lifecycle(int slot, int state, int reason, uint64_t timestamp_ns);
int aura_shell_surface(int slot, int kind, int64_t surface_id, int64_t physical_width, int64_t physical_height, int format, int generation, uint64_t timestamp_ns);
void aura_shell_request_close_all(void);
void aura_shell_vsync(int slot);
void aura_shell_post(int slot, int kind, double x, double y, int64_t key, int64_t button, const char *text);
void aura_shell_post_event(int slot, const AuraPlatformEvent *event);
void aura_shell_platform_route(int slot, int source, const char *location, uint64_t timestamp_ns);
void aura_shell_text_edit(int slot, int operation, const char *text, int64_t first, int64_t second, uint64_t timestamp_ns);
void aura_shell_text_edit_n(int slot, int operation, const char *text, size_t length, int64_t first, int64_t second, uint64_t timestamp_ns);
int aura_shell_ax_count(int slot);
const char *aura_shell_ax_role(int slot, int index);
const char *aura_shell_ax_label(int slot, int index);
const char *aura_shell_ax_value(int slot, int index);
int aura_shell_ax_checked(int slot, int index);
int64_t aura_shell_ax_press(int slot, int index);
int aura_shell_ax_heading(int slot, int index);
int64_t aura_shell_ax_node_id(int slot, int index);
int aura_shell_activate_ax(int slot, int index);
int aura_shell_ax_adjust(int slot, int index, int direction);
void aura_shell_ax_frame(int slot, int index, double *x, double *y, double *w, double *h);
int aura_shell_copy_pixels(int slot, uint8_t **out, int *width, int *height, int *stride);
int aura_shell_text_geometry_boundary(int slot, int index, int64_t *offset, double *x, double *y, double *height);

#endif
