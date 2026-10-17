/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cache.h"
#include "hltb.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    char title[512] = {0};
    if (argc > 1) {
        size_t used = 0;
        for (int i = 1; i < argc; i++) {
            size_t n = strlen(argv[i]);
            if (used + n + (i > 1 ? 1 : 0) > 500) {
                puts("Title too long");
                return 1;
            }
            if (i > 1)
                title[used++] = ' ';
            memcpy(title + used, argv[i], n + 1);
            used += n;
        }
    } else {
        if (fgets(title, sizeof title, stdin))
            title[strcspn(title, "\r\n")] = 0;
        if (!*title)
            strcpy(title, "Elden Ring");
    }
    printf("Querying HowLongToBeat by title: %s\n", title);
    fflush(stdout);
    hltb_result r = {0};
    int status = hltb_lookup(title, &r);
    if (status) {
        printf("Lookup failed: %d (1=no title match, 2=no Main Story data)\n", status);
        return 1;
    }
    char label[320];
    if (hltb_format_metrics(&r, label, sizeof label))
        return 1;
    (void)hltb_cache_write(title, &r);
    printf("HLTB game=%d title=%s seconds=%u\n%s\n", r.game_id, r.game_name, r.main_seconds, label);
    return 0;
}
