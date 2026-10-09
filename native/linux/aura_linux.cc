#include "paint.h"
#include "plugin.h"
#include "queue.h"

#include <dbus/dbus.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/Xatom.h>

#include "xdg-shell-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <sys/mman.h>
#include <fcntl.h>

enum {
    AURA_POINTER_DOWN = 1,
    AURA_POINTER_MOVE = 2,
    AURA_POINTER_UP = 3,
    AURA_POINTER_CANCEL = 4,
    AURA_KEY_DOWN = 5,
    AURA_KEY_UP = 6,
    AURA_RESIZE = 7,
    AURA_SCROLL = 8,
    AURA_IME_INSERT = 10,
    AURA_IME_DELETE = 11,
    AURA_IME_MARK = 12,
    AURA_WINDOW_FOCUS = 13,
    AURA_FILE_DROP = 14,
    AURA_AX_ACTION = 15
};

enum { AURA_SHELL_X11 = 0, AURA_SHELL_WAYLAND = 1 };

struct AuraWindow {
    int alive;
    int should_close;
    int shell;
    int text_focus;
    double scale;
    double frame_time;
    AuraQueue queue;
    AuraAx ax;
    uint8_t *pixels;
    int pixel_w;
    int pixel_h;
    int pixel_stride;
    int bgra;
    Display *display;
    Window xwindow;
    XIC ic;
    GC gc;
    XImage *image;
    struct wl_display *wl;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_surface *surface;
    struct xdg_wm_base *wm;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_seat *seat;
    struct wl_subcompositor *subcompositor;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct xkb_state *xkb;
    struct xkb_keymap *keymap;
    int width;
    int height;
    double pointer_x;
    double pointer_y;
    uint32_t serial;
    double drop_x;
    double drop_y;
    Window xdnd_source;
    struct zwp_text_input_manager_v3 *text_manager;
    struct zwp_text_input_v3 *text_input;
    struct wl_data_device_manager *data_manager;
    struct wl_data_device *data_device;
    struct wl_data_source *data_source;
    struct wl_data_offer *selection_offer;
    struct wl_data_offer *drag_offer;
    int drag_uri;
    uint32_t data_version;
    struct wl_cursor_theme *cursor_theme;
    struct wl_surface *cursor_surface;
    int shm_fd;
    uint8_t *shm_data;
    size_t shm_size;
    struct wl_buffer *buffer;
    Window plugin_windows[8];
    struct wl_surface *plugin_surfaces[8];
    int64_t plugin_ids[8];
};

#define AURA_MAX_WINDOWS 32
static AuraWindow *g_windows[AURA_MAX_WINDOWS];
static char *g_clipboard = NULL;
static int g_clip_local = 1;
static DBusConnection *g_a11y = NULL;
static XIM g_xim = NULL;

struct X11Atoms {
    int ready;
    Atom protocols;
    Atom wm_delete;
    Atom clipboard;
    Atom utf8;
    Atom targets;
    Atom aura_data;
    Atom xdnd_aware;
    Atom xdnd_enter;
    Atom xdnd_position;
    Atom xdnd_status;
    Atom xdnd_drop;
    Atom xdnd_leave;
    Atom xdnd_finished;
    Atom xdnd_selection;
    Atom xdnd_action_copy;
    Atom uri_list;
};

static X11Atoms g_atoms;

static void ensure_atoms(Display *display) {
    if (g_atoms.ready || display == NULL) {
        return;
    }
    g_atoms.protocols = XInternAtom(display, "WM_PROTOCOLS", False);
    g_atoms.wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    g_atoms.clipboard = XInternAtom(display, "CLIPBOARD", False);
    g_atoms.utf8 = XInternAtom(display, "UTF8_STRING", False);
    g_atoms.targets = XInternAtom(display, "TARGETS", False);
    g_atoms.aura_data = XInternAtom(display, "AURA_DATA", False);
    g_atoms.xdnd_aware = XInternAtom(display, "XdndAware", False);
    g_atoms.xdnd_enter = XInternAtom(display, "XdndEnter", False);
    g_atoms.xdnd_position = XInternAtom(display, "XdndPosition", False);
    g_atoms.xdnd_status = XInternAtom(display, "XdndStatus", False);
    g_atoms.xdnd_drop = XInternAtom(display, "XdndDrop", False);
    g_atoms.xdnd_leave = XInternAtom(display, "XdndLeave", False);
    g_atoms.xdnd_finished = XInternAtom(display, "XdndFinished", False);
    g_atoms.xdnd_selection = XInternAtom(display, "XdndSelection", False);
    g_atoms.xdnd_action_copy = XInternAtom(display, "XdndActionCopy", False);
    g_atoms.uri_list = XInternAtom(display, "text/uri-list", False);
    g_atoms.ready = 1;
}

static AuraWindow *window_get(int64_t handle, int require_alive) {
    AuraWindow *window = NULL;
    if (handle <= 0 || handle > AURA_MAX_WINDOWS) {
        return NULL;
    }
    window = g_windows[handle - 1];
    if (window == NULL) {
        return NULL;
    }
    if (require_alive && !window->alive) {
        return NULL;
    }
    return window;
}

static double mono_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int want_wayland(void) {
    const char *forced = getenv("AURA_LINUX_SHELL");
    struct wl_display *probe = NULL;
    if (forced != NULL && strcmp(forced, "x11") == 0) {
        return 0;
    }
    if (forced != NULL && strcmp(forced, "wayland") == 0) {
        return 1;
    }
    if (getenv("WAYLAND_DISPLAY") == NULL) {
        return 0;
    }
    probe = wl_display_connect(NULL);
    if (probe == NULL) {
        return 0;
    }
    wl_display_disconnect(probe);
    return 1;
}

static int shell_stores_bgra(AuraWindow *window) {
    Visual *visual = NULL;
    if (window->shell == AURA_SHELL_WAYLAND) {
        return 1;
    }
    if (window->shell == AURA_SHELL_X11 && window->display != NULL) {
        visual = DefaultVisual(window->display, DefaultScreen(window->display));
        if (visual != NULL && visual->red_mask == 0x000000ff) {
            return 0;
        }
        return 1;
    }
    return 0;
}

static void copy_frame_pixels(AuraWindow *window) {
    const uint8_t *source = aura_paint_pixels();
    int64_t width = aura_paint_pixel_width();
    int64_t height = aura_paint_pixel_height();
    int64_t stride = aura_paint_stride();
    size_t bytes = 0;
    size_t i = 0;
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
    window->bgra = shell_stores_bgra(window);
    if (window->bgra) {
        for (i = 0; i + 3 < bytes; i += 4) {
            uint8_t red = next[i];
            next[i] = next[i + 2];
            next[i + 2] = red;
        }
    }
    free(window->pixels);
    window->pixels = next;
    window->pixel_w = (int)width;
    window->pixel_h = (int)height;
    window->pixel_stride = (int)stride;
}

