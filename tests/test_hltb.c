/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cJSON.h"
#include "hltb.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    const char *json = "{\"data\":[{\"game_id\":1,\"game_name\":\"Elden Ring: "
                       "Nightreign\",\"comp_main\":100000},{\"game_id\":2,\"game_name\":\"ELDEN "
                       "RING™\",\"comp_main\":216464}]}";
    hltb_result r = {0};
    assert(hltb_parse_result(json, "Elden Ring", &r) == 0);
    assert(r.game_id == 2);
    assert(r.main_seconds == 216464);
    assert(hltb_parse_result(json, "Elden Ring II", &r) == 1);
    assert(hltb_parse_result("{}", "Elden Ring", &r) == -1);
    assert(hltb_parse_result("{\"data\":[{\"game_id\":1,\"game_name\":\"X\",\"comp_main\":0}]}",
                             "X", &r) == 2);
    assert(hltb_parse_result("{\"data\":[{\"game_id\":1,\"game_name\":\"X\",\"comp_main\":1e300}]}",
                             "X", &r) == 2);
    char label[100];
    assert(!hltb_format_time(216464, label, sizeof label));
    assert(!strcmp(label, "Main Story: 60h 08m"));
    assert(!hltb_format_time(1800, label, sizeof label));
    assert(!strcmp(label, "Main Story: 30m"));
    assert(hltb_format_time(0, label, sizeof label) == -1);
    assert(hltb_format_time(10000, label, 5) == -1);
    assert(!hltb_parse_result("{\"data\":[{\"game_id\":1,\"game_name\":\"X\",\"comp_main\":3600,"
                              "\"comp_plus\":7200,\"comp_100\":10800}]}",
                              "X", &r));
    assert(r.extra_seconds == 7200 && r.completionist_seconds == 10800);
    char metrics[320];
    assert(!hltb_format_metrics(&r, metrics, sizeof metrics));
    assert(!strcmp(metrics, "Main Story: 1h 00m\nMain + Extra: 2h 00m\nCompletionist: 3h 00m"));
    assert(hltb_format_metrics(&r, metrics, 8) == -1);
    assert(!hltb_parse_result("{\"data\":[{\"game_id\":1,\"game_name\":\"X\",\"comp_main\":3600,"
                              "\"comp_plus\":-1,\"comp_100\":1e300}]}",
                              "X", &r));
    assert(!r.extra_seconds && !r.completionist_seconds);
    assert(!hltb_format_metrics(&r, metrics, sizeof metrics));
    assert(strstr(metrics, "Main + Extra: N/A\nCompletionist: N/A"));
    char *body = hltb_search_body("A \"Quoted\" Game");
    assert(body);
    cJSON *root = cJSON_Parse(body);
    assert(root);
    cJSON *terms = cJSON_GetObjectItemCaseSensitive(root, "searchTerms");
    assert(cJSON_GetArraySize(terms) == 3);
    assert(!strcmp(cJSON_GetArrayItem(terms, 1)->valuestring, "\"Quoted\""));
    cJSON_Delete(root);
    free(body);
    body = hltb_search_body("The Last Guardian™");
    assert(body);
    assert(!strstr(body, "™"));
    free(body);
    puts("HLTB parsing, title matching, request escaping and time formatting passed");
}
