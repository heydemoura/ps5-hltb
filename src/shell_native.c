/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cache.h"
#include "catalog.h"
#include "hltb.h"
#include "native_protocol.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef void Domain;
typedef void Assembly;
typedef void Image;
typedef void Class;
typedef void Method;
typedef void Field;
extern Domain *mono_get_root_domain(void);
extern Domain *mono_domain_get(void);
extern void *mono_thread_attach(Domain *);
extern Assembly *mono_domain_assembly_open(Domain *, const char *);
extern Image *mono_assembly_get_image(Assembly *);
extern Class *mono_class_from_name(Image *, const char *, const char *);
extern Method *mono_class_get_method_from_name(Class *, const char *, int);
extern void *mono_compile_method(Method *);
extern Field *mono_class_get_field_from_name(Class *, const char *);
extern int mono_field_get_offset(Field *);
extern char *mono_string_to_utf8(void *);
extern void *mono_string_new(Domain *, const char *);
extern void mono_free(void *);
extern uint32_t mono_gchandle_new(void *, int);
extern void *mono_gchandle_get_target(uint32_t);
extern void mono_gchandle_free(uint32_t);
__asm__(
    ".pushsection .text\n.p2align 4\n.global "
    "hltb_native_text_original\nhltb_native_text_original:\n.fill 64,1,0xcc\n.p2align 4\n.global "
    "hltb_native_update_original\nhltb_native_update_original:\n.fill 64,1,0xcc\n.popsection\n");
