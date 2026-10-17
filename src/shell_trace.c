/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Temporary SetText tracing to identify the selected game's native node. */
#include "trace_protocol.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
typedef void Domain;
typedef void Assembly;
typedef void Image;
typedef void Class;
typedef void Method;
extern Domain *mono_get_root_domain(void);
extern void *mono_thread_attach(Domain *);
extern Assembly *mono_domain_assembly_open(Domain *, const char *);
extern Image *mono_assembly_get_image(Assembly *);
extern Class *mono_class_from_name(Image *, const char *, const char *);
extern Method *mono_class_get_method_from_name(Class *, const char *, int);
extern void *mono_compile_method(Method *);
extern char *mono_string_to_utf8(void *);
extern void mono_free(void *);
/* Controller writes this executable padding only while the target is stopped. */
__asm__(".pushsection .text\n.p2align 4\n.global "
        "hltb_trace_trampoline\nhltb_trace_trampoline:\n.fill 64,1,0xcc\n.popsection\n");
extern void hltb_trace_trampoline(void *, void *);
struct event {
    uintptr_t node;
    char text[512];
};
static struct event ring[64];
static atomic_uint write_index, read_index, dropped;
static atomic_flag guard = ATOMIC_FLAG_INIT;
static void text_hook(void *node, void *text) {
    if (text && !atomic_flag_test_and_set_explicit(&guard, memory_order_acquire)) {
        unsigned w = atomic_load_explicit(&write_index, memory_order_relaxed),
                 r = atomic_load_explicit(&read_index, memory_order_acquire);
        if (w - r < 64) {
            char *s = mono_string_to_utf8(text);
            if (s) {
                ring[w % 64].node = (uintptr_t)node;
                snprintf(ring[w % 64].text, sizeof ring[w % 64].text, "%s", s);
                mono_free(s);
                atomic_store_explicit(&write_index, w + 1, memory_order_release);
            }
        } else
            atomic_fetch_add(&dropped, 1);
        atomic_flag_clear_explicit(&guard, memory_order_release);
    }
    hltb_trace_trampoline(node, text);
}
__attribute__((visibility("default"), noreturn)) void *elf_main(void *args) {
    (void)args;
    FILE *f = fopen(HLTB_TRACE_LOG, "w");
    if (f) {
        fprintf(f, "entered pid=%d\n", getpid());
        fflush(f);
    }
    Domain *d = mono_get_root_domain();
    if (d) {
        mono_thread_attach(d);
        Assembly *a = mono_domain_assembly_open(d, "/system_ex/common_ex/lib/ReactNative.PUI.dll");
        Image *i = a ? mono_assembly_get_image(a) : NULL;
        Class *c =
            i ? mono_class_from_name(i, "ReactNative.Views.UI3.View", "ReactRawTextShadowNode")
              : NULL;
        Method *m = c ? mono_class_get_method_from_name(c, "SetText", 1) : NULL;
        void *code = m ? mono_compile_method(m) : NULL;
        if (f) {
            fprintf(f, "SetText=%p hook=%p trampoline=%p\n", code, text_hook,
                    hltb_trace_trampoline);
            fflush(f);
        }
        if (code) {
            struct hltb_trace_request q = {HLTB_TRACE_MAGIC,
                                           getpid(),
                                           0,
                                           (uintptr_t)code,
                                           (uintptr_t)text_hook,
                                           (uintptr_t)hltb_trace_trampoline};
            FILE *r = fopen(HLTB_TRACE_REQUEST ".tmp", "wb");
            if (r) {
                fwrite(&q, 1, sizeof q, r);
                fclose(r);
                rename(HLTB_TRACE_REQUEST ".tmp", HLTB_TRACE_REQUEST);
            }
        }
    }
    unsigned logged = 0;
    for (;;) {
        unsigned r = atomic_load_explicit(&read_index, memory_order_relaxed),
                 w = atomic_load_explicit(&write_index, memory_order_acquire);
        while (r < w) {
            if (f && logged < 2000) {
                fprintf(f, "node=%lx text=%s\n", (unsigned long)ring[r % 64].node,
                        ring[r % 64].text);
                logged++;
            }
            r++;
            atomic_store_explicit(&read_index, r, memory_order_release);
        }
        if (f)
            fflush(f);
        usleep(100000);
    }
}
