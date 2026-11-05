#include "shell.h"
#include "paint.h"

#include <stdint.h>

void aura_host_open(int slot, double width, double height, const char *title, int visible) {
    (void)slot;
    (void)width;
    (void)height;
    (void)title;
    (void)visible;
}

void aura_host_close(int slot) { (void)slot; }
void aura_host_present(int slot) { (void)slot; }
void aura_host_accessibility(int slot) { (void)slot; }
static int g_ax_change = -1;
void aura_host_accessibility_update(int slot, int change) {
    (void)slot;
    g_ax_change = change;
}
int aura_test_ax_change(void) { return g_ax_change; }
void aura_host_text_focus(int slot, int enabled) { (void)slot; (void)enabled; }
static int64_t g_text_generation = -1;
static int64_t g_text_revision = -1;
static int64_t g_text_selection_start = -1;
static int64_t g_text_selection_end = -1;
void aura_host_text_state(int slot, const char *text, int64_t generation, int64_t revision, int64_t selection_start, int64_t selection_end, int64_t composing_start, int64_t composing_end) {
    (void)slot;
    (void)text;
    (void)composing_start;
    (void)composing_end;
    g_text_generation = generation;
    g_text_revision = revision;
    g_text_selection_start = selection_start;
    g_text_selection_end = selection_end;
}
int64_t aura_test_text_generation(void) { return g_text_generation; }
int64_t aura_test_text_revision(void) { return g_text_revision; }
int64_t aura_test_text_selection_start(void) { return g_text_selection_start; }
int64_t aura_test_text_selection_end(void) { return g_text_selection_end; }
static double g_text_caret_x = -1.0;
static double g_text_editable_width = -1.0;
void aura_host_text_geometry(int slot, double caret_x, double caret_y, double caret_width, double caret_height, double editable_x, double editable_y, double editable_width, double editable_height) {
    (void)slot;
    (void)caret_y;
    (void)caret_width;
    (void)caret_height;
    (void)editable_x;
    (void)editable_y;
    (void)editable_height;
    g_text_caret_x = caret_x;
    g_text_editable_width = editable_width;
}
double aura_test_text_caret_x(void) { return g_text_caret_x; }
double aura_test_text_editable_width(void) { return g_text_editable_width; }
static int64_t g_text_snapshot_target = -1;
static int64_t g_text_snapshot_revision = -1;
static int g_text_snapshot_count = -1;
void aura_host_text_geometry_snapshot(int slot, int64_t target, int64_t revision, int count) {
    (void)slot;
    g_text_snapshot_target = target;
    g_text_snapshot_revision = revision;
    g_text_snapshot_count = count;
}
int64_t aura_test_text_snapshot_target(void) { return g_text_snapshot_target; }
int64_t aura_test_text_snapshot_revision(void) { return g_text_snapshot_revision; }
int aura_test_text_snapshot_count(void) { return g_text_snapshot_count; }
static int g_text_input_type = -1;
static int g_text_action = -1;
static int g_text_secure = -1;
static int g_text_capitalization = -1;
void aura_host_text_configuration(int slot, int input_type, int action, int multiline, int secure, int autocorrect, int capitalization, const char *autofill_hints) {
    (void)slot;
    (void)multiline;
    (void)autocorrect;
    (void)autofill_hints;
    g_text_input_type = input_type;
    g_text_action = action;
    g_text_secure = secure;
    g_text_capitalization = capitalization;
}
int aura_test_text_input_type(void) { return g_text_input_type; }
int aura_test_text_action(void) { return g_text_action; }
int aura_test_text_secure(void) { return g_text_secure; }
int aura_test_text_capitalization(void) { return g_text_capitalization; }
static int g_system_back_slot = -1;
static int g_system_back_handled = -1;
void aura_host_system_back_result(int slot, int handled) { g_system_back_slot = slot; g_system_back_handled = handled; }
int aura_host_capabilities(int slot) { (void)slot; return AURA_CAPABILITY_INTERACTIVE_EDGE_BACK; }
int aura_test_system_back_slot(void) { return g_system_back_slot; }
int aura_test_system_back_handled(void) { return g_system_back_handled; }
static int g_clipboard_fail;
void aura_test_clipboard_fail(int fail) { g_clipboard_fail = fail; }
int aura_host_clipboard_set(const char *text) { (void)text; return g_clipboard_fail ? -1 : 0; }
const char *aura_host_clipboard_get(void) { return g_clipboard_fail ? NULL : ""; }
size_t aura_host_clipboard_length(void) { return 0; }

