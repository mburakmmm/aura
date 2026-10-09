#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <process.h>
#include <windows.h>
#else
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#endif

typedef struct AuraNames {
    char **items;
    int count;
    int cap;
} AuraNames;

typedef struct AuraJob {
    int64_t generation;
    char *path;
} AuraJob;

static AuraJob *g_jobs = NULL;
static int g_job_count = 0;
static int g_job_cap = 0;
static int64_t g_wanted = 0;
static int64_t g_result_generation = 0;
static int64_t g_result_status = 0;
static AuraNames g_result = {NULL, 0, 0};
static int g_started = 0;

#if defined(_WIN32)
static CRITICAL_SECTION g_lock;
static CONDITION_VARIABLE g_cond;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
static HANDLE g_thread = NULL;

static BOOL CALLBACK aura_listing_init(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once;
    (void)param;
    (void)context;
    InitializeCriticalSection(&g_lock);
    InitializeConditionVariable(&g_cond);
    return TRUE;
}

static void aura_lock(void) {
    InitOnceExecuteOnce(&g_once, aura_listing_init, NULL, NULL);
    EnterCriticalSection(&g_lock);
}

static void aura_unlock(void) {
    LeaveCriticalSection(&g_lock);
}

static void aura_wait(void) {
    SleepConditionVariableCS(&g_cond, &g_lock, INFINITE);
}

static void aura_wake(void) {
    WakeConditionVariable(&g_cond);
}
#else
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static pthread_t g_thread;

static void aura_lock(void) {
    pthread_mutex_lock(&g_lock);
}

static void aura_unlock(void) {
    pthread_mutex_unlock(&g_lock);
}

static void aura_wait(void) {
    pthread_cond_wait(&g_cond, &g_lock);
}

static void aura_wake(void) {
    pthread_cond_signal(&g_cond);
}
#endif

static int aura_name_less(const void *left, const void *right) {
    const char *const *a = (const char *const *)left;
    const char *const *b = (const char *const *)right;
    if (*a == NULL && *b == NULL) {
        return 0;
    }
    if (*a == NULL) {
        return -1;
    }
    if (*b == NULL) {
        return 1;
    }
    return strcmp(*a, *b);
}

static void aura_names_sort(AuraNames *names) {
    if (names == NULL || names->count < 2 || names->items == NULL) {
        return;
    }
    qsort(names->items, (size_t)names->count, sizeof(char *), aura_name_less);
}

static char *aura_dup(const char *text) {
    size_t length = 0;
    char *copy = NULL;
    if (text == NULL) {
        text = "";
    }
    length = strlen(text);
    copy = (char *)malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

static void aura_names_free(AuraNames *names) {
    int i = 0;
    if (names == NULL) {
        return;
    }
    for (i = 0; i < names->count; i++) {
        free(names->items[i]);
    }
    free(names->items);
    names->items = NULL;
    names->count = 0;
    names->cap = 0;
}

static int aura_names_push(AuraNames *names, const char *text) {
    char *copy = NULL;
    if (names->count >= names->cap) {
        int cap = names->cap == 0 ? 8 : names->cap * 2;
        char **next = (char **)realloc(names->items, (size_t)cap * sizeof(char *));
        if (next == NULL) {
            return 0;
        }
        names->items = next;
        names->cap = cap;
    }
    copy = aura_dup(text);
    if (copy == NULL) {
        return 0;
    }
    names->items[names->count] = copy;
    names->count += 1;
    return 1;
}

static void aura_job_remove(int index) {
    int i = 0;
    if (index < 0 || index >= g_job_count) {
        return;
    }
    free(g_jobs[index].path);
    for (i = index; i < g_job_count - 1; i++) {
        g_jobs[i] = g_jobs[i + 1];
    }
    g_job_count -= 1;
}

#if defined(_WIN32)
static wchar_t *aura_wide(const char *text) {
    int count = 0;
    wchar_t *wide = NULL;
    if (text == NULL) {
        text = "";
    }
    count = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (count <= 0) {
        return NULL;
    }
    wide = (wchar_t *)malloc((size_t)count * sizeof(wchar_t));
    if (wide == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, count) <= 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static char *aura_utf8(const wchar_t *text) {
    int count = 0;
    char *utf8 = NULL;
    if (text == NULL) {
        return aura_dup("");
    }
    count = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (count <= 0) {
        return NULL;
    }
    utf8 = (char *)malloc((size_t)count);
    if (utf8 == NULL) {
        return NULL;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, count, NULL, NULL) <= 0) {
        free(utf8);
        return NULL;
    }
    return utf8;
}

static int aura_read_dir(const char *path, AuraNames *names) {
    wchar_t *root = NULL;
    wchar_t *pattern = NULL;
    size_t root_len = 0;
    WIN32_FIND_DATAW data;
    HANDLE found = INVALID_HANDLE_VALUE;
    int ok = 0;
    root = aura_wide(path);
    if (root == NULL) {
        return 0;
    }
    root_len = wcslen(root);
    pattern = (wchar_t *)malloc((root_len + 3) * sizeof(wchar_t));
    if (pattern == NULL) {
        free(root);
        return 0;
    }
    memcpy(pattern, root, root_len * sizeof(wchar_t));
    pattern[root_len] = L'\\';
    pattern[root_len + 1] = L'*';
    pattern[root_len + 2] = L'\0';
    found = FindFirstFileW(pattern, &data);
    if (found == INVALID_HANDLE_VALUE) {
        free(pattern);
        free(root);
        return 0;
    }
    ok = 1;
    do {
        char *name = NULL;
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }
        name = aura_utf8(data.cFileName);
        if (name == NULL || aura_names_push(names, name) == 0) {
            free(name);
            ok = 0;
            break;
        }
        free(name);
    } while (FindNextFileW(found, &data));
    FindClose(found);
    free(pattern);
    free(root);
    if (ok) {
        aura_names_sort(names);
    }
    return ok;
}
#else
static int aura_read_dir(const char *path, AuraNames *names) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    if (path == NULL) {
        return 0;
    }
    dir = opendir(path);
    if (dir == NULL) {
        return 0;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (aura_names_push(names, entry->d_name) == 0) {
            closedir(dir);
            return 0;
        }
    }
    closedir(dir);
    aura_names_sort(names);
    return 1;
}
#endif

