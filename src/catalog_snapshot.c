/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "catalog.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
struct header {
    uint32_t magic, count, entry_size, reserved;
};
int hltb_catalog_save(const char *path, const struct hltb_game *games, size_t count) {
    if (count > HLTB_CATALOG_MAX)
        return -1;
    char temp[256];
    if (snprintf(temp, sizeof temp, "%s.tmp", path) >= (int)sizeof temp)
        return -1;
    FILE *f = fopen(temp, "wb");
    if (!f)
        return -1;
    struct header h = {0x484c4341, (uint32_t)count, sizeof *games, 0};
    int ok =
        fwrite(&h, 1, sizeof h, f) == sizeof h && fwrite(games, sizeof *games, count, f) == count;
    if (fclose(f))
        ok = 0;
    return ok ? rename(temp, path) : -1;
}
size_t hltb_catalog_read(const char *path, struct hltb_game *games, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    struct header h;
    if (fread(&h, 1, sizeof h, f) != sizeof h || h.magic != 0x484c4341 ||
        h.entry_size != sizeof *games || h.count > HLTB_CATALOG_MAX || h.count > cap) {
        fclose(f);
        return 0;
    }
    size_t n = fread(games, sizeof *games, h.count, f);
    int extra = fgetc(f);
    fclose(f);
    if (n != h.count || extra != EOF)
        return 0;
    for (size_t i = 0; i < n; i++)
        if (!memchr(games[i].id, 0, sizeof games[i].id) ||
            !memchr(games[i].title, 0, sizeof games[i].title) || strlen(games[i].id) != 9 ||
            !*games[i].title)
            return 0;
    return n;
}
