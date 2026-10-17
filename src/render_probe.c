/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Temporary native draw observation; leaves draw arguments and results intact. */
#include "native_protocol.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
extern void *mono_get_root_domain(void);
extern void *mono_thread_attach(void *);
extern void mono_thread_detach(void *);
extern void *mono_domain_assembly_open(void *, const char *);
extern void *mono_assembly_get_image(void *);
extern void *mono_class_from_name(void *, const char *, const char *);
extern void *mono_class_get_method_from_name(void *, const char *, int);
extern void *mono_compile_method(void *);
extern void *mono_class_get_field_from_name(void *, const char *);
extern int mono_field_get_offset(void *);
extern uint32_t mono_gchandle_new(void *, int);
extern void mono_gchandle_free(uint32_t);
__asm__(".pushsection .text\n.p2align 4\n.global hltb_render_original\n"
        "hltb_render_original:\n.fill 64,1,0xcc\n.popsection\n");
extern void hltb_render_original(void *, void *, void *);
static void *source_element, *extra_element;
static atomic_uint source_draws, extra_draws, all_draws;
static void render_hook(void *element, void *context, void *matrix) {
    atomic_fetch_add_explicit(&all_draws, 1, memory_order_relaxed);
    if (element == source_element)
        atomic_fetch_add_explicit(&source_draws, 1, memory_order_relaxed);
    if (element == extra_element)
        atomic_fetch_add_explicit(&extra_draws, 1, memory_order_relaxed);
    hltb_render_original(element, context, matrix);
}
__attribute__((visibility("default"))) void *elf_main(void *args) {
    (void)args;
    FILE *f = fopen("/system_tmp/hltb-native.log", "r");
    uintptr_t source = 0, extra = 0;
    char line[1024];
    int pid = 0;
    if (!f)
        return NULL;
    if (fgets(line, sizeof line, f))
        sscanf(line, "entered pid=%d", &pid);
    if (pid != getpid()) {
        fclose(f);
        return NULL;
    }
    while (fgets(line, sizeof line, f)) {
        char *p = __builtin_strstr(line, " source=");
        if (p)
            sscanf(p, " source=%lx annotation=%lx", &source, &extra);
    }
    fclose(f);
    if (!source || !extra)
        return NULL;
    void *domain = mono_get_root_domain();
    void *thread = domain ? mono_thread_attach(domain) : NULL;
    if (!thread)
        return NULL;
    uint32_t handles[4] = {mono_gchandle_new((void *)source, 1),
                           mono_gchandle_new((void *)extra, 1), 0, 0};
    void *assembly =
        mono_domain_assembly_open(domain, "/system_ex/common_ex/lib/Sce.PlayStation.PUI.dll");
    void *image = assembly ? mono_assembly_get_image(assembly) : NULL;
    void *label = image ? mono_class_from_name(image, "Sce.PlayStation.PUI.UI3", "Label") : NULL;
    void *field = label ? mono_class_get_field_from_name(label, "element") : NULL;
    int offset = field ? mono_field_get_offset(field) : -1;
    void *cls =
        image ? mono_class_from_name(image, "Sce.PlayStation.PUI.UI3", "LabelElement") : NULL;
    void *method = cls ? mono_class_get_method_from_name(cls, "OnRender", 2) : NULL;
    void *code = method ? mono_compile_method(method) : NULL;
    if (offset < 16 || !code || !handles[0] || !handles[1])
        goto done;
    source_element = *(void **)(source + offset);
    extra_element = *(void **)(extra + offset);
    handles[2] = mono_gchandle_new(source_element, 1);
    handles[3] = mono_gchandle_new(extra_element, 1);
    if (!handles[2] || !handles[3])
        goto done;
    struct hltb_native_request request = {HLTB_NATIVE_MAGIC,
                                          getpid(),
                                          3,
                                          (uintptr_t)code,
                                          (uintptr_t)render_hook,
                                          (uintptr_t)hltb_render_original};
    f = fopen(HLTB_NATIVE_TEXT, "wb");
    if (!f)
        goto done;
    fwrite(&request, 1, sizeof request, f);
    fclose(f);
    do {
        f = fopen(HLTB_NATIVE_LOG, "w");
        if (f) {
            fprintf(f, "entered pid=%d\nsource=%p extra=%p draws=%u,%u all=%u\n", getpid(),
                    source_element, extra_element, atomic_load(&source_draws),
                    atomic_load(&extra_draws), atomic_load(&all_draws));
            fclose(f);
        }
        sleep(1);
    } while (!access(HLTB_NATIVE_TEXT, F_OK));
done:
    for (unsigned n = 0; n < 4; n++)
        if (handles[n])
            mono_gchandle_free(handles[n]);
    mono_thread_detach(thread);
    return NULL;
}