static void x11_blit(AuraWindow *window) {
    if (window->pixels == NULL || window->display == NULL) {
        return;
    }
    if (window->image != NULL) {
        XDestroyImage(window->image);
        window->image = NULL;
    }
    window->image = XCreateImage(window->display, DefaultVisual(window->display, DefaultScreen(window->display)), 24, ZPixmap, 0, (char *)window->pixels, (unsigned)window->pixel_w, (unsigned)window->pixel_h, 32, window->pixel_stride);
    if (window->image == NULL) {
        return;
    }
    window->image->byte_order = LSBFirst;
    XPutImage(window->display, window->xwindow, window->gc, window->image, 0, 0, 0, 0, (unsigned)window->width, (unsigned)window->height);
    window->image->data = NULL;
    XDestroyImage(window->image);
    window->image = NULL;
    XFlush(window->display);
}

static void wayland_blit(AuraWindow *window) {
    struct wl_shm_pool *pool = NULL;
    size_t bytes = 0;
    if (window->pixels == NULL || window->shm == NULL || window->surface == NULL) {
        return;
    }
    bytes = (size_t)window->pixel_stride * (size_t)window->pixel_h;
    if (window->shm_data == NULL || window->shm_size < bytes) {
        if (window->shm_fd < 0) {
            char name[] = "/aura-shm";
            window->shm_fd = shm_open(name, O_CREAT | O_RDWR, 0600);
            shm_unlink(name);
        }
        if (window->shm_fd < 0) {
            return;
        }
        if (ftruncate(window->shm_fd, (off_t)bytes) != 0) {
            return;
        }
        if (window->shm_data != NULL && window->shm_data != MAP_FAILED) {
            munmap(window->shm_data, window->shm_size);
        }
        window->shm_data = (uint8_t *)mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, window->shm_fd, 0);
        window->shm_size = bytes;
    }
    if (window->shm_data == NULL || window->shm_data == MAP_FAILED) {
        return;
    }
    memcpy(window->shm_data, window->pixels, bytes);
    if (window->buffer != NULL) {
        wl_buffer_destroy(window->buffer);
    }
    pool = wl_shm_create_pool(window->shm, window->shm_fd, (int)bytes);
    window->buffer = wl_shm_pool_create_buffer(pool, 0, window->pixel_w, window->pixel_h, window->pixel_stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    wl_surface_attach(window->surface, window->buffer, 0, 0);
    wl_surface_damage(window->surface, 0, 0, window->width, window->height);
    wl_surface_commit(window->surface);
    wl_display_flush(window->wl);
}

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    AuraWindow *window = (AuraWindow *)data;
    if (strcmp(interface, "wl_compositor") == 0) {
        window->compositor = (struct wl_compositor *)wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, "wl_shm") == 0) {
        window->shm = (struct wl_shm *)wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        window->wm = (struct xdg_wm_base *)wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    } else if (strcmp(interface, "wl_seat") == 0) {
        window->seat = (struct wl_seat *)wl_registry_bind(registry, name, &wl_seat_interface, 5);
    } else if (strcmp(interface, "wl_subcompositor") == 0) {
        window->subcompositor = (struct wl_subcompositor *)wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    } else if (strcmp(interface, "zwp_text_input_manager_v3") == 0) {
        window->text_manager = (struct zwp_text_input_manager_v3 *)wl_registry_bind(registry, name, &zwp_text_input_manager_v3_interface, 1);
    } else if (strcmp(interface, "wl_data_device_manager") == 0) {
        uint32_t bind = version < 3 ? version : 3;
        window->data_version = bind;
        window->data_manager = (struct wl_data_device_manager *)wl_registry_bind(registry, name, &wl_data_device_manager_interface, bind);
    }
}

static void registry_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

static const struct wl_registry_listener registry_listener = { registry_global, registry_remove };

static void xdg_ping(void *data, struct xdg_wm_base *wm, uint32_t serial) {
    (void)data;
    xdg_wm_base_pong(wm, serial);
}

static const struct xdg_wm_base_listener wm_listener = { xdg_ping };

static void xdg_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = { xdg_configure };

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states) {
    AuraWindow *window = (AuraWindow *)data;
    (void)toplevel;
    (void)states;
    if (width > 0 && height > 0) {
        window->width = width;
        window->height = height;
        aura_queue_push(&window->queue, AURA_RESIZE, width, height, 0, 0);
    }
}

static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    AuraWindow *window = (AuraWindow *)data;
    (void)toplevel;
    window->should_close = 1;
}

static void toplevel_bounds(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height) {
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
}

static void toplevel_capabilities(void *data, struct xdg_toplevel *toplevel, struct wl_array *capabilities) {
    (void)data;
    (void)toplevel;
    (void)capabilities;
}

static const struct xdg_toplevel_listener toplevel_listener = { toplevel_configure, toplevel_close, toplevel_bounds, toplevel_capabilities };

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
    AuraWindow *window = (AuraWindow *)data;
    (void)pointer;
    (void)surface;
    window->serial = serial;
    window->pointer_x = wl_fixed_to_double(x);
    window->pointer_y = wl_fixed_to_double(y);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface) {
    AuraWindow *window = (AuraWindow *)data;
    (void)pointer;
    (void)serial;
    (void)surface;
    aura_queue_push(&window->queue, AURA_POINTER_CANCEL, 0, 0, 0, 0);
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    AuraWindow *window = (AuraWindow *)data;
    (void)pointer;
    (void)time;
    window->pointer_x = wl_fixed_to_double(x);
    window->pointer_y = wl_fixed_to_double(y);
    aura_queue_push(&window->queue, AURA_POINTER_MOVE, window->pointer_x, window->pointer_y, 0, 1);
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    AuraWindow *window = (AuraWindow *)data;
    (void)pointer;
    (void)time;
    window->serial = serial;
    aura_queue_push(&window->queue, state == WL_POINTER_BUTTON_STATE_PRESSED ? AURA_POINTER_DOWN : AURA_POINTER_UP, window->pointer_x, window->pointer_y, 0, button == 0x110 ? 1 : 2);
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    AuraWindow *window = (AuraWindow *)data;
    (void)pointer;
    (void)time;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        aura_queue_push(&window->queue, AURA_SCROLL, 0, -wl_fixed_to_double(value), 0, 0);
    }
}

static void pointer_frame(void *data, struct wl_pointer *pointer) {
    (void)data;
    (void)pointer;
}

static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source) {
    (void)data;
    (void)pointer;
    (void)source;
}

static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis) {
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)discrete;
}

static void pointer_axis_value120(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t value) {
    (void)data;
    (void)pointer;
    (void)axis;
    (void)value;
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_enter,
    pointer_leave,
    pointer_motion,
    pointer_button,
    pointer_axis,
    pointer_frame,
    pointer_axis_source,
    pointer_axis_stop,
    pointer_axis_discrete,
    pointer_axis_value120
};

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int fd, uint32_t size) {
    AuraWindow *window = (AuraWindow *)data;
    char *map = NULL;
    struct xkb_context *context = NULL;
    (void)keyboard;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    map = (char *)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) {
        return;
    }
    context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (window->keymap != NULL) {
        xkb_keymap_unref(window->keymap);
    }
    if (window->xkb != NULL) {
        xkb_state_unref(window->xkb);
    }
    window->keymap = xkb_keymap_new_from_string(context, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    window->xkb = window->keymap != NULL ? xkb_state_new(window->keymap) : NULL;
    xkb_context_unref(context);
    munmap(map, size);
}

static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
    AuraWindow *window = (AuraWindow *)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    (void)keys;
    aura_queue_push(&window->queue, AURA_WINDOW_FOCUS, 0, 0, 0, 0);
}

