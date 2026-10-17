/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Diagnostic injected thread: metadata lookup only, no UI or method patches. */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
typedef void Domain;
typedef void Assembly;
typedef void Image;
typedef void Class;
typedef void Method;
typedef void Field;
extern Domain *mono_get_root_domain(void);
extern void *mono_thread_attach(Domain *);
extern Assembly *mono_domain_assembly_open(Domain *, const char *);
extern Image *mono_assembly_get_image(Assembly *);
extern Class *mono_class_from_name(Image *, const char *, const char *);
extern Method *mono_class_get_method_from_name(Class *, const char *, int);
extern void *mono_compile_method(Method *);
extern Field *mono_class_get_field_from_name(Class *, const char *);
extern int mono_field_get_offset(Field *);
__attribute__((visibility("default"), noreturn)) void *elf_main(void *args) {
    (void)args;
    FILE *f = fopen("/system_tmp/hltb-shell-probe-v2.log", "w");
    if (f) {
        fprintf(f, "entered pid=%d\n", getpid());
        fflush(f);
    }
    Domain *d = mono_get_root_domain();
    if (f) {
        fprintf(f, "root domain=%p\n", d);
        fflush(f);
    }
    if (d) {
        mono_thread_attach(d);
        Assembly *a = mono_domain_assembly_open(d, "/system_ex/common_ex/lib/ReactNative.PUI.dll");
        Image *i = a ? mono_assembly_get_image(a) : NULL;
        Class *c =
            i ? mono_class_from_name(i, "ReactNative.Views.UI3.View", "ReactRawTextShadowNode")
              : NULL;
        Method *m = c ? mono_class_get_method_from_name(c, "SetText", 1) : NULL;
        Field *field = c ? mono_class_get_field_from_name(c, "_text") : NULL;
        void *code = m ? mono_compile_method(m) : NULL;
        if (f) {
            fprintf(f, "assembly=%p image=%p class=%p method=%p code=%p text-offset=%d\n", a, i, c,
                    m, code, field ? mono_field_get_offset(field) : -1);
            fflush(f);
        }
    }
    if (d) {
        Assembly *a =
            mono_domain_assembly_open(d, "/system_ex/common_ex/lib/Sce.PlayStation.PUI.dll");
        Image *i = a ? mono_assembly_get_image(a) : NULL;
        Class *c = i ? mono_class_from_name(i, "Sce.PlayStation.PUI", "Application") : NULL;
        Method *m = c ? mono_class_get_method_from_name(c, "Update", 0) : NULL;
        if (f) {
            fprintf(f, "Application.Update=%p\n", m ? mono_compile_method(m) : NULL);
            fflush(f);
        }
        a = mono_domain_assembly_open(d, "/system_ex/common_ex/lib/ReactNative.PUI.dll");
        i = a ? mono_assembly_get_image(a) : NULL;
        c = i ? mono_class_from_name(i, "ReactNative.UIManager", "ReactShadowNode") : NULL;
        const char *fields[] = {"_rootNode",
                                "_parent",
                                "_children",
                                "_themedContext",
                                "<ReactTag>k__BackingField",
                                "<ViewClass>k__BackingField",
                                "_yogaNode",
                                "_absoluteLeft",
                                "_absoluteTop"};
        for (unsigned n = 0; c && n < sizeof fields / sizeof fields[0]; n++) {
            Field *field = mono_class_get_field_from_name(c, fields[n]);
            if (f)
                fprintf(f, "field %s offset=%d\n", fields[n],
                        field ? mono_field_get_offset(field) : -1);
        }
    }
    if (f) {
        fputs("parked; no patches installed\n", f);
        fclose(f);
    }
    for (;;)
        sleep(60);
}
