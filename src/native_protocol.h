/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#ifdef HLTB_RENDER_PROBE
#define HLTB_NATIVE_LOG "/system_tmp/hltb-render.log"
#define HLTB_NATIVE_TEXT "/system_tmp/hltb-render.req"
#define HLTB_NATIVE_UPDATE "/system_tmp/hltb-render-unused.req"
#else
#define HLTB_NATIVE_LOG "/system_tmp/hltb-native.log"
#define HLTB_NATIVE_TEXT "/system_tmp/hltb-native-text.req"
#define HLTB_NATIVE_UPDATE "/system_tmp/hltb-native-update.req"
#endif
#define HLTB_NATIVE_MAGIC UINT64_C(0x484c54424e415431)
struct hltb_native_request {
    uint64_t magic;
    int32_t pid;
    uint32_t kind;
    uint64_t method, hook, trampoline;
};
