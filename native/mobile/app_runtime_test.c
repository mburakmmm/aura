#include "app_runtime.h"
#include "build_identity.h"
#include "nox_embed.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static atomic_int g_mode;
static atomic_int g_start_entered;
static atomic_int g_stop_requested;
static atomic_int g_create_count;
static atomic_int g_destroy_count;
static atomic_int g_release_count;
static atomic_int g_close_count;
static atomic_int g_error_logs;
static char g_last_error[128];
static NoxEmbedSlot *g_slot;
static int g_app_token;
static int g_exception_token;

void aura_shell_request_close_all(void) {
    atomic_fetch_add(&g_close_count, 1);
}

int32_t nox_embed_abi_version(void) {
    if (atomic_load(&g_mode) == 6) {
        return NOX_EMBED_ABI + 1;
    }
    return NOX_EMBED_ABI;
}

int32_t nox_app_create(const NoxHostConfig *config, void **out_app) {
    atomic_fetch_add(&g_create_count, 1);
    if (atomic_load(&g_mode) == 1) {
        *out_app = NULL;
        return NOX_EMBED_ERR_NOMEM;
    }
    g_slot = config->slot;
    *out_app = &g_app_token;
    g_slot->app = *out_app;
    if (atomic_load(&g_mode) == 5) {
        return NOX_EMBED_ERR_NOMEM;
    }
    return NOX_EMBED_OK;
}

int32_t nox_app_start(void *app) {
    (void)app;
    atomic_store(&g_start_entered, 1);
    if (atomic_load(&g_mode) == 2) {
        return NOX_EMBED_ERR_STATE;
    }
    if (atomic_load(&g_mode) == 3) {
        return NOX_EMBED_ERR_EXCEPTION;
    }
    while (atomic_load(&g_mode) == 4 && !atomic_load(&g_stop_requested)) {
        usleep(1000);
    }
    return NOX_EMBED_OK;
}

void *nox_app_runtime(void *app) { return app; }
int32_t nox_app_post_event(void *app, int32_t kind, const char *bytes, uint32_t len) { (void)app; (void)kind; (void)bytes; (void)len; return 0; }
int32_t nox_app_pump(void *app, uint64_t time) { (void)app; (void)time; return 0; }

int32_t nox_app_request_stop(void *app) {
    (void)app;
    atomic_store(&g_stop_requested, 1);
    return NOX_EMBED_OK;
}

int32_t nox_app_destroy(void *app) {
    if (app != &g_app_token) return NOX_EMBED_ERR_INVAL;
    atomic_fetch_add(&g_destroy_count, 1);
    if (g_slot != NULL) g_slot->app = NULL;
    return NOX_EMBED_OK;
}

void nox_embed_bind(void *app) { (void)app; }
void nox_app_set_entry(void *app, NoxEmbedEntry entry) { (void)app; (void)entry; }
void nox_app_set_event_fn(void *app, NoxEmbedEventFn fn) { (void)app; (void)fn; }
char *nox_str_from_cstr(void *rt, const char *text) { (void)rt; return (char *)text; }
void nox_str_release(void *rt, char *text) { (void)rt; (void)text; }

int32_t nox_exception_pending(void *rt) {
    (void)rt;
    return atomic_load(&g_mode) == 3;
}

void *nox_exception_take(void *rt) { (void)rt; return &g_exception_token; }

void nox_exception_release(void *rt, void *exception) {
    (void)rt;
    if (exception == &g_exception_token) atomic_fetch_add(&g_release_count, 1);
}

static void test_log(int is_error, const char *message) {
    if (is_error) {
        atomic_fetch_add(&g_error_logs, 1);
        snprintf(g_last_error, sizeof(g_last_error), "%s", message == NULL ? "" : message);
    }
}

static AuraStorageSpec dot_spec(void) {
    AuraStorageSpec spec;
    spec.documents = ".";
    spec.support = ".";
    spec.cache = ".";
    spec.temp_dir = ".";
    spec.assets = ".";
    return spec;
}

static int run_mode(int mode, int expected_state) {
    AuraStorageSpec spec = dot_spec();
    atomic_store(&g_mode, mode);
    atomic_store(&g_start_entered, 0);
    atomic_store(&g_stop_requested, 0);
    if (aura_mobile_runtime_start(&spec, test_log) != 0) return 0;
    return aura_mobile_runtime_wait(2000) == expected_state;
}

int main(void) {
    int i = 0;
    int creates = 0;
    if (!run_mode(0, AURA_RUNTIME_STOPPED)) return 1;
    if (atomic_load(&g_create_count) != 1 || atomic_load(&g_destroy_count) != 1) return 2;
    if (!run_mode(2, AURA_RUNTIME_FAILED)) return 3;
    if (atomic_load(&g_destroy_count) != 2) return 4;
    if (!run_mode(3, AURA_RUNTIME_FAILED)) return 5;
    if (atomic_load(&g_release_count) != 1 || atomic_load(&g_destroy_count) != 3) return 6;
    if (!run_mode(1, AURA_RUNTIME_FAILED)) return 7;
    if (atomic_load(&g_destroy_count) != 3) return 8;
    if (!run_mode(5, AURA_RUNTIME_FAILED)) return 9;
    if (atomic_load(&g_destroy_count) != 4) return 10;

    atomic_store(&g_mode, 4);
    atomic_store(&g_start_entered, 0);
    atomic_store(&g_stop_requested, 0);
    AuraStorageSpec spec = dot_spec();
    if (aura_mobile_runtime_start(&spec, test_log) != 0) return 11;
    while (!atomic_load(&g_start_entered) && i < 2000) {
        usleep(1000);
        i += 1;
    }
    if (!atomic_load(&g_start_entered)) return 12;
    aura_mobile_runtime_request_stop();
    aura_mobile_runtime_request_stop();
    if (aura_mobile_runtime_wait(2000) != AURA_RUNTIME_STOPPED) return 13;
    if (!atomic_load(&g_stop_requested)) return 14;
    if (atomic_load(&g_destroy_count) != 5) return 15;
    if (atomic_load(&g_close_count) != 1) return 16;
    if (atomic_load(&g_error_logs) < 4) return 17;
    creates = atomic_load(&g_create_count);
    if (!run_mode(6, AURA_RUNTIME_FAILED)) return 18;
    if (atomic_load(&g_create_count) != creates) return 19;
    char expected_error[128];
    snprintf(expected_error, sizeof(expected_error), "aura %s embed-abi %d: Nox embed ABI mismatch", AURA_VERSION, AURA_EMBED_ABI);
    if (strcmp(g_last_error, expected_error) != 0) return 20;
    {
        char before[PATH_MAX];
        char after[PATH_MAX];
        AuraStorageSpec spec = dot_spec();
        char assets[] = "/tmp/aura-runtime-assets-XXXXXX";
        if (getcwd(before, sizeof(before)) == NULL || mkdtemp(assets) == NULL) return 21;
        spec.assets = assets;
        atomic_store(&g_mode, 0);
        if (aura_mobile_runtime_start(&spec, test_log) != 0) return 22;
        if (aura_mobile_runtime_wait(2000) != AURA_RUNTIME_STOPPED) return 23;
        if (getcwd(after, sizeof(after)) == NULL || strcmp(before, after) != 0) return 24;
    }
    printf("mobile app runtime ok\n");
    return 0;
}
