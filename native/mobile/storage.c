#include "storage.h"

#include "build_identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
static pthread_mutex_t g_storage_mu = PTHREAD_MUTEX_INITIALIZER;
#define STORAGE_LOCK() pthread_mutex_lock(&g_storage_mu)
#define STORAGE_UNLOCK() pthread_mutex_unlock(&g_storage_mu)
#else
#define STORAGE_LOCK()
#define STORAGE_UNLOCK()
#endif

static char *g_roots[5];

static void free_roots(void) {
    int i = 0;
    while (i < 5) {
        free(g_roots[i]);
        g_roots[i] = NULL;
        i += 1;
    }
}

static int copy_joined(const char *root, const char *path, char *out, size_t out_len) {
    int written = 0;
    if (root == NULL || root[0] == 0) {
        written = snprintf(out, out_len, "%s", path);
    } else {
        written = snprintf(out, out_len, "%s/%s", root, path);
    }
    if (written < 0 || (size_t)written >= out_len) {
        return -1;
    }
    return 0;
}

int aura_storage_set(const AuraStorageSpec *spec) {
    char *copies[5];
    const char *values[5];
    int i = 0;
    if (spec == NULL) {
        return -1;
    }
    values[0] = spec->documents;
    values[1] = spec->support;
    values[2] = spec->cache;
    values[3] = spec->temp_dir;
    values[4] = spec->assets;
    while (i < 5) {
        if (values[i] == NULL || values[i][0] == 0) {
            return -1;
        }
        copies[i] = strdup(values[i]);
        if (copies[i] == NULL) {
            while (i > 0) {
                i -= 1;
                free(copies[i]);
            }
            return -1;
        }
        i += 1;
    }
    STORAGE_LOCK();
    free_roots();
    i = 0;
    while (i < 5) {
        g_roots[i] = copies[i];
        i += 1;
    }
    STORAGE_UNLOCK();
    return 0;
}

void aura_storage_clear(void) {
    STORAGE_LOCK();
    free_roots();
    STORAGE_UNLOCK();
}

const char *aura_storage_root(int kind) {
    if (kind < 0 || kind > AURA_STORAGE_ASSETS) {
        return NULL;
    }
    return g_roots[kind];
}

int aura_storage_documents_len(void) {
    const char *path = aura_storage_root(AURA_STORAGE_DOCUMENTS);
    if (path == NULL) {
        return 0;
    }
    return (int)strlen(path);
}

int aura_storage_documents_byte(int offset) {
    const char *path = aura_storage_root(AURA_STORAGE_DOCUMENTS);
    int length = 0;
    if (path == NULL || offset < 0) {
        return 0;
    }
    length = (int)strlen(path);
    if (offset >= length) {
        return 0;
    }
    return (unsigned char)path[offset];
}

int aura_storage_is_backup_excluded(int kind) {
    if (kind == AURA_STORAGE_CACHE || kind == AURA_STORAGE_TEMP || kind == AURA_STORAGE_ASSETS) {
        return 1;
    }
    return 0;
}

int aura_storage_resolve_asset(const char *path, char *out, size_t out_len) {
    int code = 0;
    if (out == NULL || out_len == 0 || path == NULL || path[0] == 0 || strstr(path, "..") != NULL) {
        return -1;
    }
    STORAGE_LOCK();
    if (path[0] == '/') {
        code = copy_joined(NULL, path, out, out_len);
    } else if (strchr(path, '\\') != NULL || strchr(path, ':') != NULL) {
        code = -1;
    } else {
        code = copy_joined(g_roots[AURA_STORAGE_ASSETS], path, out, out_len);
    }
    STORAGE_UNLOCK();
    return code;
}

static char *g_route_memory = NULL;

static int route_path(char *out, size_t out_len) {
    if (g_roots[AURA_STORAGE_SUPPORT] == NULL || g_roots[AURA_STORAGE_SUPPORT][0] == 0) {
        return -1;
    }
    return copy_joined(g_roots[AURA_STORAGE_SUPPORT], "aura-route-stack", out, out_len);
}

