/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stddef.h>
#define HLTB_CATALOG_MAX 512
struct hltb_game {
    char id[16];
    char title[512];
};
size_t hltb_catalog_load(struct hltb_game *games, size_t capacity);
int hltb_catalog_save(const char *path, const struct hltb_game *games, size_t count);
size_t hltb_catalog_read(const char *path, struct hltb_game *games, size_t capacity);
