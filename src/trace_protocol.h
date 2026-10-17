/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#define HLTB_TRACE_REQUEST "/system_tmp/hltb-trace.req"
#define HLTB_TRACE_LOG "/system_tmp/hltb-trace.log"
struct hltb_trace_request {
    uint64_t magic;
    int32_t pid;
    uint32_t reserved;
    uint64_t method, hook, trampoline;
};
#define HLTB_TRACE_MAGIC UINT64_C(0x484c544254524331)
