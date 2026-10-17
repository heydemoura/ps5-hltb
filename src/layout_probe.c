/* SPDX-License-Identifier: GPL-3.0-or-later */
/* One-shot metadata inspection; does not patch or mutate shell widgets. */
#include <stdio.h>
#include <unistd.h>
typedef void Class;
extern void *mono_get_root_domain(void);
extern void *mono_thread_attach(void *);
extern void mono_thread_detach(void *);
extern void *mono_domain_assembly_open(void *, const char *);
extern void *mono_assembly_get_image(void *);
extern Class *mono_class_from_name(void *, const char *, const char *);
extern Class *mono_class_get_parent(Class *);
extern const char *mono_class_get_name(Class *);
extern void *mono_class_get_fields(Class *, void **);
extern const char *mono_field_get_name(void *);
extern int mono_field_get_offset(void *);
extern void *mono_class_get_method_from_name(Class *, const char *, int);
extern void *mono_compile_method(void *);
static void fields(FILE *f, Class *c) {
    for (int depth = 0; c && depth < 8; depth++, c = mono_class_get_parent(c)) {
        fprintf(f, "class=%s\n", mono_class_get_name(c));
        void *iter = NULL, *field;
        while ((field = mono_class_get_fields(c, &iter)))
            fprintf(f, " field=%s offset=%d\n", mono_field_get_name(field),
                    mono_field_get_offset(field));
    }
}
__attribute__((visibility("default"))) void *elf_main(void *args) {
    (void)args;
    FILE *f = fopen("/system_tmp/hltb-layout-probe.log", "w");
    void *domain = mono_get_root_domain();
    if (!f || !domain) {
        if (f)
            fclose(f);
        return NULL;
    }
    fprintf(f, "entered pid=%d\n", getpid());
    void *thread = mono_thread_attach(domain);
    void *assembly =
        mono_domain_assembly_open(domain, "/system_ex/common_ex/lib/ReactNative.PUI.dll");
    void *image = assembly ? mono_assembly_get_image(assembly) : NULL;
    const char *names[] = {"ReactTextShadowNode", "ReactVirtualTextObjectShadowNode"};
    for (unsigned n = 0; image && n < sizeof names / sizeof names[0]; n++) {
        Class *c = mono_class_from_name(image, "ReactNative.Views.UI3.View", names[n]);
        fields(f, c);
    }
    Class *c =
        image ? mono_class_from_name(image, "ReactNative.Bridge", "DispatcherHelpers") : NULL;
    void *m = c ? mono_class_get_method_from_name(c, "RunOnDispatcherCpp", 2) : NULL;
    fprintf(f, "RunOnDispatcherCpp=%p\n", m ? mono_compile_method(m) : NULL);
    assembly =
        mono_domain_assembly_open(domain, "/system_ex/common_ex/lib/Sce.PlayStation.PUI.dll");
    image = assembly ? mono_assembly_get_image(assembly) : NULL;
    c = image ? mono_class_from_name(image, "Sce.PlayStation.PUI.UI3", "SpannableText") : NULL;
    fields(f, c);
    c = mono_class_from_name(image, "Sce.PlayStation.PUI.UI3", "Label");
    fields(f, c);
    fields(f, mono_class_from_name(image, "Sce.PlayStation.PUI.UI3", "LabelElement"));
    const char *methods[] = {".ctor",    "get_DisplayText", "get_Parent",
                             "get_Font", "set_Font",        "AppendChild"};
    const int counts[] = {0, 0, 0, 0, 1, 1};
    for (unsigned n = 0; n < sizeof counts / sizeof counts[0]; n++) {
        m = mono_class_get_method_from_name(c, methods[n], counts[n]);
        fprintf(f, "Label.%s=%p\n", methods[n], m ? mono_compile_method(m) : NULL);
    }
    assembly = mono_domain_assembly_open(domain, "/system_ex/common_ex/lib/ReactNative.PUI.dll");
    image = assembly ? mono_assembly_get_image(assembly) : NULL;
    c = mono_class_from_name(image, "ReactNative.Views.UI3.View", "ReactTextShadowNode");
    m = mono_class_get_method_from_name(c, "UpdateLabel", 1);
    fprintf(f, "ReactTextShadowNode.UpdateLabel=%p\n", m ? mono_compile_method(m) : NULL);
    fclose(f);
    mono_thread_detach(thread);
    return NULL;
}
