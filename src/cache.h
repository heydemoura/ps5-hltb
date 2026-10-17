/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "hltb.h"
#include <time.h>
#define HLTB_CACHE_TTL (30 * 86400)
#define HLTB_CACHE_BYTES (1024 * 1024)
#define HLTB_CACHE_ENTRIES 512
int hltb_cache_read_until(const char *title, hltb_result *result, time_t *expires);
int hltb_cache_read(const char *title, hltb_result *result);
int hltb_cache_write(const char *title, const hltb_result *result);
