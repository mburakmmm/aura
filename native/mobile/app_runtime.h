#ifndef AURA_MOBILE_APP_RUNTIME_H
#define AURA_MOBILE_APP_RUNTIME_H

#include "storage.h"

enum {
    AURA_RUNTIME_IDLE = 0,
    AURA_RUNTIME_STARTING = 1,
    AURA_RUNTIME_RUNNING = 2,
    AURA_RUNTIME_STOPPING = 3,
    AURA_RUNTIME_STOPPED = 4,
    AURA_RUNTIME_FAILED = 5
};

typedef void (*AuraRuntimeLog)(int is_error, const char *message);

int aura_mobile_runtime_start(const AuraStorageSpec *spec, AuraRuntimeLog log);
void aura_mobile_runtime_request_stop(void);
int aura_mobile_runtime_state(void);
int aura_mobile_runtime_wait(int timeout_ms);

#endif
