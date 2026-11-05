#include "storage.h"

#include "build_identity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    size_t len = 0;
    if (file == NULL) {
        return -1;
    }
    len = strlen(text);
    if (fwrite(text, 1, len, file) != len) {
        fclose(file);
        return -1;
    }
    fclose(file);
    return 0;
}

int main(void) {
    char root[] = "/tmp/aura-storage-XXXXXX";
    char documents[256];
    char support[256];
    char cache[256];
    char temp_dir[256];
    char assets[256];
    char resolved[512];
    char snapshot_in[64];
    char snapshot_out[64];
    char stamp[256];
    char cache_file[300];
    char kept[300];
    size_t snapshot_len = 0;
    AuraStorageSpec spec;
    if (mkdtemp(root) == NULL) {
        return 1;
    }
    snprintf(documents, sizeof(documents), "%s/documents", root);
    snprintf(support, sizeof(support), "%s/support", root);
    snprintf(cache, sizeof(cache), "%s/cache", root);
    snprintf(temp_dir, sizeof(temp_dir), "%s/tmp", root);
    snprintf(assets, sizeof(assets), "%s/assets", root);
    if (mkdir(documents, 0700) != 0 || mkdir(support, 0700) != 0 || mkdir(cache, 0700) != 0 || mkdir(temp_dir, 0700) != 0 || mkdir(assets, 0700) != 0) {
        return 2;
    }
    spec.documents = documents;
    spec.support = support;
    spec.cache = cache;
    spec.temp_dir = temp_dir;
    spec.assets = assets;
    if (aura_storage_set(&spec) != 0) {
        return 3;
    }
    if (aura_storage_is_backup_excluded(AURA_STORAGE_DOCUMENTS) != 0 || aura_storage_is_backup_excluded(AURA_STORAGE_SUPPORT) != 0) {
        return 4;
    }
    if (aura_storage_is_backup_excluded(AURA_STORAGE_CACHE) != 1 || aura_storage_is_backup_excluded(AURA_STORAGE_TEMP) != 1 || aura_storage_is_backup_excluded(AURA_STORAGE_ASSETS) != 1) {
        return 5;
    }
    if (aura_storage_resolve_asset("tests/fixtures/pixel.png", resolved, sizeof(resolved)) != 0) {
        return 6;
    }
    if (strcmp(resolved, assets) == 0 || strstr(resolved, "/tests/fixtures/pixel.png") == NULL) {
        return 7;
    }
    if (aura_storage_resolve_asset("../secret", resolved, sizeof(resolved)) == 0) {
        return 8;
    }
    if (aura_storage_resolve_asset("/tmp/pixel.png", resolved, sizeof(resolved)) != 0 || strcmp(resolved, "/tmp/pixel.png") != 0) {
        return 9;
    }
    memcpy(snapshot_in, "AURA-RS:1:ok", 12);
    if (aura_storage_write_route_snapshot(snapshot_in, 12) != 0) {
        return 10;
    }
    if (aura_storage_write_route_snapshot("nope", 4) == 0) {
        return 11;
    }
    if (aura_storage_read_route_snapshot(snapshot_out, sizeof(snapshot_out), &snapshot_len) != 0 || snapshot_len != 12 || memcmp(snapshot_out, snapshot_in, 12) != 0) {
        return 12;
    }
    if (aura_route_snapshot_save("AURA-RS:1:1:4:home") != 0) {
        return 18;
    }
    aura_route_snapshot_release_memory();
    if (aura_route_snapshot_load_len() != 18 || aura_route_snapshot_load_byte(0) != 'A' || aura_route_snapshot_load_byte(17) != 'e') {
        return 19;
    }
    if (aura_route_snapshot_save("nope") == 0 || aura_route_snapshot_clear() != 0 || aura_route_snapshot_load_len() != 0) {
        return 20;
    }
    if (aura_route_snapshot_save("AURA-RS:1:1:4:home") != 0 || aura_scene_restoration_prepare("AURA-RS", 2) != 0 || aura_route_snapshot_load_len() != 0) {
        return 21;
    }
    if (aura_route_snapshot_save("AURA-RS:1:1:4:home") != 0 || aura_scene_restoration_prepare("AURA-RS", 1) != 1 || aura_route_snapshot_load_len() != 18) {
        return 22;
    }
    if (aura_scene_restoration_accept(NULL, 1) != 0 || aura_scene_restoration_accept("other", 1) != 0) {
        return 23;
    }
    snprintf(cache_file, sizeof(cache_file), "%s/decoded.bin", cache);
    snprintf(kept, sizeof(kept), "%s/note.txt", documents);
    if (write_text(cache_file, "cached") != 0 || write_text(kept, "keep") != 0) {
        return 13;
    }
    if (aura_storage_purge_cache() != 0 || access(cache_file, F_OK) == 0 || access(kept, F_OK) != 0) {
        return 14;
    }
    snprintf(stamp, sizeof(stamp), "%s/.aura-stamp", assets);
    if (aura_storage_stamp_current(stamp) != 0 || write_text(stamp, AURA_VERSION) != 0 || aura_storage_stamp_current(stamp) != 1) {
        return 15;
    }
    if (write_text(stamp, "0.0.0") != 0 || aura_storage_stamp_current(stamp) != 0) {
        return 16;
    }
    spec.cache = documents;
    if (aura_storage_set(&spec) != 0 || aura_storage_purge_cache() == 0) {
        return 17;
    }
    aura_storage_clear();
    printf("mobile storage ok\n");
    return 0;
}
