/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "hltb.h"
int hltb_cache_read(const char *title, hltb_result *result);
int hltb_cache_write(const char *title, const hltb_result *result);