int aura_paint_begin(int64_t width, int64_t height, double scale) { (void)width; (void)height; (void)scale; return 0; }
int aura_paint_clear(int64_t argb) { (void)argb; return 0; }
int aura_paint_rect(double x, double y, double w, double h, int64_t argb) { (void)x; (void)y; (void)w; (void)h; (void)argb; return 0; }
int aura_paint_rrect(double x, double y, double w, double h, double radius, int64_t argb) { (void)x; (void)y; (void)w; (void)h; (void)radius; (void)argb; return 0; }
int aura_paint_rrect_stroke(double x, double y, double w, double h, double radius, double stroke, int64_t argb) { (void)x; (void)y; (void)w; (void)h; (void)radius; (void)stroke; (void)argb; return 0; }
int aura_paint_oval(double x, double y, double w, double h, int64_t argb) { (void)x; (void)y; (void)w; (void)h; (void)argb; return 0; }
int aura_paint_line(double x, double y, double x2, double y2, double stroke, int64_t argb) { (void)x; (void)y; (void)x2; (void)y2; (void)stroke; (void)argb; return 0; }
int aura_paint_shadow(double x, double y, double w, double h, double radius, double dx, double dy, double blur, int64_t argb) { (void)x; (void)y; (void)w; (void)h; (void)radius; (void)dx; (void)dy; (void)blur; (void)argb; return 0; }
int aura_path_begin(void) { return 0; }
int aura_path_move(double x, double y) { (void)x; (void)y; return 0; }
int aura_path_line(double x, double y) { (void)x; (void)y; return 0; }
int aura_path_quad(double cx, double cy, double x, double y) { (void)cx; (void)cy; (void)x; (void)y; return 0; }
int aura_path_cubic(double c1x, double c1y, double c2x, double c2y, double x, double y) { (void)c1x; (void)c1y; (void)c2x; (void)c2y; (void)x; (void)y; return 0; }
int aura_path_close(void) { return 0; }
int aura_path_fill(int64_t argb) { (void)argb; return 0; }
int aura_path_stroke(double stroke, int64_t argb) { (void)stroke; (void)argb; return 0; }
int aura_paint_text(double x, double y, const char *text, double size, int64_t weight, int64_t argb, const char *family) { (void)x; (void)y; (void)text; (void)size; (void)weight; (void)argb; (void)family; return 0; }
int aura_paint_text_style(double x, double y, const char *text, double size, int64_t weight, int64_t argb, const char *family, int64_t italic) { (void)x; (void)y; (void)text; (void)size; (void)weight; (void)argb; (void)family; (void)italic; return 0; }
int aura_paint_opacity(double opacity) { (void)opacity; return 0; }
int aura_paint_opacity_pop(void) { return 0; }
int aura_paint_clip(double x, double y, double w, double h) { (void)x; (void)y; (void)w; (void)h; return 0; }
int aura_paint_clip_pop(void) { return 0; }
int aura_paint_image(int64_t image, double x, double y, double w, double h) { (void)image; (void)x; (void)y; (void)w; (void)h; return 0; }
int aura_paint_end(void) { return 0; }
const uint8_t *aura_paint_pixels(void) { return NULL; }
int64_t aura_paint_pixel_width(void) { return 0; }
int64_t aura_paint_pixel_height(void) { return 0; }
int64_t aura_paint_stride(void) { return 0; }
double aura_text_width(const char *text, double size, int64_t weight, const char *family) { (void)text; (void)size; (void)weight; (void)family; return 0.0; }
double aura_text_width_style(const char *text, double size, int64_t weight, const char *family, int64_t italic) { (void)text; (void)size; (void)weight; (void)family; (void)italic; return 0.0; }
double aura_text_height(double size, int64_t weight, const char *family) { (void)size; (void)weight; (void)family; return 0.0; }
int64_t aura_image_open_file(const char *path) { (void)path; return 0; }
int64_t aura_image_buffer_reset(void) { return 0; }
int64_t aura_image_buffer_byte(int64_t value) { (void)value; return 0; }
int64_t aura_image_buffer_commit(void) { return 0; }
int64_t aura_image_width(int64_t handle) { (void)handle; return 0; }
int64_t aura_image_height(int64_t handle) { (void)handle; return 0; }
int64_t aura_image_dispose(int64_t handle) { (void)handle; return 0; }
double aura_mono_time(void) { return 0.0; }