static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
    AuraWindow *window = (AuraWindow *)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    aura_queue_push(&window->queue, AURA_POINTER_CANCEL, 0, 0, 0, 0);
}

static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    AuraWindow *window = (AuraWindow *)data;
    char utf8[64];
    (void)keyboard;
    (void)time;
    window->serial = serial;
    aura_queue_push(&window->queue, state == WL_KEYBOARD_KEY_STATE_PRESSED ? AURA_KEY_DOWN : AURA_KEY_UP, 0, 0, (int64_t)key, 0);
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED && window->text_input == NULL && window->xkb != NULL && window->text_focus) {
        xkb_state_key_get_utf8(window->xkb, key + 8, utf8, sizeof(utf8));
        if (utf8[0] != '\0') {
            aura_queue_push_text(&window->queue, AURA_IME_INSERT, utf8);
        }
    }
}

static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    AuraWindow *window = (AuraWindow *)data;
    (void)keyboard;
    (void)serial;
    if (window->xkb != NULL) {
        xkb_state_update_mask(window->xkb, depressed, latched, locked, 0, 0, group);
    }
}

static void keyboard_repeat(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = { keyboard_keymap, keyboard_enter, keyboard_leave, keyboard_key, keyboard_modifiers, keyboard_repeat };

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps) {
    AuraWindow *window = (AuraWindow *)data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && window->pointer == NULL) {
        window->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(window->pointer, &pointer_listener, window);
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && window->keyboard == NULL) {
        window->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(window->keyboard, &keyboard_listener, window);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener seat_listener = { seat_caps, seat_name };

static void text_enter(void *data, struct zwp_text_input_v3 *input, struct wl_surface *surface) {
    (void)data;
    (void)input;
    (void)surface;
}

static void text_leave(void *data, struct zwp_text_input_v3 *input, struct wl_surface *surface) {
    (void)data;
    (void)input;
    (void)surface;
}

static void text_preedit(void *data, struct zwp_text_input_v3 *input, const char *text, int32_t begin, int32_t end) {
    AuraWindow *window = (AuraWindow *)data;
    (void)input;
    (void)begin;
    (void)end;
    aura_queue_push_text(&window->queue, AURA_IME_MARK, text != NULL ? text : "");
}

static void text_commit(void *data, struct zwp_text_input_v3 *input, const char *text) {
    AuraWindow *window = (AuraWindow *)data;
    (void)input;
    if (text != NULL && text[0] != '\0') {
        aura_queue_push_text(&window->queue, AURA_IME_INSERT, text);
    }
}

static void text_delete(void *data, struct zwp_text_input_v3 *input, uint32_t before, uint32_t after) {
    AuraWindow *window = (AuraWindow *)data;
    uint32_t i = 0;
    (void)input;
    (void)after;
    if (before == 0) {
        before = 1;
    }
    for (i = 0; i < before; i++) {
        aura_queue_push(&window->queue, AURA_IME_DELETE, 0, 0, 0, 0);
    }
}

static void text_done(void *data, struct zwp_text_input_v3 *input, uint32_t serial) {
    (void)data;
    (void)input;
    (void)serial;
}

static const struct zwp_text_input_v3_listener text_input_listener = { text_enter, text_leave, text_preedit, text_commit, text_delete, text_done };

static void push_uri_list(AuraWindow *window, const char *data);

static char *read_fd_text(int fd) {
    char *buffer = (char *)malloc(65536);
    int total = 0;
    int n = 0;
    if (buffer == NULL) {
        close(fd);
        return NULL;
    }
    while (total < 65535 && (n = (int)read(fd, buffer + total, (size_t)(65535 - total))) > 0) {
        total = total + n;
    }
    close(fd);
    buffer[total] = '\0';
    return buffer;
}

static void offer_mime(void *data, struct wl_data_offer *offer, const char *mime) {
    AuraWindow *window = (AuraWindow *)data;
    (void)offer;
    if (mime != NULL && strstr(mime, "uri-list") != NULL) {
        window->drag_uri = 1;
    }
}

static void offer_actions(void *data, struct wl_data_offer *offer, uint32_t source) {
    (void)data;
    (void)offer;
    (void)source;
}

static void offer_action(void *data, struct wl_data_offer *offer, uint32_t action) {
    (void)data;
    (void)offer;
    (void)action;
}

static const struct wl_data_offer_listener offer_listener = { offer_mime, offer_actions, offer_action };

static void data_offer(void *data, struct wl_data_device *device, struct wl_data_offer *offer) {
    AuraWindow *window = (AuraWindow *)data;
    (void)device;
    window->drag_uri = 0;
    wl_data_offer_add_listener(offer, &offer_listener, window);
}

static void data_enter(void *data, struct wl_data_device *device, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer) {
    AuraWindow *window = (AuraWindow *)data;
    (void)device;
    (void)surface;
    window->serial = serial;
    window->drop_x = wl_fixed_to_double(x);
    window->drop_y = wl_fixed_to_double(y);
    window->drag_offer = offer;
    if (offer != NULL && window->drag_uri) {
        wl_data_offer_accept(offer, serial, "text/uri-list");
    }
}

static void data_leave(void *data, struct wl_data_device *device) {
    AuraWindow *window = (AuraWindow *)data;
    (void)device;
    window->drag_offer = NULL;
}

static void data_motion(void *data, struct wl_data_device *device, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    AuraWindow *window = (AuraWindow *)data;
    (void)device;
    (void)time;
    window->drop_x = wl_fixed_to_double(x);
    window->drop_y = wl_fixed_to_double(y);
}

static void data_drop(void *data, struct wl_data_device *device) {
    AuraWindow *window = (AuraWindow *)data;
    int pipes[2];
    char *text = NULL;
    (void)device;
    if (window->drag_offer == NULL || pipe(pipes) != 0) {
        return;
    }
    wl_data_offer_receive(window->drag_offer, "text/uri-list", pipes[1]);
    close(pipes[1]);
    wl_display_roundtrip(window->wl);
    text = read_fd_text(pipes[0]);
    push_uri_list(window, text);
    free(text);
    if (window->data_version >= 3) {
        wl_data_offer_finish(window->drag_offer);
    }
    wl_data_offer_destroy(window->drag_offer);
    window->drag_offer = NULL;
}

static void data_selection(void *data, struct wl_data_device *device, struct wl_data_offer *offer) {
    AuraWindow *window = (AuraWindow *)data;
    (void)device;
    if (window->selection_offer != NULL && window->selection_offer != offer) {
        wl_data_offer_destroy(window->selection_offer);
    }
    window->selection_offer = offer;
    g_clip_local = offer == NULL ? 1 : 0;
}

static const struct wl_data_device_listener data_device_listener = { data_offer, data_enter, data_leave, data_motion, data_drop, data_selection };

static void source_target(void *data, struct wl_data_source *source, const char *mime) {
    (void)data;
    (void)source;
    (void)mime;
}

static void source_send(void *data, struct wl_data_source *source, const char *mime, int32_t fd) {
    (void)data;
    (void)source;
    if (g_clipboard != NULL && mime != NULL && strstr(mime, "text") != NULL) {
        ssize_t ignored = write(fd, g_clipboard, strlen(g_clipboard));
        (void)ignored;
    }
    close(fd);
}

static void source_cancelled(void *data, struct wl_data_source *source) {
    AuraWindow *window = (AuraWindow *)data;
    if (window->data_source == source) {
        wl_data_source_destroy(source);
        window->data_source = NULL;
        g_clip_local = 0;
    }
}

static void source_performed(void *data, struct wl_data_source *source) {
    (void)data;
    (void)source;
}

static void source_finished(void *data, struct wl_data_source *source) {
    (void)data;
    (void)source;
}

static void source_action(void *data, struct wl_data_source *source, uint32_t action) {
    (void)data;
    (void)source;
    (void)action;
}

static const struct wl_data_source_listener data_source_listener = { source_target, source_send, source_cancelled, source_performed, source_finished, source_action };

static void publish_wayland_clipboard(AuraWindow *window, const char *text) {
    if (window->data_manager == NULL || window->data_device == NULL) {
        return;
    }
    if (window->data_source != NULL) {
        wl_data_source_destroy(window->data_source);
        window->data_source = NULL;
    }
    window->data_source = wl_data_device_manager_create_data_source(window->data_manager);
    wl_data_source_add_listener(window->data_source, &data_source_listener, window);
    wl_data_source_offer(window->data_source, "text/plain;charset=utf-8");
    wl_data_source_offer(window->data_source, "text/plain");
    wl_data_device_set_selection(window->data_device, window->data_source, window->serial);
    wl_display_flush(window->wl);
    (void)text;
}

static void pull_wayland_clipboard(AuraWindow *window) {
    int pipes[2];
    char *text = NULL;
    if (window->selection_offer == NULL || g_clip_local || pipe(pipes) != 0) {
        return;
    }
    wl_data_offer_receive(window->selection_offer, "text/plain;charset=utf-8", pipes[1]);
    close(pipes[1]);
    wl_display_roundtrip(window->wl);
    text = read_fd_text(pipes[0]);
    if (text != NULL) {
        free(g_clipboard);
        g_clipboard = text;
        g_clip_local = 1;
    }
}

static int create_wayland(AuraWindow *window, int64_t width, int64_t height, const char *title, int64_t visible) {
    struct wl_registry *registry = NULL;
    window->wl = wl_display_connect(NULL);
    window->shm_fd = -1;
    if (window->wl == NULL) {
        return 0;
    }
    registry = wl_display_get_registry(window->wl);
    wl_registry_add_listener(registry, &registry_listener, window);
    wl_display_roundtrip(window->wl);
    if (window->compositor == NULL || window->shm == NULL || window->wm == NULL) {
        wl_display_disconnect(window->wl);
        window->wl = NULL;
        return 0;
    }
    xdg_wm_base_add_listener(window->wm, &wm_listener, window);
    window->surface = wl_compositor_create_surface(window->compositor);
    window->xdg_surface = xdg_wm_base_get_xdg_surface(window->wm, window->surface);
    xdg_surface_add_listener(window->xdg_surface, &xdg_surface_listener, window);
    window->toplevel = xdg_surface_get_toplevel(window->xdg_surface);
    xdg_toplevel_add_listener(window->toplevel, &toplevel_listener, window);
    xdg_toplevel_set_title(window->toplevel, title != NULL ? title : "Aura");
    if (window->seat != NULL) {
        wl_seat_add_listener(window->seat, &seat_listener, window);
        if (window->text_manager != NULL) {
            window->text_input = zwp_text_input_manager_v3_get_text_input(window->text_manager, window->seat);
            zwp_text_input_v3_add_listener(window->text_input, &text_input_listener, window);
        }
        if (window->data_manager != NULL) {
            window->data_device = wl_data_device_manager_get_data_device(window->data_manager, window->seat);
            wl_data_device_add_listener(window->data_device, &data_device_listener, window);
        }
    }
    wl_surface_commit(window->surface);
    wl_display_roundtrip(window->wl);
    window->width = (int)width;
    window->height = (int)height;
    window->shell = AURA_SHELL_WAYLAND;
    (void)visible;
    return 1;
}

static int create_x11(AuraWindow *window, int64_t width, int64_t height, const char *title, int64_t visible) {
    XSetWindowAttributes attrs;
    window->display = XOpenDisplay(NULL);
    if (window->display == NULL) {
        return 0;
    }
    memset(&attrs, 0, sizeof(attrs));
    attrs.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | FocusChangeMask;
    window->xwindow = XCreateWindow(window->display, DefaultRootWindow(window->display), 0, 0, (unsigned)width, (unsigned)height, 0, CopyFromParent, InputOutput, CopyFromParent, CWEventMask, &attrs);
    if (window->xwindow == 0) {
        XCloseDisplay(window->display);
        window->display = NULL;
        return 0;
    }
    XStoreName(window->display, window->xwindow, title != NULL ? title : "Aura");
    ensure_atoms(window->display);
    XSetWMProtocols(window->display, window->xwindow, &g_atoms.wm_delete, 1);
    {
        long version = 5;
        XChangeProperty(window->display, window->xwindow, g_atoms.xdnd_aware, XA_ATOM, 32, PropModeReplace, (unsigned char *)&version, 1);
    }
    window->gc = XCreateGC(window->display, window->xwindow, 0, NULL);
    if (g_xim == NULL) {
        g_xim = XOpenIM(window->display, NULL, NULL, NULL);
    }
    if (g_xim != NULL) {
        window->ic = XCreateIC(g_xim, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, window->xwindow, NULL);
    }
    window->width = (int)width;
    window->height = (int)height;
    window->shell = AURA_SHELL_X11;
    if (visible != 0) {
        XMapWindow(window->display, window->xwindow);
        XFlush(window->display);
    }
    return 1;
}

static void ensure_a11y(void) {
    DBusError error;
    DBusMessage *message = NULL;
    DBusMessage *reply = NULL;
    const char *address = NULL;
    if (g_a11y != NULL) {
        return;
    }
    dbus_error_init(&error);
    message = dbus_message_new_method_call("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
    if (message == NULL) {
        return;
    }
    reply = dbus_connection_send_with_reply_and_block(dbus_bus_get(DBUS_BUS_SESSION, &error), message, 200, &error);
    dbus_message_unref(message);
    if (reply == NULL) {
        dbus_error_free(&error);
        return;
    }
    if (dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &address, DBUS_TYPE_INVALID)) {
        g_a11y = dbus_connection_open(address, &error);
        if (g_a11y != NULL) {
            dbus_bus_register(g_a11y, &error);
        }
    }
    dbus_message_unref(reply);
    dbus_error_free(&error);
}

static void publish_a11y(AuraWindow *window, int64_t handle) {
    DBusMessage *message = NULL;
    int i = 0;
    char path[128];
    ensure_a11y();
    if (g_a11y == NULL) {
        return;
    }
    for (i = 0; i < aura_ax_count_nodes(&window->ax); i++) {
        snprintf(path, sizeof(path), "/org/aura/window/%lld/child/%d", (long long)handle, i);
        message = dbus_message_new_signal(path, "org.a11y.atspi.Event.Object", "PropertyChange");
        if (message != NULL) {
            const char *role = window->ax.nodes[i].role != NULL ? window->ax.nodes[i].role : "";
            const char *label = window->ax.nodes[i].label != NULL ? window->ax.nodes[i].label : "";
            dbus_message_append_args(message, DBUS_TYPE_STRING, &role, DBUS_TYPE_STRING, &label, DBUS_TYPE_INVALID);
            dbus_connection_send(g_a11y, message, NULL);
            dbus_message_unref(message);
        }
    }
    dbus_connection_flush(g_a11y);
}

static int hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static char *percent_decode(const char *text, int length) {
    char *out = (char *)malloc((size_t)length + 1);
    int read = 0;
    int write = 0;
    if (out == NULL) {
        return NULL;
    }
    while (read < length) {
        if (text[read] == '%' && read + 2 < length) {
            int hi = hex_nibble(text[read + 1]);
            int lo = hex_nibble(text[read + 2]);
            if (hi >= 0 && lo >= 0) {
                out[write] = (char)((hi << 4) | lo);
                write = write + 1;
                read = read + 3;
                continue;
            }
        }
        out[write] = text[read];
        write = write + 1;
        read = read + 1;
    }
    out[write] = '\0';
    return out;
}

static void push_uri_list(AuraWindow *window, const char *data) {
    const char *line = data;
    if (data == NULL) {
        return;
    }
    while (*line != '\0') {
        const char *end = line;
        char *path = NULL;
        while (*end != '\0' && *end != '\n' && *end != '\r') {
            end = end + 1;
        }
        if ((int)(end - line) > 7 && strncmp(line, "file://", 7) == 0) {
            const char *start = line + 7;
            if (strncmp(start, "localhost", 9) == 0) {
                start = start + 9;
            }
            path = percent_decode(start, (int)(end - start));
            if (path != NULL && path[0] != '\0') {
                aura_queue_push_text(&window->queue, AURA_FILE_DROP, path);
                if (window->queue.count > 0) {
                    window->queue.events[window->queue.count - 1].x = window->drop_x;
                    window->queue.events[window->queue.count - 1].y = window->drop_y;
                }
            }
            free(path);
        }
        line = end;
        while (*line == '\n' || *line == '\r') {
            line = line + 1;
        }
    }
}

static char *x11_property_text(Display *display, Window target, Atom property) {
    Atom actual = 0;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char *data = NULL;
    char *copy = NULL;
    if (XGetWindowProperty(display, target, property, 0, 1 << 20, True, AnyPropertyType, &actual, &format, &count, &remaining, &data) != Success || data == NULL) {
        return NULL;
    }
    copy = (char *)malloc(count + 1);
    if (copy != NULL) {
        memcpy(copy, data, count);
        copy[count] = '\0';
    }
    XFree(data);
    return copy;
}

static void x11_send_status(AuraWindow *window, Window source, int accept) {
    XClientMessageEvent reply;
    memset(&reply, 0, sizeof(reply));
    reply.type = ClientMessage;
    reply.display = window->display;
    reply.window = source;
    reply.message_type = g_atoms.xdnd_status;
    reply.format = 32;
    reply.data.l[0] = (long)window->xwindow;
    reply.data.l[1] = accept ? 1 : 0;
    reply.data.l[4] = (long)g_atoms.xdnd_action_copy;
    XSendEvent(window->display, source, False, NoEventMask, (XEvent *)&reply);
    XFlush(window->display);
}

static void x11_finish_drop(AuraWindow *window) {
    XClientMessageEvent reply;
    if (window->xdnd_source == 0) {
        return;
    }
    memset(&reply, 0, sizeof(reply));
    reply.type = ClientMessage;
    reply.display = window->display;
    reply.window = window->xdnd_source;
    reply.message_type = g_atoms.xdnd_finished;
    reply.format = 32;
    reply.data.l[0] = (long)window->xwindow;
    reply.data.l[1] = 1;
    reply.data.l[2] = (long)g_atoms.xdnd_action_copy;
    XSendEvent(window->display, window->xdnd_source, False, NoEventMask, (XEvent *)&reply);
    XFlush(window->display);
    window->xdnd_source = 0;
}

static AuraWindow *first_window(int shell) {
    int i = 0;
    for (i = 0; i < AURA_MAX_WINDOWS; i++) {
        if (g_windows[i] != NULL && g_windows[i]->alive && g_windows[i]->shell == shell) {
            return g_windows[i];
        }
    }
    return NULL;
}

static void poll_x11(AuraWindow *window);

static void claim_x11_clipboard(const char *text) {
    AuraWindow *window = first_window(AURA_SHELL_X11);
    free(g_clipboard);
    g_clipboard = strdup(text != NULL ? text : "");
    g_clip_local = 1;
    if (window == NULL || window->display == NULL || g_clipboard == NULL) {
        return;
    }
    ensure_atoms(window->display);
    XSetSelectionOwner(window->display, g_atoms.clipboard, window->xwindow, CurrentTime);
    XSetSelectionOwner(window->display, XA_PRIMARY, window->xwindow, CurrentTime);
    XFlush(window->display);
}

static void pull_x11_clipboard(AuraWindow *window) {
    Window owner = 0;
    int fd = 0;
    int spins = 0;
    struct pollfd pfd;
    if (window == NULL || window->display == NULL) {
        return;
    }
    ensure_atoms(window->display);
    owner = XGetSelectionOwner(window->display, g_atoms.clipboard);
    if (owner == None || owner == window->xwindow) {
        return;
    }
    XConvertSelection(window->display, g_atoms.clipboard, g_atoms.utf8, g_atoms.aura_data, window->xwindow, CurrentTime);
    XFlush(window->display);
    fd = ConnectionNumber(window->display);
    pfd.fd = fd;
    pfd.events = POLLIN;
    while (spins < 20) {
        if (poll(&pfd, 1, 20) <= 0 && XPending(window->display) == 0) {
            spins = spins + 1;
            continue;
        }
        poll_x11(window);
        if (g_clip_local) {
            return;
        }
        spins = spins + 1;
    }
}

static void poll_x11(AuraWindow *window) {
    while (XPending(window->display) > 0) {
        XEvent event;
        char text[64];
        KeySym symbol = 0;
        Status status = 0;
        int length = 0;
        XNextEvent(window->display, &event);
        if (event.type == ButtonPress) {
            aura_queue_push(&window->queue, event.xbutton.button == 4 || event.xbutton.button == 5 ? AURA_SCROLL : AURA_POINTER_DOWN, event.xbutton.x, event.xbutton.y, 0, event.xbutton.button);
            if (event.xbutton.button == 4) {
                window->queue.events[window->queue.count - 1].kind = AURA_SCROLL;
                window->queue.events[window->queue.count - 1].y = 1;
            } else if (event.xbutton.button == 5) {
                window->queue.events[window->queue.count - 1].kind = AURA_SCROLL;
                window->queue.events[window->queue.count - 1].y = -1;
            }
        } else if (event.type == ButtonRelease) {
            aura_queue_push(&window->queue, AURA_POINTER_UP, event.xbutton.x, event.xbutton.y, 0, event.xbutton.button);
        } else if (event.type == MotionNotify) {
            aura_queue_push(&window->queue, AURA_POINTER_MOVE, event.xmotion.x, event.xmotion.y, 0, 1);
        } else if (event.type == KeyPress || event.type == KeyRelease) {
            int64_t mods = 0;
            if (event.xkey.state & ShiftMask) {
                mods |= 2;
            }
            if (event.xkey.state & ControlMask) {
                mods |= 1;
            }
            aura_queue_push(&window->queue, event.type == KeyPress ? AURA_KEY_DOWN : AURA_KEY_UP, 0, 0, (int64_t)event.xkey.keycode, mods);
            if (event.type == KeyPress && window->text_focus && window->ic != NULL) {
                length = Xutf8LookupString(window->ic, &event.xkey, text, (int)sizeof(text) - 1, &symbol, &status);
                if (length > 0) {
                    text[length] = '\0';
                    aura_queue_push_text(&window->queue, AURA_IME_INSERT, text);
                }
            }
        } else if (event.type == ConfigureNotify) {
            window->width = event.xconfigure.width;
            window->height = event.xconfigure.height;
            aura_queue_push(&window->queue, AURA_RESIZE, event.xconfigure.width, event.xconfigure.height, 0, 0);
        } else if (event.type == FocusIn) {
            aura_queue_push(&window->queue, AURA_WINDOW_FOCUS, 0, 0, 0, 0);
        } else if (event.type == FocusOut) {
            aura_queue_push(&window->queue, AURA_POINTER_CANCEL, 0, 0, 0, 0);
        } else if (event.type == SelectionRequest) {
            XSelectionEvent reply;
            Atom target = event.xselectionrequest.target;
            memset(&reply, 0, sizeof(reply));
            reply.type = SelectionNotify;
            reply.display = window->display;
            reply.requestor = event.xselectionrequest.requestor;
            reply.selection = event.xselectionrequest.selection;
            reply.target = target;
            reply.time = event.xselectionrequest.time;
            reply.property = None;
            if (g_clipboard != NULL && (target == g_atoms.utf8 || target == XA_STRING)) {
                XChangeProperty(window->display, event.xselectionrequest.requestor, event.xselectionrequest.property, target, 8, PropModeReplace, (unsigned char *)g_clipboard, (int)strlen(g_clipboard));
                reply.property = event.xselectionrequest.property;
            } else if (target == g_atoms.targets) {
                Atom list[3];
                list[0] = g_atoms.utf8;
                list[1] = XA_STRING;
                list[2] = g_atoms.targets;
                XChangeProperty(window->display, event.xselectionrequest.requestor, event.xselectionrequest.property, XA_ATOM, 32, PropModeReplace, (unsigned char *)list, 3);
                reply.property = event.xselectionrequest.property;
            }
            XSendEvent(window->display, reply.requestor, False, NoEventMask, (XEvent *)&reply);
        } else if (event.type == SelectionNotify) {
            char *data = x11_property_text(window->display, window->xwindow, event.xselection.property);
            if (event.xselection.target == g_atoms.uri_list) {
                push_uri_list(window, data);
                x11_finish_drop(window);
            } else if (data != NULL) {
                free(g_clipboard);
                g_clipboard = data;
                data = NULL;
                g_clip_local = 1;
            }
            free(data);
        } else if (event.type == SelectionClear) {
            g_clip_local = 0;
        } else if (event.type == ClientMessage) {
            if (event.xclient.message_type == g_atoms.protocols && (Atom)event.xclient.data.l[0] == g_atoms.wm_delete) {
                window->should_close = 1;
            } else if (event.xclient.message_type == g_atoms.xdnd_position) {
                int root_x = (int)((event.xclient.data.l[2] >> 16) & 0xffff);
                int root_y = (int)(event.xclient.data.l[2] & 0xffff);
                int local_x = 0;
                int local_y = 0;
                Window child = 0;
                XTranslateCoordinates(window->display, DefaultRootWindow(window->display), window->xwindow, root_x, root_y, &local_x, &local_y, &child);
                window->drop_x = local_x;
                window->drop_y = local_y;
                window->xdnd_source = (Window)event.xclient.data.l[0];
                x11_send_status(window, window->xdnd_source, 1);
            } else if (event.xclient.message_type == g_atoms.xdnd_drop) {
                window->xdnd_source = (Window)event.xclient.data.l[0];
                XConvertSelection(window->display, g_atoms.xdnd_selection, g_atoms.uri_list, g_atoms.aura_data, window->xwindow, (Time)event.xclient.data.l[2]);
            } else if (event.xclient.message_type == g_atoms.xdnd_leave) {
                window->xdnd_source = 0;
            }
        }
    }
}

extern "C" int64_t aura_window_create(int64_t width, int64_t height, const char *title, int64_t visible) {
    int slot = -1;
    int i = 0;
    AuraWindow *window = NULL;
    int created = 0;
    for (i = 0; i < AURA_MAX_WINDOWS; i++) {
        if (g_windows[i] == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return 0;
    }
    window = (AuraWindow *)calloc(1, sizeof(AuraWindow));
    if (window == NULL) {
        return 0;
    }
    window->alive = 1;
    window->scale = 1.0;
    window->shm_fd = -1;
    window->width = (int)width;
    window->height = (int)height;
    aura_queue_init(&window->queue);
    aura_ax_init(&window->ax);
    if (want_wayland()) {
        created = create_wayland(window, width, height, title, visible);
    } else {
        created = create_x11(window, width, height, title, visible);
    }
    if (!created) {
        aura_queue_free(&window->queue);
        aura_ax_free(&window->ax);
        free(window);
        return 0;
    }
    g_windows[slot] = window;
    return (int64_t)slot + 1;
}

extern "C" int64_t aura_window_destroy(int64_t handle) {
    AuraWindow *window = NULL;
    if (handle <= 0 || handle > AURA_MAX_WINDOWS) {
        return -1;
    }
    window = g_windows[handle - 1];
    if (window == NULL) {
        return 0;
    }
    if (!window->alive) {
        return 0;
    }
    window->alive = 0;
    if (window->shell == AURA_SHELL_X11 && window->display != NULL) {
        if (window->ic != NULL) {
            XDestroyIC(window->ic);
        }
        if (window->gc != NULL) {
            XFreeGC(window->display, window->gc);
        }
        XDestroyWindow(window->display, window->xwindow);
        XCloseDisplay(window->display);
    }
    if (window->shell == AURA_SHELL_WAYLAND && window->wl != NULL) {
        if (window->buffer != NULL) {
            wl_buffer_destroy(window->buffer);
        }
        if (window->toplevel != NULL) {
            xdg_toplevel_destroy(window->toplevel);
        }
        if (window->xdg_surface != NULL) {
            xdg_surface_destroy(window->xdg_surface);
        }
        if (window->surface != NULL) {
            wl_surface_destroy(window->surface);
        }
        if (window->shm_data != NULL && window->shm_data != MAP_FAILED) {
            munmap(window->shm_data, window->shm_size);
        }
        if (window->shm_fd >= 0) {
            close(window->shm_fd);
        }
        if (window->xkb != NULL) {
            xkb_state_unref(window->xkb);
        }
        if (window->keymap != NULL) {
            xkb_keymap_unref(window->keymap);
        }
        if (window->text_input != NULL) {
            zwp_text_input_v3_destroy(window->text_input);
        }
        if (window->data_source != NULL) {
            wl_data_source_destroy(window->data_source);
        }
        if (window->data_device != NULL) {
            wl_data_device_destroy(window->data_device);
        }
        if (window->selection_offer != NULL) {
            wl_data_offer_destroy(window->selection_offer);
        }
        if (window->cursor_surface != NULL) {
            wl_surface_destroy(window->cursor_surface);
        }
        if (window->cursor_theme != NULL) {
            wl_cursor_theme_destroy(window->cursor_theme);
        }
        wl_display_disconnect(window->wl);
    }
    aura_queue_free(&window->queue);
    aura_ax_free(&window->ax);
    free(window->pixels);
    g_windows[handle - 1] = NULL;
    free(window);
    return 0;
}

extern "C" int64_t aura_window_poll(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (window->shell == AURA_SHELL_X11) {
        poll_x11(window);
    } else if (window->wl != NULL) {
        wl_display_dispatch_pending(window->wl);
        wl_display_flush(window->wl);
    }
    return window->queue.count;
}

extern "C" int64_t aura_window_event_count(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->queue.count;
}

extern "C" int64_t aura_window_event_kind(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    return window->queue.events[index].kind;
}

extern "C" double aura_window_event_x(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    return window->queue.events[index].x;
}

extern "C" double aura_window_event_y(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    return window->queue.events[index].y;
}

extern "C" int64_t aura_window_event_key(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    return window->queue.events[index].key;
}

extern "C" int64_t aura_window_event_button(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    return window->queue.events[index].button;
}

extern "C" int64_t aura_window_events_clear(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_queue_clear(&window->queue);
    return 0;
}

extern "C" int64_t aura_window_event_text_len(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL || index < 0 || index >= window->queue.count || window->queue.events[index].text == NULL) {
        return 0;
    }
    return (int64_t)strlen(window->queue.events[index].text);
}

extern "C" int64_t aura_window_event_text_byte(int64_t handle, int64_t index, int64_t offset) {
    AuraWindow *window = window_get(handle, 1);
    const char *text = NULL;
    if (window == NULL || index < 0 || index >= window->queue.count) {
        return 0;
    }
    text = window->queue.events[index].text;
    if (text == NULL || offset < 0 || offset >= (int64_t)strlen(text)) {
        return 0;
    }
    return (unsigned char)text[offset];
}

extern "C" int64_t aura_window_should_close(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return 1;
    }
    return window->should_close;
}

