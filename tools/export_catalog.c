/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "catalog.h"
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
int hltb_export_catalog(void) {
    static struct hltb_game games[HLTB_CATALOG_MAX];
    size_t n = hltb_catalog_load(games, HLTB_CATALOG_MAX);
    printf("Exporting %zu console game titles for ShellUI\n", n);
    int rc = hltb_catalog_save("/system_tmp/hltb-catalog.bin", games, n);
    if (rc)
        perror("save HLTB catalog");
    return rc;
}
