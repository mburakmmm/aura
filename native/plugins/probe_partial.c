#include <stdint.h>

#ifdef _WIN32
#define AURA_PROBE_EXPORT __declspec(dllexport)
#else
#define AURA_PROBE_EXPORT
#endif

AURA_PROBE_EXPORT int64_t aura_plugin_probe_frame(int64_t width, int64_t height) {
    return width + height;
}
