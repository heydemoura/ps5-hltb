/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "native_protocol.h"
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
int main(void) {
#if defined(HLTB_NATIVE_CLEAR_MARKER)
    if (access(HLTB_NATIVE_TEXT ".undo", F_OK) == 0 ||
        access(HLTB_NATIVE_UPDATE ".undo", F_OK) == 0) {
        puts("Native hooks must be restored before resetting the marker");
        return 1;
    }
    if (unlink(HLTB_NATIVE_LOG) && errno != ENOENT) {
        perror("reset native marker");
        return 1;
    }
    puts("Restored native renderer marker reset");
#elif defined(HLTB_NATIVE_DISABLE)
    if (unlink("/system_tmp/hltb-native.enable")) {
        perror("disable");
        return 1;
    }
    puts("Native annotations disabled");
#else
    FILE *f = fopen("/system_tmp/hltb-native.enable", "w");
    if (!f) {
        perror("enable");
        return 1;
    }
    fclose(f);
    puts("Native annotations enabled");
#endif
    return 0;
}