extern "C" double aura_window_width(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->width;
}

extern "C" double aura_window_height(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->height;
}

extern "C" double aura_window_scale(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->scale;
}

extern "C" double aura_frame_timestamp(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return window->frame_time;
}

extern "C" int64_t aura_window_wait_frame(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    struct pollfd fd;
    if (window == NULL) {
        return -1;
    }
    if (window->shell == AURA_SHELL_WAYLAND && window->wl != NULL) {
        fd.fd = wl_display_get_fd(window->wl);
        fd.events = POLLIN;
        poll(&fd, 1, 16);
        wl_display_dispatch_pending(window->wl);
    } else {
        struct timespec delay;
        delay.tv_sec = 0;
        delay.tv_nsec = 16000000;
        nanosleep(&delay, NULL);
    }
    window->frame_time = mono_seconds();
    return 0;
}

extern "C" int64_t aura_frame_begin(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    int64_t width = 1;
    int64_t height = 1;
    if (window == NULL) {
        return -1;
    }
    width = (int64_t)(window->width * window->scale);
    height = (int64_t)(window->height * window->scale);
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    return aura_paint_begin(width, height, window->scale);
}

extern "C" int64_t aura_frame_end(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (aura_paint_end() < 0) {
        return -1;
    }
    copy_frame_pixels(window);
    if (window->shell == AURA_SHELL_WAYLAND) {
        wayland_blit(window);
    } else {
        x11_blit(window);
    }
    return 0;
}