static int read_route_file_locked(char **out, size_t *out_len) {
    char path[4096];
    FILE *file = NULL;
    long size = 0;
    char *buf = NULL;
    if (out == NULL || out_len == NULL || route_path(path, sizeof(path)) != 0) {
        return -1;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    size = ftell(file);
    if (size < 10 || size > 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    buf = (char *)malloc((size_t)size + 1);
    if (buf == NULL || fread(buf, 1, (size_t)size, file) != (size_t)size) {
        free(buf);
        fclose(file);
        return -1;
    }
    fclose(file);
    if (memcmp(buf, "AURA-RS:1:", 10) != 0) {
        free(buf);
        return -1;
    }
    buf[size] = 0;
    *out = buf;
    *out_len = (size_t)size;
    return 0;
}

int aura_storage_write_route_snapshot(const void *bytes, size_t len) {
    char path[4096];
    FILE *file = NULL;
    size_t wrote = 0;
    if (bytes == NULL || len < 10 || memcmp(bytes, "AURA-RS:1:", 10) != 0) {
        return -1;
    }
    STORAGE_LOCK();
    if (route_path(path, sizeof(path)) != 0) {
        STORAGE_UNLOCK();
        return -1;
    }
    file = fopen(path, "wb");
    if (file != NULL) {
        wrote = fwrite(bytes, 1, len, file);
        fclose(file);
    }
    STORAGE_UNLOCK();
    if (file == NULL || wrote != len) {
        return -1;
    }
    return 0;
}

int aura_storage_read_route_snapshot(void *bytes, size_t cap, size_t *out_len) {
    char path[4096];
    FILE *file = NULL;
    size_t read_count = 0;
    if (bytes == NULL || out_len == NULL || cap < 10) {
        return -1;
    }
    STORAGE_LOCK();
    if (route_path(path, sizeof(path)) != 0) {
        STORAGE_UNLOCK();
        return -1;
    }
    file = fopen(path, "rb");
    if (file != NULL) {
        read_count = fread(bytes, 1, cap, file);
        fclose(file);
    }
    STORAGE_UNLOCK();
    if (file == NULL || read_count < 10 || memcmp(bytes, "AURA-RS:1:", 10) != 0) {
        return -1;
    }
    *out_len = read_count;
    return 0;
}

int aura_route_snapshot_save(const char *text) {
    size_t len = 0;
    char *copy = NULL;
    if (text == NULL) {
        return -1;
    }
    len = strlen(text);
    if (len < 10 || memcmp(text, "AURA-RS:1:", 10) != 0) {
        return -1;
    }
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, text, len + 1);
    STORAGE_LOCK();
    free(g_route_memory);
    g_route_memory = copy;
    STORAGE_UNLOCK();
    (void)aura_storage_write_route_snapshot(text, len);
    return 0;
}

int aura_route_snapshot_load_len(void) {
    char *file_bytes = NULL;
    size_t file_len = 0;
    int len = 0;
    STORAGE_LOCK();
    if (read_route_file_locked(&file_bytes, &file_len) == 0) {
        free(g_route_memory);
        g_route_memory = file_bytes;
        len = (int)file_len;
    } else if (g_route_memory != NULL) {
        len = (int)strlen(g_route_memory);
    }
    STORAGE_UNLOCK();
    return len;
}

int aura_route_snapshot_load_byte(int index) {
    int value = -1;
    STORAGE_LOCK();
    if (g_route_memory != NULL && index >= 0 && (size_t)index < strlen(g_route_memory)) {
        value = (unsigned char)g_route_memory[index];
    }
    STORAGE_UNLOCK();
    return value;
}

int aura_scene_restoration_accept(const char *format, int version) {
    return format != NULL && strcmp(format, "AURA-RS") == 0 && version == 1;
}

int aura_scene_restoration_prepare(const char *format, int version) {
    if (aura_scene_restoration_accept(format, version)) {
        return 1;
    }
    aura_route_snapshot_clear();
    return 0;
}

int aura_route_snapshot_clear(void) {
    char path[4096];
    STORAGE_LOCK();
    free(g_route_memory);
    g_route_memory = NULL;
    if (route_path(path, sizeof(path)) == 0) {
        remove(path);
    }
    STORAGE_UNLOCK();
    return 0;
}

void aura_route_snapshot_release_memory(void) {
    STORAGE_LOCK();
    free(g_route_memory);
    g_route_memory = NULL;
    STORAGE_UNLOCK();
}

#if !defined(_WIN32)
static int same_path(const char *left, const char *right) {
    return left != NULL && right != NULL && strcmp(left, right) == 0;
}

static int purge_children(const char *path) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    int code = 0;
    dir = opendir(path);
    if (dir == NULL) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        char child[4096];
        struct stat info;
        int written = 0;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        written = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (written < 0 || (size_t)written >= sizeof(child) || lstat(child, &info) != 0) {
            code = -1;
            break;
        }
        if (S_ISLNK(info.st_mode)) {
            code = -1;
            break;
        }
        if (S_ISDIR(info.st_mode)) {
            if (purge_children(child) != 0 || rmdir(child) != 0) {
                code = -1;
                break;
            }
        } else if (unlink(child) != 0) {
            code = -1;
            break;
        }
    }
    closedir(dir);
    return code;
}
#endif

int aura_storage_purge_cache(void) {
#if defined(_WIN32)
    return -1;
#else
    const char *cache = NULL;
    int code = 0;
    STORAGE_LOCK();
    cache = g_roots[AURA_STORAGE_CACHE];
    if (cache == NULL || cache[0] != '/' || strcmp(cache, "/") == 0 || same_path(cache, g_roots[AURA_STORAGE_DOCUMENTS]) || same_path(cache, g_roots[AURA_STORAGE_SUPPORT]) || same_path(cache, g_roots[AURA_STORAGE_ASSETS])) {
        STORAGE_UNLOCK();
        return -1;
    }
    code = purge_children(cache);
    STORAGE_UNLOCK();
    return code;
#endif
}

int aura_storage_stamp_current(const char *path) {
    char buffer[64];
    FILE *file = NULL;
    size_t read_count = 0;
    size_t expect = 0;
    if (path == NULL || path[0] == 0) {
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    read_count = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);
    expect = strlen(AURA_VERSION);
    if (read_count != expect || memcmp(buffer, AURA_VERSION, expect) != 0) {
        return 0;
    }
    return 1;
}
