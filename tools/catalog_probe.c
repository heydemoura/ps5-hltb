/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "catalog.h"
#include <stdio.h>
static struct hltb_game games[HLTB_CATALOG_MAX];
int main(void) {
    size_t n = hltb_catalog_load(games, HLTB_CATALOG_MAX);
    for (size_t i = 0; i < n; i++)
        printf("%s\t%s\n", games[i].id, games[i].title);
    printf("%zu game titles\n", n);
    return 0;
}
