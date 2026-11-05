#ifndef AURA_MOBILE_STORAGE_H
#define AURA_MOBILE_STORAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AURA_STORAGE_DOCUMENTS = 0,
    AURA_STORAGE_SUPPORT = 1,
    AURA_STORAGE_CACHE = 2,
    AURA_STORAGE_TEMP = 3,
    AURA_STORAGE_ASSETS = 4
};

typedef struct AuraStorageSpec {
    const char *documents;
    const char *support;
    const char *cache;
    const char *temp_dir;
    const char *assets;
} AuraStorageSpec;

int aura_storage_set(const AuraStorageSpec *spec);
void aura_storage_clear(void);
const char *aura_storage_root(int kind);
int aura_storage_documents_len(void);
int aura_storage_documents_byte(int offset);
int aura_storage_is_backup_excluded(int kind);
int aura_storage_resolve_asset(const char *path, char *out, size_t out_len);
int aura_storage_write_route_snapshot(const void *bytes, size_t len);
int aura_storage_read_route_snapshot(void *bytes, size_t cap, size_t *out_len);
int aura_route_snapshot_save(const char *text);
int aura_route_snapshot_load_len(void);
int aura_route_snapshot_load_byte(int index);
int aura_route_snapshot_clear(void);
void aura_route_snapshot_release_memory(void);
/* Scene restoration names the canonical file format. It does not carry the
   route stack. Version 1 accepts AURA-RS; any other pair rejects it. */
int aura_scene_restoration_accept(const char *format, int version);
/* Returns 1 when the saved snapshot may be read. An activity that names a
   different format or version clears the snapshot and returns 0. */
int aura_scene_restoration_prepare(const char *format, int version);
int aura_storage_purge_cache(void);
int aura_storage_stamp_current(const char *path);

#ifdef __cplusplus
}
#endif

#endif
