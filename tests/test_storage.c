/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cJSON.h"
#include "cache.h"
#include "catalog.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void) {
    char temp[] = "/tmp/hltb-storage-XXXXXX";
    assert(mkdtemp(temp));
    assert(!chdir(temp));
    hltb_result r = {.game_id = 169267,
                     .game_name = "Resident Evil Requiem",
                     .main_seconds = 39800},
                out = {0};
    assert(hltb_cache_read("RESIDENT EVIL requiem", &out) == -1);
    assert(!hltb_cache_write("RESIDENT EVIL requiem", &r));
    assert(!hltb_cache_read("RESIDENT EVIL requiem", &out));
    assert(out.main_seconds == 39800 && out.game_id == 169267);
    assert(hltb_cache_read("Resident Evil 4", &out) == -1);
    const char *path = "cache.json";
    FILE *f = fopen(path, "w");
    assert(f);
    fputs("{\"title\":\"Different Game\",\"timestamp\":0,\"response\":{}}", f);
    fclose(f);
    assert(hltb_cache_read("RESIDENT EVIL requiem", &out) == -1);
    struct hltb_game games[2] = {{"PPSA31246", "RESIDENT EVIL requiem"},
                                 {"CUSA01113", "Gravity Rush™"}},
                     read[2];
    assert(!hltb_catalog_save("catalog.bin", games, 2));
    assert(hltb_catalog_read("catalog.bin", read, 2) == 2);
    assert(!strcmp(read[1].title, "Gravity Rush™"));
    assert(hltb_catalog_read("catalog.bin", read, 1) == 0);
    f = fopen("catalog.bin", "a");
    assert(f);
    fputs("trailing garbage", f);
    fclose(f);
    assert(hltb_catalog_read("catalog.bin", read, 2) == 0);
    // Long titles and repeated lookups must not exhaust the console's tiny RAM disk.
    for (int i = 0; i < 40; i++) {
        char title[480];
        memset(title, 'A', 440);
        snprintf(title + 440, sizeof title - 440, " %d", i);
        strcpy(r.game_name, title);
        assert(!hltb_cache_write(title, &r));
        assert(!hltb_cache_read(title, &out));
        struct stat st;
        assert(!stat(path, &st));
        assert(st.st_size <= 8192);
    }
    f = fopen(path, "r");
    assert(f);
    char bytes[8193];
    size_t n = fread(bytes, 1, sizeof bytes - 1, f);
    fclose(f);
    bytes[n] = 0;
    cJSON *cache = cJSON_Parse(bytes);
    assert(cJSON_IsArray(cache));
    assert(cJSON_GetArraySize(cache) <= 16);
    cJSON *latest = cJSON_GetArrayItem(cache, cJSON_GetArraySize(cache) - 1);
    cJSON *title = cJSON_GetObjectItemCaseSensitive(latest, "title");
    assert(cJSON_IsString(title));
    char expired[512];
    strcpy(expired, title->valuestring);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(latest, "timestamp"), 0);
    char *old = cJSON_PrintUnformatted(cache);
    assert(old);
    cJSON_Delete(cache);
    f = fopen(path, "w");
    assert(f);
    fputs(old, f);
    fclose(f);
    free(old);
    assert(hltb_cache_read(expired, &out) == -1);
    unlink(path);
    unlink("catalog.bin");
    assert(!chdir("/"));
    rmdir(temp);
    puts("Cache identity, cached values, catalog round-trip and malformed storage checks passed");
}
