#include "plugin.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE AuraLib;
#else
#include <dlfcn.h>
typedef void *AuraLib;
#endif

typedef int64_t (*AuraProbeFrame)(int64_t, int64_t);
typedef int64_t (*AuraProbeEvent)(int64_t, double, double);
typedef int64_t (*AuraProbeClose)(void);

#define AURA_MAX_PLUGINS 8

typedef struct AuraPlugin {
    int alive;
    AuraLib library;
    AuraProbeFrame frame;
    AuraProbeEvent event;
    AuraProbeClose close_fn;
} AuraPlugin;

static AuraPlugin g_plugins[AURA_MAX_PLUGINS];

static void *aura_symbol(AuraLib library, const char *name) {
#ifdef _WIN32
    return (void *)GetProcAddress(library, name);
#else
    return dlsym(library, name);
#endif
}

static void aura_lib_close(AuraLib library) {
    if (library == NULL) {
        return;
    }
#ifdef _WIN32
    FreeLibrary(library);
#else
    dlclose(library);
#endif
}

static AuraLib aura_lib_open(const char *path) {
#ifdef _WIN32
    return LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW);
#endif
}

int64_t aura_plugin_load(const char *path) {
    int slot = -1;
    int i = 0;
    AuraLib library = NULL;
    AuraProbeFrame frame = NULL;
    AuraProbeEvent event = NULL;
    AuraProbeClose close_fn = NULL;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    for (i = 0; i < AURA_MAX_PLUGINS; i++) {
        if (!g_plugins[i].alive) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return 0;
    }
    library = aura_lib_open(path);
    if (library == NULL) {
        return 0;
    }
    frame = (AuraProbeFrame)aura_symbol(library, "aura_plugin_probe_frame");
    event = (AuraProbeEvent)aura_symbol(library, "aura_plugin_probe_event");
    close_fn = (AuraProbeClose)aura_symbol(library, "aura_plugin_probe_close");
    if (frame == NULL || event == NULL || close_fn == NULL) {
        aura_lib_close(library);
        return 0;
    }
    memset(&g_plugins[slot], 0, sizeof(AuraPlugin));
    g_plugins[slot].alive = 1;
    g_plugins[slot].library = library;
    g_plugins[slot].frame = frame;
    g_plugins[slot].event = event;
    g_plugins[slot].close_fn = close_fn;
    return (int64_t)slot + 1;
}

int64_t aura_plugin_check(int64_t handle) {
    if (handle <= 0 || handle > AURA_MAX_PLUGINS) {
        return -1;
    }
    if (!g_plugins[handle - 1].alive) {
        return -1;
    }
    return 0;
}

int64_t aura_plugin_close(int64_t handle) {
    AuraPlugin *plugin = NULL;
    if (handle <= 0 || handle > AURA_MAX_PLUGINS) {
        return -1;
    }
    plugin = &g_plugins[handle - 1];
    if (!plugin->alive) {
        return 0;
    }
    if (plugin->close_fn != NULL) {
        plugin->close_fn();
    }
    aura_lib_close(plugin->library);
    memset(plugin, 0, sizeof(AuraPlugin));
    return 0;
}

int64_t aura_plugin_frame(int64_t handle, int64_t width, int64_t height) {
    AuraPlugin *plugin = NULL;
    if (aura_plugin_check(handle) < 0) {
        return -1;
    }
    plugin = &g_plugins[handle - 1];
    return plugin->frame(width, height);
}

int64_t aura_plugin_event(int64_t handle, int64_t kind, double x, double y) {
    AuraPlugin *plugin = NULL;
    if (aura_plugin_check(handle) < 0) {
        return -1;
    }
    plugin = &g_plugins[handle - 1];
    return plugin->event(kind, x, y);
}
