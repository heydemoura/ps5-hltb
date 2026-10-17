/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "hltb.h"
#include "cJSON.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Ignore ASCII punctuation, trademark signs and case, preserving non-ASCII
 * title bytes. Do not strip edition names or numerals: those identify games. */
static void normalize(const char *s, char *out, size_t cap) {
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n + 1 < cap) {
        if (p[0] == 0xc2 && (p[1] == 0xae || p[1] == 0xa9)) {
            p += 2;
            continue;
        }
        if (p[0] == 0xe2 && p[1] == 0x84 && p[2] == 0xa2) {
            p += 3;
            continue;
        }
        if (*p >= 128 || isalnum(*p))
            out[n++] = (char)(*p < 128 ? tolower(*p) : *p);
        p++;
    }
    out[n] = 0;
}
static unsigned optional_seconds(cJSON *item, const char *field) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(item, field);
    return cJSON_IsNumber(value) && isfinite(value->valuedouble) && value->valuedouble > 0 &&
                   value->valuedouble <= 315360000
               ? (unsigned)value->valuedouble
               : 0;
}
int hltb_parse_result(const char *json, const char *title, hltb_result *out) {
    if (!json || !title || !out || !*title || strlen(title) > 500)
        return -1;
    cJSON *root = cJSON_Parse(json);
    if (!root)
        return -1;
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return -1;
    }
    char wanted[1024];
    normalize(title, wanted, sizeof wanted);
    int status = 1;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, data) {
        cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "game_name");
        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "game_id");
        cJSON *secs = cJSON_GetObjectItemCaseSensitive(item, "comp_main");
        if (!cJSON_IsString(name) || strlen(name->valuestring) > 500 || !cJSON_IsNumber(id) ||
            !cJSON_IsNumber(secs))
            continue;
        char norm[1024];
        normalize(name->valuestring, norm, sizeof norm);
        if (strcmp(wanted, norm))
            continue;
        if (!isfinite(secs->valuedouble) || secs->valuedouble <= 0 ||
            secs->valuedouble > 315360000 || id->valuedouble <= 0 || id->valuedouble > 2147483647) {
            status = 2;
            continue;
        }
        if (strlen(name->valuestring) >= sizeof out->game_name) {
            status = -1;
            break;
        }
        out->game_id = id->valueint;
        strcpy(out->game_name, name->valuestring);
        out->main_seconds = (unsigned)secs->valuedouble;
        out->extra_seconds = optional_seconds(item, "comp_plus");
        out->completionist_seconds = optional_seconds(item, "comp_100");
        status = 0;
        break;
    }
    cJSON_Delete(root);
    return status;
}
char *hltb_search_body(const char *title) {
    if (!title || !*title || strlen(title) > 500)
        return NULL;
    const char *template =
        "{\"searchType\":\"games\",\"searchPage\":1,\"size\":20,\"searchOptions\":{\"games\":{"
        "\"userId\":0,\"platform\":\"\",\"sortCategory\":\"popular\",\"rangeCategory\":\"main\","
        "\"rangeTime\":{\"min\":0,\"max\":0},\"gameplay\":{\"perspective\":\"\",\"flow\":\"\","
        "\"genre\":\"\",\"difficulty\":\"\"},\"rangeYear\":{\"min\":\"\",\"max\":\"\"},"
        "\"modifier\":\"hide_dlc\"},\"users\":{\"sortCategory\":\"postcount\"},\"lists\":{"
        "\"sortCategory\":\"follows\"},\"filter\":\"\",\"sort\":0,\"randomizer\":0},\"useCache\":"
        "true}";
    cJSON *root = cJSON_Parse(template);
    if (!root)
        return NULL;
    cJSON *terms = cJSON_AddArrayToObject(root, "searchTerms");
    char copy[512];
    size_t copied = 0;
    const unsigned char *p = (const unsigned char *)title;
    while (*p) {
        if (p[0] == 0xc2 && (p[1] == 0xae || p[1] == 0xa9)) {
            p += 2;
            continue;
        }
        if (p[0] == 0xe2 && p[1] == 0x84 && p[2] == 0xa2) {
            p += 3;
            continue;
        }
        copy[copied++] = (char)*p++;
    }
    copy[copied] = 0;
    char *state = NULL;
    for (char *t = strtok_r(copy, " \t\r\n", &state); t; t = strtok_r(NULL, " \t\r\n", &state))
        cJSON_AddItemToArray(terms, cJSON_CreateString(t));
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return body;
}
int hltb_format_time(unsigned seconds, char *out, size_t size) {
    if (!seconds)
        return -1;
    unsigned mins = (seconds + 30) / 60;
    int n;
    if (mins >= 60)
        n = snprintf(out, size, "Main Story: %uh %02um", mins / 60, mins % 60);
    else
        n = snprintf(out, size, "Main Story: %um", mins);
    return n < 0 || (size_t)n >= size ? -1 : 0;
}

int hltb_format_metrics(const hltb_result *r, char *out, size_t size) {
    if (!r || !out || !size)
        return -1;
    const char *names[] = {"Main Story", "Main + Extra", "Completionist"};
    unsigned values[] = {r->main_seconds, r->extra_seconds, r->completionist_seconds};
    size_t used = 0;
    for (unsigned i = 0; i < 3; i++) {
        char duration[64];
        if (!values[i])
            snprintf(duration, sizeof duration, "N/A");
        else {
            unsigned minutes = (values[i] + 30) / 60;
            if (minutes >= 60)
                snprintf(duration, sizeof duration, "%uh %02um", minutes / 60, minutes % 60);
            else
                snprintf(duration, sizeof duration, "%um", minutes);
        }
        int n = snprintf(out + used, size - used, "%s%s: %s", i ? "\n" : "", names[i], duration);
        if (n < 0 || (size_t)n >= size - used)
            return -1;
        used += (size_t)n;
    }
    return 0;
}
