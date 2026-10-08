#ifndef AURA_PLUGIN_H
#define AURA_PLUGIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int64_t aura_plugin_load(const char *path);
int64_t aura_plugin_close(int64_t handle);
int64_t aura_plugin_frame(int64_t handle, int64_t width, int64_t height);
int64_t aura_plugin_event(int64_t handle, int64_t kind, double x, double y);
int64_t aura_plugin_check(int64_t handle);

#ifdef __cplusplus
}
#endif

#endif
