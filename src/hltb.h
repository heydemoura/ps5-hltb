/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
typedef struct {
    int game_id;
    char game_name[512];
    unsigned main_seconds;
    unsigned extra_seconds, completionist_seconds;
} hltb_result;
/* A strict title match avoids silently displaying time for a different game. */
int hltb_parse_result(const char *json, const char *title, hltb_result *result);
char *hltb_search_body(const char *title);
int hltb_format_time(unsigned seconds, char *out, size_t size);
int hltb_format_metrics(const hltb_result *result, char *out, size_t size);
/* Native PS5 HTTPS transport; no PC companion service. */
int hltb_lookup(const char *title, hltb_result *result);
