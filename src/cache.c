/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cache.h"
#include "cJSON.h"
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifndef HLTB_CACHE_PATH
#define HLTB_CACHE_PATH "/user/hltb/cache.json"
#define HLTB_CACHE_DEFAULT_PATH
#endif
/* Persistent storage: enough for the full installed-game catalog. Never evict
 * an unexpired result to cache a newer game. Parsing memory stays bounded. */
static int save_bytes(const char *bytes) {
#ifdef HLTB_CACHE_DEFAULT_PATH
    if (mkdir("/user/hltb", 0755) && errno != EEXIST)
        return -1;
#endif
    FILE *f = fopen(HLTB_CACHE_PATH ".tmp", "wb");
    if (!f)
        return -1;
    size_t n = strlen(bytes);
    int ok = fwrite(bytes, 1, n, f) == n;
    if (fflush(f) || fsync(fileno(f)))
        ok = 0;
    if (fclose(f))
        ok = 0;
    int rc = ok ? rename(HLTB_CACHE_PATH ".tmp", HLTB_CACHE_PATH) : -1;
    if (rc) {
        int failure = errno;
        unlink(HLTB_CACHE_PATH ".tmp");
        errno = failure;
    }
    return rc;
}
static cJSON *load(void) {
    FILE *f = fopen(HLTB_CACHE_PATH, "rb");
#ifdef HLTB_CACHE_DEFAULT_PATH
    int migrate = 0;
    if (!f && errno == ENOENT) {
        f = fopen("/system_tmp/hltb-cache.json", "rb");
        migrate = f != NULL;
    }
#endif
    if (!f)
        return cJSON_CreateArray();
    char *bytes = malloc(HLTB_CACHE_BYTES + 1);
    if (!bytes) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(bytes, 1, HLTB_CACHE_BYTES, f);
    int extra = fgetc(f), failed = ferror(f);
    fclose(f);
    if (extra != EOF || failed) {
        free(bytes);
        return cJSON_CreateArray();
    }
    bytes[n] = 0;
    cJSON *j = cJSON_Parse(bytes);
#ifdef HLTB_CACHE_DEFAULT_PATH
    if (migrate && cJSON_IsArray(j))
        (void)save_bytes(bytes);
#endif
    free(bytes);
    if (!cJSON_IsArray(j)) {
        cJSON_Delete(j);
        return cJSON_CreateArray();
    }
    return j;
}
int hltb_cache_read(const char *title, hltb_result *out) {
    return hltb_cache_read_until(title, out, NULL);
}
int hltb_cache_read_until(const char *title, hltb_result *out, time_t *expires) {
    if (!title || !out)
        return -1;
    time_t now = time(NULL);
    cJSON *j = load();
    if (!j)
        return -1;
    int rc = -1;
    cJSON *row;
    cJSON_ArrayForEach(row, j) {
        cJSON *t = cJSON_GetObjectItemCaseSensitive(row, "title"),
              *at = cJSON_GetObjectItemCaseSensitive(row, "timestamp"),
              *data = cJSON_GetObjectItemCaseSensitive(row, "response"),
              *version = cJSON_GetObjectItemCaseSensitive(row, "version");
        if (cJSON_IsNumber(version) && version->valueint == 2 && cJSON_IsString(t) &&
            !strcmp(t->valuestring, title) && cJSON_IsNumber(at) &&
            at->valuedouble <= (double)now &&
            at->valuedouble > (double)now - HLTB_CACHE_TTL && cJSON_IsObject(data)) {
            char *bytes = cJSON_PrintUnformatted(data);
            if (bytes) {
                rc = hltb_parse_result(bytes, title, out);
                if (!rc && expires)
                    *expires = (time_t)at->valuedouble + HLTB_CACHE_TTL;
                free(bytes);
            }
            break;
        }
    }
    cJSON_Delete(j);
    return rc;
}
int hltb_cache_write(const char *title, const hltb_result *r) {
    if (!title || strlen(title) > 500 || !r || !r->main_seconds)
        return -1;
    cJSON *j = load();
    if (!j)
        return -1;
    for (int i = cJSON_GetArraySize(j) - 1; i >= 0; i--) {
        cJSON *t = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(j, i), "title");
        if (cJSON_IsString(t) && !strcmp(t->valuestring, title))
            cJSON_DeleteItemFromArray(j, i);
    }
    cJSON *row = cJSON_CreateObject();
    cJSON_AddNumberToObject(row, "version", 2);
    cJSON_AddStringToObject(row, "title", title);
    cJSON_AddNumberToObject(row, "timestamp", (double)time(NULL));
    cJSON *response = cJSON_AddObjectToObject(row, "response"),
          *data = cJSON_AddArrayToObject(response, "data"), *game = cJSON_CreateObject();
    cJSON_AddItemToArray(data, game);
    cJSON_AddNumberToObject(game, "game_id", r->game_id);
    cJSON_AddStringToObject(game, "game_name", r->game_name);
    cJSON_AddNumberToObject(game, "comp_main", r->main_seconds);
    cJSON_AddNumberToObject(game, "comp_plus", r->extra_seconds);
    cJSON_AddNumberToObject(game, "comp_100", r->completionist_seconds);
    cJSON_AddItemToArray(j, row);
    /* Remove expired records only; keep every valid record for its full TTL. */
    time_t now = time(NULL);
    for (int i = cJSON_GetArraySize(j) - 1; i >= 0; i--) {
        cJSON *at = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(j, i), "timestamp");
        if (!cJSON_IsNumber(at) || at->valuedouble > (double)now ||
            at->valuedouble <= (double)now - HLTB_CACHE_TTL)
            cJSON_DeleteItemFromArray(j, i);
    }
    char *bytes = cJSON_PrintUnformatted(j);
    int count = cJSON_GetArraySize(j);
    cJSON_Delete(j);
    if (!bytes || strlen(bytes) > HLTB_CACHE_BYTES || count > HLTB_CACHE_ENTRIES) {
        free(bytes);
        return -1;
    }
    int rc = save_bytes(bytes);
    free(bytes);
    return rc;
}