extern "C" int64_t aura_cmd_clear(int64_t handle, int64_t argb) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_clear(argb);
}

extern "C" int64_t aura_cmd_rect(int64_t handle, double x, double y, double w, double h, int64_t argb) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_rect(x, y, w, h, argb);
}

extern "C" int64_t aura_cmd_text(int64_t handle, double x, double y, const char *text, double size, int64_t weight, int64_t argb, const char *family) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_text(x, y, text, size, weight, argb, family);
}

extern "C" int64_t aura_cmd_opacity(int64_t handle, double opacity) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_opacity(opacity);
}

extern "C" int64_t aura_cmd_opacity_pop(int64_t handle) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_opacity_pop();
}

extern "C" int64_t aura_cmd_image(int64_t handle, int64_t image, double x, double y, double w, double h) {
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return aura_paint_image(image, x, y, w, h);
}

extern "C" int64_t aura_sample(int64_t handle, double x, double y) {
    AuraWindow *window = window_get(handle, 1);
    int px = 0;
    int py = 0;
    uint8_t *pixel = NULL;
    int a = 0;
    int r = 0;
    int g = 0;
    int b = 0;
    if (window == NULL || window->pixels == NULL) {
        return -1;
    }
    px = (int)(x * window->scale);
    py = (int)(y * window->scale);
    if (px < 0 || py < 0 || px >= window->pixel_w || py >= window->pixel_h) {
        return -1;
    }
    pixel = window->pixels + (size_t)py * (size_t)window->pixel_stride + (size_t)px * 4;
    if (window->bgra) {
        r = pixel[2];
        b = pixel[0];
    } else {
        r = pixel[0];
        b = pixel[2];
    }
    g = pixel[1];
    a = pixel[3];
    if (a > 0 && a < 255) {
        r = r * 255 / a;
        g = g * 255 / a;
        b = b * 255 / a;
    }
    return ((int64_t)a << 24) | ((int64_t)r << 16) | ((int64_t)g << 8) | (int64_t)b;
}