static void aura_publish(int64_t generation, int64_t status, AuraNames *names) {
    aura_names_free(&g_result);
    if (status == 1 && names != NULL) {
        g_result = *names;
        memset(names, 0, sizeof(AuraNames));
    } else if (names != NULL) {
        aura_names_free(names);
    }
    g_result_generation = generation;
    g_result_status = status;
}

#if defined(_WIN32)
static unsigned __stdcall aura_listing_main(void *arg)
#else
static void *aura_listing_main(void *arg)
#endif
{
    (void)arg;
    for (;;) {
        AuraJob job;
        AuraNames names;
        int status = 2;
        int64_t wanted = 0;
        memset(&job, 0, sizeof(job));
        memset(&names, 0, sizeof(names));
        aura_lock();
        while (g_job_count == 0) {
            aura_wait();
        }
        job = g_jobs[0];
        g_jobs[0].path = NULL;
        aura_job_remove(0);
        aura_unlock();
        if (aura_read_dir(job.path, &names)) {
            status = 1;
        }
        aura_lock();
        wanted = g_wanted;
        if (job.generation == wanted) {
            aura_publish(job.generation, status, &names);
        } else {
            aura_names_free(&names);
        }
        aura_unlock();
        free(job.path);
    }
#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static void aura_ensure_thread(void) {
    if (g_started != 0) {
        return;
    }
#if defined(_WIN32)
    g_thread = (HANDLE)_beginthreadex(NULL, 0, aura_listing_main, NULL, 0, NULL);
    if (g_thread == NULL) {
        return;
    }
#else
    if (pthread_create(&g_thread, NULL, aura_listing_main, NULL) != 0) {
        return;
    }
#endif
    g_started = 1;
}

void aura_listing_lock(void) {
    aura_lock();
}

void aura_listing_unlock(void) {
    aura_unlock();
}

int64_t aura_listing_submit(int64_t generation, const char *path) {
    AuraJob *next = NULL;
    aura_lock();
    if (g_job_count >= g_job_cap) {
        int cap = g_job_cap == 0 ? 4 : g_job_cap * 2;
        next = (AuraJob *)realloc(g_jobs, (size_t)cap * sizeof(AuraJob));
        if (next == NULL) {
            aura_unlock();
            return 0;
        }
        g_jobs = next;
        g_job_cap = cap;
    }
    g_jobs[g_job_count].generation = generation;
    g_jobs[g_job_count].path = aura_dup(path);
    if (g_jobs[g_job_count].path == NULL) {
        aura_unlock();
        return 0;
    }
    g_job_count += 1;
    g_wanted = generation;
    aura_ensure_thread();
    aura_wake();
    aura_unlock();
    return 1;
}

int64_t aura_listing_result_generation(void) {
    return g_result_generation;
}

int64_t aura_listing_result_status(void) {
    return g_result_status;
}

int64_t aura_listing_count(void) {
    return g_result.count;
}

int64_t aura_listing_name_len(int64_t index) {
    if (index < 0 || index >= g_result.count || g_result.items[index] == NULL) {
        return 0;
    }
    return (int64_t)strlen(g_result.items[index]);
}

int64_t aura_listing_name_byte(int64_t index, int64_t offset) {
    const char *text = NULL;
    size_t length = 0;
    if (index < 0 || index >= g_result.count || offset < 0) {
        return 0;
    }
    text = g_result.items[index];
    if (text == NULL) {
        return 0;
    }
    length = strlen(text);
    if ((size_t)offset >= length) {
        return 0;
    }
    return (unsigned char)text[offset];
}

int64_t aura_listing_remove_dir(const char *path) {
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
#if defined(_WIN32)
    if (_rmdir(path) != 0) {
        return 0;
    }
#else
    if (rmdir(path) != 0) {
        return 0;
    }
#endif
    return 1;
}