extern void hltb_native_text_original(void *, void *);
extern void hltb_native_update_original(void *);
static Domain *domain;
static struct hltb_game games[HLTB_CATALOG_MAX];
static size_t game_count;
struct cache {
    atomic_int state;
    char label[640];
}; /* 0 idle,1 queued,2 ready,3 unavailable */
static struct cache cache[HLTB_CATALOG_MAX];
struct event {
    uint32_t handle;
    int index;
    uintptr_t thread;
};
static struct event ring[64];
static atomic_uint write_index, read_index;
static atomic_flag producer_guard = ATOMIC_FLAG_INIT;
static atomic_int enabled;
struct tracked {
    uint32_t handle;
    int index;
    int changed;
    uintptr_t owner_thread;
};
static struct tracked tracked[128];
static size_t next_slot;
static int text_off, parent_off, root_off, tag_off, view_off, disposed_off;
static FILE *logfile;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static void log_event(const char *format, ...) {
    pthread_mutex_lock(&log_mutex);
    if (logfile) {
        if (ftell(logfile) > 6144) {
            fflush(logfile);
            if (!ftruncate(fileno(logfile), 0))
                fseek(logfile, 0, SEEK_SET);
        }
        va_list args;
        va_start(args, format);
        vfprintf(logfile, format, args);
        va_end(args);
        fflush(logfile);
    }
    pthread_mutex_unlock(&log_mutex);
}
static uintptr_t setter_thread, update_thread;
static atomic_int frames;
static int find_game(const char *text) {
    for (size_t i = 0; i < game_count; i++)
        if (!strcmp(text, games[i].title))
            return (int)i;
    return -1;
}
static void refresh_titles(void *current_node) {
    void *current_root = *(void **)((char *)current_node + root_off);
    if (!current_root)
        return;
    if (!atomic_load_explicit(&enabled, memory_order_acquire))
        return;
    for (size_t i = 0; i < 128; i++) {
        struct tracked *t = &tracked[i];
        if (!t->handle || t->changed || t->owner_thread != (uintptr_t)pthread_self() ||
            atomic_load_explicit(&cache[t->index].state, memory_order_acquire) != 2)
            continue;
        void *node = mono_gchandle_get_target(t->handle);
        if (!node || *(unsigned char *)((char *)node + disposed_off)) {
            mono_gchandle_free(t->handle);
            t->handle = 0;
            continue;
        }
        if (!*(void **)((char *)node + parent_off) ||
            *(void **)((char *)node + root_off) != current_root)
            continue;
        void *old = *(void **)((char *)node + text_off);
        char *s = old ? mono_string_to_utf8(old) : NULL;
        int same = s && !strcmp(s, games[t->index].title);
        if (s)
            mono_free(s);
        if (same) {
            void *str = mono_string_new(mono_domain_get(), cache[t->index].label);
            hltb_native_text_original(node, str);
            t->changed = 1;
            if (logfile) {
                log_event("rendered thread=%lx %s\n", (unsigned long)(uintptr_t)pthread_self(),
                          cache[t->index].label);
            }
        }
    }
}
static void text_hook(void *node, void *text) {
    if (atomic_flag_test_and_set_explicit(&producer_guard, memory_order_acquire)) {
        hltb_native_text_original(node, text);
        return;
    }
    char *s = text ? mono_string_to_utf8(text) : NULL;
    int index = s ? find_game(s) : -1;
    if (s)
        mono_free(s);
    int ready = index >= 0 && atomic_load_explicit(&enabled, memory_order_acquire) &&
                atomic_load_explicit(&cache[index].state, memory_order_acquire) == 2;
    void *actual = ready ? mono_string_new(mono_domain_get(), cache[index].label) : text;
    hltb_native_text_original(node, actual);
    if (index >= 0) {
        int expect = 0;
        atomic_compare_exchange_strong(&cache[index].state, &expect, 1);
        size_t slot;
        for (slot = 0; slot < 128; slot++)
            if (tracked[slot].handle && mono_gchandle_get_target(tracked[slot].handle) == node)
                break;
        if (slot == 128)
            slot = next_slot++ % 128;
        if (tracked[slot].handle)
            mono_gchandle_free(tracked[slot].handle);
        tracked[slot] =
            (struct tracked){mono_gchandle_new(node, 1), index, ready, (uintptr_t)pthread_self()};
        unsigned w = atomic_load_explicit(&write_index, memory_order_relaxed),
                 r = atomic_load_explicit(&read_index, memory_order_acquire);
        if (w - r < 64) {
            uint32_t h = mono_gchandle_new(node, 1);
            if (h) {
                ring[w % 64] = (struct event){h, index, (uintptr_t)pthread_self()};
                atomic_store_explicit(&write_index, w + 1, memory_order_release);
            }
        }
        if (ready && logfile) {
            log_event("rendered-inline thread=%lx %s\n", (unsigned long)(uintptr_t)pthread_self(),
                      cache[index].label);
        }
    }
    refresh_titles(node);
    atomic_flag_clear_explicit(&producer_guard, memory_order_release);
}
static void log_node(void *node, int index, uintptr_t tid) {
    log_event("title=%s node=%p root=%p setter-thread=%lx update-thread=%lx\n", games[index].title,
              node, *(void **)((char *)node + root_off), (unsigned long)tid,
              (unsigned long)(uintptr_t)pthread_self());
}
static void update_hook(void *app) {
    update_thread = (uintptr_t)pthread_self();
    atomic_fetch_add(&frames, 1);
    unsigned r = atomic_load_explicit(&read_index, memory_order_relaxed),
             w = atomic_load_explicit(&write_index, memory_order_acquire);
    while (r < w) {
        struct event e = ring[r % 64];
        void *node = mono_gchandle_get_target(e.handle);
        setter_thread = e.thread;
        if (node)
            log_node(node, e.index, e.thread);
        mono_gchandle_free(e.handle);
        r++;
        atomic_store_explicit(&read_index, r, memory_order_release);
    }
    hltb_native_update_original(app);
}
static int field_offset(Class *c, const char *name) {
    Field *f = mono_class_get_field_from_name(c, name);
    return f ? mono_field_get_offset(f) : -1;
}
static void request(const char *path, int kind, void *method, void *hook, void *trampoline) {
    struct hltb_native_request q = {HLTB_NATIVE_MAGIC, getpid(),        kind,
                                    (uintptr_t)method, (uintptr_t)hook, (uintptr_t)trampoline};
    char temp[128];
    snprintf(temp, sizeof temp, "%s.tmp", path);
    FILE *f = fopen(temp, "wb");
    if (f) {
        fwrite(&q, 1, sizeof q, f);
        fclose(f);
        rename(temp, path);
    }
}
__attribute__((visibility("default"), noreturn)) void *elf_main(void *args) {
    (void)args;
    logfile = fopen(HLTB_NATIVE_LOG, "w");
    if (logfile) {
        log_event("entered pid=%d\n", getpid());
    }
    domain = mono_get_root_domain();
    if (!domain)
        goto parked;
    mono_thread_attach(domain);
    game_count = hltb_catalog_read("/system_tmp/hltb-catalog.bin", games, HLTB_CATALOG_MAX);
    if (!game_count)
        game_count = hltb_catalog_load(games, HLTB_CATALOG_MAX);
    for (size_t n = 0; n < game_count; n++) {
        hltb_result r;
        char time[100];
        if (!hltb_cache_read(games[n].title, &r) &&
            !hltb_format_time(r.main_seconds, time, sizeof time)) {
            snprintf(cache[n].label, sizeof cache[n].label, "%s  |  %s", games[n].title, time);
            atomic_store(&cache[n].state, 2);
        }
    }
    Assembly *a = mono_domain_assembly_open(domain, "/system_ex/common_ex/lib/ReactNative.PUI.dll");
    Image *i = a ? mono_assembly_get_image(a) : NULL;
    Class *raw =
        i ? mono_class_from_name(i, "ReactNative.Views.UI3.View", "ReactRawTextShadowNode") : NULL;
    Class *base = i ? mono_class_from_name(i, "ReactNative.UIManager", "ReactShadowNode") : NULL;
    if (!raw || !base)
        goto parked;
    text_off = field_offset(raw, "_text");
    parent_off = field_offset(base, "_parent");
    root_off = field_offset(base, "_rootNode");
    tag_off = field_offset(base, "<ReactTag>k__BackingField");
    view_off = field_offset(base, "<ViewClass>k__BackingField");
    disposed_off = field_offset(base, "_disposed");
    if (text_off < 16 || parent_off < 16 || root_off < 16 || tag_off < 16 || view_off < 16 ||
        disposed_off < 16)
        goto parked;
    Method *m = mono_class_get_method_from_name(raw, "SetText", 1);
    void *text = m ? mono_compile_method(m) : NULL;
    a = mono_domain_assembly_open(domain, "/system_ex/common_ex/lib/Sce.PlayStation.PUI.dll");
    i = a ? mono_assembly_get_image(a) : NULL;
    Class *application = i ? mono_class_from_name(i, "Sce.PlayStation.PUI", "Application") : NULL;
    m = application ? mono_class_get_method_from_name(application, "Update", 0) : NULL;
    void *update = m ? mono_compile_method(m) : NULL;
    if (!text || !update)
        goto parked;
    if (logfile) {
        log_event("catalog=%zu SetText=%p Update=%p offsets=%d,%d,%d,%d,%d,%d\n", game_count, text,
                  update, text_off, parent_off, root_off, tag_off, view_off, disposed_off);
    }
    request(HLTB_NATIVE_TEXT, 1, text, text_hook, hltb_native_text_original);
    request(HLTB_NATIVE_UPDATE, 2, update, update_hook, hltb_native_update_original);
    for (;;) {
        for (size_t n = 0; n < game_count; n++)
            if (atomic_load_explicit(&cache[n].state, memory_order_acquire) == 1) {
                hltb_result result = {0};
                int rc = hltb_lookup(games[n].title, &result);
                char time[100];
                if (!rc && !hltb_format_time(result.main_seconds, time, sizeof time)) {
                    (void)hltb_cache_write(games[n].title, &result);
                    snprintf(cache[n].label, sizeof cache[n].label, "%s  |  %s", games[n].title,
                             time);
                    atomic_store_explicit(&cache[n].state, 2, memory_order_release);
                } else
                    atomic_store_explicit(&cache[n].state, 3, memory_order_release);
                if (logfile) {
                    log_event("lookup title=%s status=%d seconds=%u frames=%d\n", games[n].title,
                              rc, result.main_seconds, atomic_load(&frames));
                }
                usleep(500000);
            }
        atomic_store_explicit(&enabled, access("/system_tmp/hltb-native.enable", F_OK) == 0,
                              memory_order_release);
        usleep(100000);
    }
parked:
    if (logfile) {
        log_event("initialization failed; parked\n");
    }
    for (;;)
        sleep(60);
}