extern "C" int64_t aura_post_mouse(int64_t handle, int64_t kind, double x, double y) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_queue_push(&window->queue, (int)kind, x, y, 0, 1);
    return 0;
}

extern "C" int64_t aura_post_text(int64_t handle, const char *utf8) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_queue_push_text(&window->queue, AURA_IME_INSERT, utf8);
    return 0;
}

extern "C" int64_t aura_post_drop(int64_t handle, double x, double y, const char *path) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    aura_queue_push_text(&window->queue, AURA_FILE_DROP, path);
    if (window->queue.count > 0) {
        window->queue.events[window->queue.count - 1].x = x;
        window->queue.events[window->queue.count - 1].y = y;
    }
    return 0;
}

extern "C" int64_t aura_post_ax(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    int64_t press = 0;
    if (window == NULL) {
        return -1;
    }
    press = aura_ax_press_node(&window->ax, index);
    if (press < 0) {
        return -1;
    }
    aura_queue_push(&window->queue, AURA_AX_ACTION, 0, 0, press, 0);
    return 0;
}

extern "C" int64_t aura_text_focus(int64_t handle, int64_t enabled) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    window->text_focus = enabled != 0;
    if (window->shell == AURA_SHELL_X11 && window->ic != NULL && enabled != 0) {
        XSetICFocus(window->ic);
    }
    if (window->shell == AURA_SHELL_WAYLAND && window->text_input != NULL) {
        if (enabled != 0) {
            zwp_text_input_v3_enable(window->text_input);
        } else {
            zwp_text_input_v3_disable(window->text_input);
        }
        zwp_text_input_v3_commit(window->text_input);
        wl_display_flush(window->wl);
    }
    return 0;
}

