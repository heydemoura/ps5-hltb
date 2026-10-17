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
#include <time.h>
#include <unistd.h>
int main(void) {
    char temp[] = "/tmp/hltb-storage-XXXXXX";
    assert(mkdtemp(temp));
    assert(!chdir(temp));
    hltb_result r = {.game_id = 169267,
                     .game_name = "Resident Evil Requiem",
                     .main_seconds = 39800,
                     .extra_seconds = 45000,
                     .completionist_seconds = 72000},
                out = {0};
    assert(hltb_cache_read("RESIDENT EVIL requiem", &out) == -1);
    assert(!hltb_cache_write("RESIDENT EVIL requiem", &r));
    assert(!hltb_cache_read("RESIDENT EVIL requiem", &out));
    assert(out.main_seconds == 39800 && out.game_id == 169267);
    assert(out.extra_seconds == 45000 && out.completionist_seconds == 72000);
    /* Keep disk values beyond the old seven-day TTL; expire exactly at 30 days. */
    for (int age = 0; age < 4; age++) {
        FILE *cf = fopen("cache.json", "r");
        assert(cf);
        char input[4096];
        size_t count = fread(input, 1, sizeof input - 1, cf);
        fclose(cf);
        input[count] = 0;
        cJSON *rows = cJSON_Parse(input);
        assert(rows);
        time_t now = time(NULL);
        int ages[] = {8 * 86400, HLTB_CACHE_TTL - 60, HLTB_CACHE_TTL, -60};
        cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(rows, 0),
                                                           "timestamp"), now - ages[age]);
        char *output = cJSON_PrintUnformatted(rows);
        cJSON_Delete(rows);
        cf = fopen("cache.json", "w");
        assert(cf && output);
        fputs(output, cf);
        fclose(cf);
        free(output);
        time_t expires = 0;
        int rc = hltb_cache_read_until("RESIDENT EVIL requiem", &out, &expires);
        assert(age < 2 ? rc == 0 : rc != 0);
        if (age < 2)
            assert(expires == now - ages[age] + HLTB_CACHE_TTL);
    }
    assert(!hltb_cache_write("RESIDENT EVIL requiem", &r));
    FILE *legacy = fopen("cache.json", "r");
    assert(legacy);
    char old_cache[8193];
    size_t old_n = fread(old_cache, 1, sizeof old_cache - 1, legacy);
    fclose(legacy);
    old_cache[old_n] = 0;
    cJSON *old_rows = cJSON_Parse(old_cache);
    assert(old_rows);
    cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetArrayItem(old_rows, 0), "version");
    char *legacy_bytes = cJSON_PrintUnformatted(old_rows);
    cJSON_Delete(old_rows);
    assert(legacy_bytes);
    legacy = fopen("cache.json", "w");
    assert(legacy);
    fputs(legacy_bytes, legacy);
    fclose(legacy);
    free(legacy_bytes);
    assert(hltb_cache_read("RESIDENT EVIL requiem", &out) == -1);
    assert(!hltb_cache_write("RESIDENT EVIL requiem", &r));
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
    assert(!hltb_cache_write("RESIDENT EVIL requiem", &r));
    // More than 16 valid titles must survive without eviction, within a bounded file.
    for (int i = 0; i < 40; i++) {
        char title[480];
        memset(title, 'A', 440);
        snprintf(title + 440, sizeof title - 440, " %d", i);
        strcpy(r.game_name, title);
        assert(!hltb_cache_write(title, &r));
        assert(!hltb_cache_read(title, &out));
        struct stat st;
        assert(!stat(path, &st));
        assert(st.st_size <= HLTB_CACHE_BYTES);
    }
    f = fopen(path, "r");
    assert(f);
    char bytes[HLTB_CACHE_BYTES + 1];
    size_t n = fread(bytes, 1, sizeof bytes - 1, f);
    fclose(f);
    bytes[n] = 0;
    cJSON *cache = cJSON_Parse(bytes);
    assert(cJSON_IsArray(cache));
    assert(cJSON_GetArraySize(cache) == 41);
    assert(!hltb_cache_read("RESIDENT EVIL requiem", &out));
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
    /* Every catalog entry fits; a 513th record must preserve the existing file. */
    unlink(path);
    for (unsigned i = 0; i < HLTB_CACHE_ENTRIES; i++) {
        char name[32];
        snprintf(name, sizeof name, "Game %u", i);
        strcpy(r.game_name, name);
        assert(!hltb_cache_write(name, &r));
    }
    for (unsigned i = 0; i < HLTB_CACHE_ENTRIES; i++) {
        char name[32];
        snprintf(name, sizeof name, "Game %u", i);
        assert(!hltb_cache_read(name, &out));
    }
    strcpy(r.game_name, "Overflow");
    assert(hltb_cache_write("Overflow", &r) == -1);
    assert(!hltb_cache_read("Game 0", &out));
    assert(!hltb_cache_read("Game 511", &out));
    unlink(path);
    unlink("catalog.bin");
    assert(!chdir("/"));
    rmdir(temp);
    puts("Cache identity, cached values, catalog round-trip and malformed storage checks passed");
}
