/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cache.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef HLTB_CACHE_PATH
#define HLTB_CACHE_PATH "/system_tmp/hltb-cache.json"
#endif
/* /system_tmp is only 256 KiB on the test console. Keep the complete cache
 * in one bounded file, not a directory of filesystem-block-sized records. */
#define CACHE_BYTES 8192
#define CACHE_ENTRIES 16
static cJSON *load(void) {
    char bytes[CACHE_BYTES + 1];
    FILE *f = fopen(HLTB_CACHE_PATH, "rb");
    if (!f)
        return cJSON_CreateArray();
    size_t n = fread(bytes, 1, CACHE_BYTES, f);
    int extra = fgetc(f);
    fclose(f);
    if (extra != EOF)
        return cJSON_CreateArray();
    bytes[n] = 0;
    cJSON *j = cJSON_Parse(bytes);
    if (!cJSON_IsArray(j)) {
        cJSON_Delete(j);
        return cJSON_CreateArray();
    }
    return j;
}
int hltb_cache_read(const char *title, hltb_result *out) {
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
            at->valuedouble <= (double)time(NULL) &&
            at->valuedouble > (double)time(NULL) - 7 * 86400 && cJSON_IsObject(data)) {
            char *bytes = cJSON_PrintUnformatted(data);
            if (bytes) {
                rc = hltb_parse_result(bytes, title, out);
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
    while (cJSON_GetArraySize(j) > CACHE_ENTRIES)
        cJSON_DeleteItemFromArray(j, 0);
    char *bytes = cJSON_PrintUnformatted(j);
    while (bytes && strlen(bytes) > CACHE_BYTES && cJSON_GetArraySize(j) > 1) {
        free(bytes);
        cJSON_DeleteItemFromArray(j, 0);
        bytes = cJSON_PrintUnformatted(j);
    }
    cJSON_Delete(j);
    if (!bytes || strlen(bytes) > CACHE_BYTES) {
        free(bytes);
        return -1;
    }
    FILE *f = fopen(HLTB_CACHE_PATH ".tmp", "wb");
    int rc = -1;
    if (f) {
        size_t n = strlen(bytes);
        int ok = fwrite(bytes, 1, n, f) == n;
        if (fclose(f))
            ok = 0;
        if (ok)
            rc = rename(HLTB_CACHE_PATH ".tmp", HLTB_CACHE_PATH);
    }
    free(bytes);
    return rc;
}