extern "C" int64_t aura_text_selection(int64_t handle, int64_t start, int64_t length) {
    (void)start;
    (void)length;
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return 0;
}

extern "C" int64_t aura_text_contents(int64_t handle, const char *utf8) {
    (void)utf8;
    if (window_get(handle, 1) == NULL) {
        return -1;
    }
    return 0;
}

extern "C" int64_t aura_text_caret(int64_t handle, double x, double y, double height) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (window->shell == AURA_SHELL_WAYLAND && window->text_input != NULL && window->text_focus) {
        zwp_text_input_v3_set_cursor_rectangle(window->text_input, (int32_t)x, (int32_t)y, 1, (int32_t)height);
        zwp_text_input_v3_commit(window->text_input);
        wl_display_flush(window->wl);
    }
    return 0;
}

extern "C" int64_t aura_clipboard_set(const char *text) {
    AuraWindow *wayland = first_window(AURA_SHELL_WAYLAND);
    claim_x11_clipboard(text);
    if (wayland != NULL) {
        publish_wayland_clipboard(wayland, text);
    }
    return g_clipboard != NULL ? 0 : -1;
}

extern "C" int64_t aura_clipboard_len(void) {
    if (!g_clip_local) {
        pull_x11_clipboard(first_window(AURA_SHELL_X11));
        pull_wayland_clipboard(first_window(AURA_SHELL_WAYLAND));
    }
    if (g_clipboard == NULL) {
        g_clipboard = strdup("");
    }
    if (g_clipboard == NULL) {
        return 0;
    }
    return (int64_t)strlen(g_clipboard);
}

