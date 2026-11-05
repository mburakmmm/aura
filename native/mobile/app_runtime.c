#include "app_runtime.h"

#include "build_identity.h"
#include "nox_embed.h"
#include "shell.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct AuraRuntimeState {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int state;
    int stop_requested;
    void *app;
    NoxEmbedSlot slot;
    AuraRuntimeLog log;
} AuraRuntimeState;

static AuraRuntimeState g_runtime = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    AURA_RUNTIME_IDLE,
    0,
    NULL,
    {NOX_EMBED_ABI, NULL},
    NULL
};

static void runtime_log(int is_error, const char *message) {
    AuraRuntimeLog log = NULL;
    char line[320];
    pthread_mutex_lock(&g_runtime.mu);
    log = g_runtime.log;
    pthread_mutex_unlock(&g_runtime.mu);
    if (log == NULL) {
        return;
    }
    if (is_error) {
        snprintf(line, sizeof(line), "aura %s embed-abi %d: %s", AURA_VERSION, AURA_EMBED_ABI, message != NULL ? message : "");
        log(1, line);
        return;
    }
    log(0, message);
}

static void runtime_finish(int state) {
    pthread_mutex_lock(&g_runtime.mu);
    g_runtime.app = NULL;
    g_runtime.slot.app = NULL;
    g_runtime.state = state;
    g_runtime.stop_requested = 0;
    pthread_cond_broadcast(&g_runtime.cv);
    pthread_mutex_unlock(&g_runtime.mu);
}

static void *runtime_thread(void *unused) {
    NoxHostConfig config;
    void *app = NULL;
    void *runtime = NULL;
    void *exception = NULL;
    int create_code = 0;
    int start_code = 0;
    int stop_requested = 0;
    int failed = 0;
    (void)unused;
    if (nox_embed_abi_version() != NOX_EMBED_ABI) {
        runtime_log(1, "Nox embed ABI mismatch");
        runtime_finish(AURA_RUNTIME_FAILED);
        return NULL;
    }

    memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.abi_version = NOX_EMBED_ABI;
    config.slot = &g_runtime.slot;
    config.documents = aura_storage_root(AURA_STORAGE_DOCUMENTS);
    config.support = aura_storage_root(AURA_STORAGE_SUPPORT);
    config.cache = aura_storage_root(AURA_STORAGE_CACHE);
    config.temp_dir = aura_storage_root(AURA_STORAGE_TEMP);
    create_code = nox_app_create(&config, &app);
    if (create_code != NOX_EMBED_OK || app == NULL) {
        runtime_log(1, "nox_app_create failed");
        if (g_runtime.slot.app != NULL) {
            nox_app_destroy(g_runtime.slot.app);
        } else if (app != NULL) {
            nox_app_destroy(app);
        }
        runtime_finish(AURA_RUNTIME_FAILED);
        return NULL;
    }

    pthread_mutex_lock(&g_runtime.mu);
    g_runtime.app = app;
    stop_requested = g_runtime.stop_requested;
    g_runtime.state = stop_requested ? AURA_RUNTIME_STOPPING : AURA_RUNTIME_RUNNING;
    pthread_mutex_unlock(&g_runtime.mu);

    nox_embed_bind(app);
    if (stop_requested) {
        nox_app_request_stop(app);
    }
    start_code = nox_app_start(app);
    if (start_code != NOX_EMBED_OK) {
        failed = 1;
        runtime_log(1, start_code == NOX_EMBED_ERR_EXCEPTION ? "Nox entry raised an exception" : "nox_app_start failed");
    }
    runtime = nox_app_runtime(app);
    if (runtime != NULL && nox_exception_pending(runtime)) {
        exception = nox_exception_take(runtime);
        runtime_log(1, "Nox entry left a pending exception");
        nox_exception_release(runtime, exception);
        failed = 1;
    }

    pthread_mutex_lock(&g_runtime.mu);
    g_runtime.app = NULL;
    pthread_mutex_unlock(&g_runtime.mu);
    if (g_runtime.slot.app != NULL && nox_app_destroy(g_runtime.slot.app) != NOX_EMBED_OK) {
        runtime_log(1, "nox_app_destroy failed");
        failed = 1;
    }
    runtime_finish(failed ? AURA_RUNTIME_FAILED : AURA_RUNTIME_STOPPED);
    return NULL;
}

int aura_mobile_runtime_start(const AuraStorageSpec *spec, AuraRuntimeLog log) {
    pthread_t thread;
    int code = 0;
    if (aura_storage_set(spec) != 0) {
        return -1;
    }
    pthread_mutex_lock(&g_runtime.mu);
    if (g_runtime.state == AURA_RUNTIME_STARTING || g_runtime.state == AURA_RUNTIME_RUNNING || g_runtime.state == AURA_RUNTIME_STOPPING) {
        pthread_mutex_unlock(&g_runtime.mu);
        return 0;
    }
    g_runtime.log = log;
    g_runtime.stop_requested = 0;
    g_runtime.slot.abi_version = NOX_EMBED_ABI;
    g_runtime.slot.app = NULL;
    g_runtime.state = AURA_RUNTIME_STARTING;
    code = pthread_create(&thread, NULL, runtime_thread, NULL);
    if (code == 0) {
        pthread_detach(thread);
    } else {
        g_runtime.state = AURA_RUNTIME_FAILED;
        aura_storage_clear();
        pthread_cond_broadcast(&g_runtime.cv);
    }
    pthread_mutex_unlock(&g_runtime.mu);
    return code == 0 ? 0 : -1;
}

void aura_mobile_runtime_request_stop(void) {
    int should_close = 0;
    pthread_mutex_lock(&g_runtime.mu);
    if (g_runtime.state == AURA_RUNTIME_STARTING || g_runtime.state == AURA_RUNTIME_RUNNING) {
        g_runtime.stop_requested = 1;
        g_runtime.state = AURA_RUNTIME_STOPPING;
        should_close = 1;
        if (g_runtime.app != NULL) {
            nox_app_request_stop(g_runtime.app);
        }
    }
    pthread_mutex_unlock(&g_runtime.mu);
    if (should_close) {
        aura_shell_request_close_all();
    }
}

int aura_mobile_runtime_state(void) {
    int state = AURA_RUNTIME_IDLE;
    pthread_mutex_lock(&g_runtime.mu);
    state = g_runtime.state;
    pthread_mutex_unlock(&g_runtime.mu);
    return state;
}

int aura_mobile_runtime_wait(int timeout_ms) {
    struct timespec deadline;
    int state = AURA_RUNTIME_IDLE;
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec += 1;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&g_runtime.mu);
    while (g_runtime.state == AURA_RUNTIME_STARTING || g_runtime.state == AURA_RUNTIME_RUNNING || g_runtime.state == AURA_RUNTIME_STOPPING) {
        if (pthread_cond_timedwait(&g_runtime.cv, &g_runtime.mu, &deadline) != 0) {
            break;
        }
    }
    state = g_runtime.state;
    pthread_mutex_unlock(&g_runtime.mu);
    return state;
}