extern "C" int64_t aura_clipboard_byte(int64_t offset) {
    if (g_clipboard == NULL || offset < 0 || offset >= (int64_t)strlen(g_clipboard)) {
        return 0;
    }
    return (unsigned char)g_clipboard[offset];
}

extern "C" int64_t aura_set_cursor(int64_t handle, int64_t kind) {
    AuraWindow *window = window_get(handle, 1);
    Cursor cursor;
    if (window == NULL) {
        return -1;
    }
    if (window->shell == AURA_SHELL_X11 && window->display != NULL) {
        cursor = XCreateFontCursor(window->display, kind == 1 ? XC_xterm : XC_left_ptr);
        XDefineCursor(window->display, window->xwindow, cursor);
        XFreeCursor(window->display, cursor);
    } else if (window->shell == AURA_SHELL_WAYLAND && window->pointer != NULL && window->shm != NULL && window->compositor != NULL) {
        struct wl_cursor *drawn = NULL;
        struct wl_cursor_image *image = NULL;
        struct wl_buffer *buffer = NULL;
        if (window->cursor_theme == NULL) {
            window->cursor_theme = wl_cursor_theme_load(NULL, 24, window->shm);
            window->cursor_surface = wl_compositor_create_surface(window->compositor);
        }
        if (window->cursor_theme != NULL && window->cursor_surface != NULL) {
            drawn = wl_cursor_theme_get_cursor(window->cursor_theme, kind == 1 ? "xterm" : "left_ptr");
            if (drawn != NULL && drawn->image_count > 0) {
                image = drawn->images[0];
                buffer = wl_cursor_image_get_buffer(image);
                wl_surface_attach(window->cursor_surface, buffer, 0, 0);
                wl_surface_damage(window->cursor_surface, 0, 0, (int32_t)image->width, (int32_t)image->height);
                wl_surface_commit(window->cursor_surface);
                wl_pointer_set_cursor(window->pointer, window->serial, window->cursor_surface, (int32_t)image->hotspot_x, (int32_t)image->hotspot_y);
                wl_display_flush(window->wl);
            }
        }
    }
    return 0;
}

extern "C" int64_t aura_ax_begin(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_begin_nodes(&window->ax);
}

extern "C" int64_t aura_ax_add(int64_t handle, const char *role, const char *label, const char *value, int64_t checked, double x, double y, double w, double h, int64_t press) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_add_node(&window->ax, role, label, value, checked, x, y, w, h, press);
}

extern "C" int64_t aura_ax_commit(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    if (aura_ax_commit_nodes(&window->ax) < 0) {
        return -1;
    }
    publish_a11y(window, handle);
    return 0;
}

extern "C" int64_t aura_ax_count(int64_t handle) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_count_nodes(&window->ax);
}

extern "C" int64_t aura_ax_text_len(int64_t handle, int64_t index, int64_t field) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_text_len_node(&window->ax, index, field);
}

extern "C" int64_t aura_ax_text_byte(int64_t offset) {
    int i = 0;
    for (i = 0; i < AURA_MAX_WINDOWS; i++) {
        if (g_windows[i] != NULL && g_windows[i]->ax.query != NULL) {
            return aura_ax_text_byte_node(&g_windows[i]->ax, offset);
        }
    }
    return 0;
}

extern "C" int64_t aura_ax_checked(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_checked_node(&window->ax, index);
}

extern "C" int64_t aura_ax_press(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_press_node(&window->ax, index);
}

extern "C" double aura_ax_x(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_bound(&window->ax, index, 0);
}

extern "C" double aura_ax_y(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_bound(&window->ax, index, 1);
}

extern "C" double aura_ax_w(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_bound(&window->ax, index, 2);
}

extern "C" double aura_ax_h(int64_t handle, int64_t index) {
    AuraWindow *window = window_get(handle, 1);
    if (window == NULL) {
        return -1;
    }
    return aura_ax_bound(&window->ax, index, 3);
}

extern "C" int64_t aura_plugin_place(int64_t handle, int64_t plugin, double x, double y, double w, double h) {
    AuraWindow *window = window_get(handle, 1);
    int slot = -1;
    int i = 0;
    if (window == NULL || aura_plugin_check(plugin) < 0) {
        return -1;
    }
    for (i = 0; i < 8; i++) {
        if (window->plugin_ids[i] == plugin) {
            slot = i;
            break;
        }
        if (slot < 0 && window->plugin_ids[i] == 0) {
            slot = i;
        }
    }
    if (slot < 0) {
        return -1;
    }
    window->plugin_ids[slot] = plugin;
    if (window->shell == AURA_SHELL_X11 && window->display != NULL && window->plugin_windows[slot] == 0) {
        window->plugin_windows[slot] = XCreateSimpleWindow(window->display, window->xwindow, (int)x, (int)y, (unsigned)w, (unsigned)h, 0, 0, 0);
        XMapWindow(window->display, window->plugin_windows[slot]);
    } else if (window->shell == AURA_SHELL_X11 && window->plugin_windows[slot] != 0) {
        XMoveResizeWindow(window->display, window->plugin_windows[slot], (int)x, (int)y, (unsigned)w, (unsigned)h);
    } else if (window->shell == AURA_SHELL_WAYLAND && window->compositor != NULL && window->subcompositor != NULL && window->surface != NULL && window->plugin_surfaces[slot] == NULL) {
        struct wl_subsurface *sub = NULL;
        window->plugin_surfaces[slot] = wl_compositor_create_surface(window->compositor);
        sub = wl_subcompositor_get_subsurface(window->subcompositor, window->plugin_surfaces[slot], window->surface);
        wl_subsurface_set_position(sub, (int)x, (int)y);
        wl_surface_commit(window->plugin_surfaces[slot]);
    }
    return aura_plugin_frame(plugin, (int64_t)w, (int64_t)h);
}
