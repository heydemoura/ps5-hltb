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
extern Class *mono_class_get_parent(Class *);
extern const char *mono_class_get_name(Class *);
extern Class *mono_object_get_class(void *);
extern void *mono_object_new(Domain *, Class *);
extern void *mono_runtime_invoke(Method *, void *, void **, void **);
extern void *mono_object_unbox(void *);
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

static struct hltb_game games[HLTB_CATALOG_MAX];
static size_t game_count;
struct cache {
    atomic_int state;
    char label[100];
};
static struct cache cache[HLTB_CATALOG_MAX];
static atomic_int enabled, frames;
static int parent_off, root_off, disposed_off, span_off, test_off, children_off;
static int span_id_off, span_outer_off, span_inner_off;
static Method *label_display;
struct event {
    uint32_t node, label;
    int index;
    uintptr_t thread;
};
static struct event ring[64];
static atomic_uint write_index, read_index;
static atomic_flag producer_guard = ATOMIC_FLAG_INIT;
static int selected_index = -1;
static uint32_t selected_root_handle;
static int find_game(const char *s) {
    for (size_t i = 0; i < game_count; i++)
        if (!strcmp(s, games[i].title))
            return (int)i;
    return -1;
}
static int string_is(void *str, const char *s) {
    char *value = str ? mono_string_to_utf8(str) : NULL;
    int equal = value && !strcmp(value, s);
    if (value)
        mono_free(value);
    return equal;
}
static void *ptr(void *p, int off) { return p ? *(void **)((char *)p + off) : NULL; }
/* Mono List<T> uses _items at 16 and _size at 32 on this verified firmware. */
static int list_count(void *list) {
    int count = list ? *(int *)((char *)list + 32) : 0;
    return count >= 0 && count <= 500 ? count : 0;
}
static void *list_at(void *list, int index, int stride, int member) {
    void *array = ptr(list, 16);
    return array ? ptr(array, 32 + index * stride + member) : NULL;
}
static int has_span_id(void *span, const char *id, int depth) {
    if (!span || depth > 5)
        return 0;
    if (string_is(ptr(span, span_id_off), id))
        return 1;
    void *list = ptr(span, span_outer_off);
    for (int n = 0; n < list_count(list); n++)
        if (has_span_id(list_at(list, n, 8, 0), id, depth + 1))
            return 1;
    list = ptr(span, span_inner_off);
    for (int n = 0; n < list_count(list); n++)
        if (has_span_id(list_at(list, n, 16, 8), id, depth + 1))
            return 1;
    return 0;
}
static int subtree_has_tooltip(void *node, int depth, int *budget) {
    if (!node || depth > 8 || --*budget < 0)
        return 0;
    /* Only text nodes have a SpannableText field; check their view class first. */
    void *view = ptr(node, 88);
    if ((string_is(view, "RCTText") || string_is(view, "RCTVirtualTextObject")) &&
        has_span_id(ptr(node, span_off), "msgid_playtime_indicates_overall_time", 0))
        return 1;
    void *list = ptr(node, children_off);
    for (int n = 0; n < list_count(list); n++)
        if (subtree_has_tooltip(list_at(list, n, 8, 0), depth + 1, budget))
            return 1;
    return 0;
}
static int is_playtime(void *node) {
    void *span = ptr(node, span_off);
    if (has_span_id(span, "msgid_playtime_indicates_overall_time", 0))
        return 0;
    /* Match the stopwatch row by its tooltip, independent of locale/duration format. */
    void *parent = ptr(node, parent_off);
    void *children = ptr(parent, children_off);
    int icon = 0;
    for (int n = 0; n < list_count(children); n++) {
        void *child = list_at(children, n, 8, 0);
        if (string_is(ptr(child, 88), "RCTImageView"))
            icon = 1;
    }
    if (!icon || !string_is(ptr(node, 88), "RCTText"))
        return 0;
    int budget = 100;
    return subtree_has_tooltip(parent, 0, &budget);
}
static void *invoke(Method *method, void *obj, void **args) {
    if (!method)
        return NULL;
    void *exception = NULL;
    void *result = mono_runtime_invoke(method, obj, args, &exception);
    if (exception)
        log_event("managed exception method=%p\n", method);
    return exception ? NULL : result;
}
static void text_hook(void *node, void *label) {
    uint32_t node_handle = mono_gchandle_new(node, 1);
    uint32_t label_handle = mono_gchandle_new(label, 1);
    hltb_native_text_original(node, label);
    if (!node_handle || !label_handle ||
        atomic_flag_test_and_set_explicit(&producer_guard, memory_order_acquire)) {
        if (node_handle)
            mono_gchandle_free(node_handle);
        if (label_handle)
            mono_gchandle_free(label_handle);
        return;
    }
    void *text = invoke(label_display, label, NULL);
    char *s = text ? mono_string_to_utf8(text) : NULL;
    int index = s ? find_game(s) : -1;
    if (string_is(ptr(node, test_off), "post-cover-concept-name")) {
        selected_index = index;
        if (selected_root_handle)
            mono_gchandle_free(selected_root_handle);
        void *root = ptr(node, root_off);
        selected_root_handle = root ? mono_gchandle_new(root, 1) : 0;
        if (index >= 0) {
            int expected = 0;
            atomic_compare_exchange_strong(&cache[index].state, &expected, 1);
        }
    }
    if (selected_index >= 0 && selected_root_handle &&
        mono_gchandle_get_target(selected_root_handle) == ptr(node, root_off) &&
        is_playtime(node)) {
        unsigned w = atomic_load_explicit(&write_index, memory_order_relaxed);
        unsigned r = atomic_load_explicit(&read_index, memory_order_acquire);
        if (w - r < 64) {
            uint32_t nh = mono_gchandle_new(node, 1), lh = mono_gchandle_new(label, 1);
            if (nh && lh) {
                ring[w % 64] = (struct event){nh, lh, selected_index, (uintptr_t)pthread_self()};
                atomic_store_explicit(&write_index, w + 1, memory_order_release);
            } else {
                if (nh)
                    mono_gchandle_free(nh);
                if (lh)
                    mono_gchandle_free(lh);
            }
        }
        log_event("played title=%s text=%s node=%p label=%p class=%s thread=%lx\n",
                  games[selected_index].title, s ? s : "", node, label,
                  mono_class_get_name(mono_object_get_class(label)), (unsigned long)pthread_self());
    }
    if (s)
        mono_free(s);
    atomic_flag_clear_explicit(&producer_guard, memory_order_release);
    mono_gchandle_free(node_handle);
    mono_gchandle_free(label_handle);
}

/* All widget construction, mutation and disposal occurs in Application.Update. */
static Class *label_class;
static Method *label_ctor, *label_text, *label_parent, *label_disposed;
static Method *append_child, *remove_child, *dispose_label;
static Method *get_x, *get_y, *get_height, *set_x, *set_y, *set_width, *set_height;
struct style_property {
    Method *get, *set;
    int reference;
};
static struct style_property styles[3];
struct tracked {
    uint32_t node, source, annotation;
    int index;
    int rendered;
};
static struct tracked tracked[16];
static Method *method_inherited(Class *c, const char *name, int count) {
    for (; c; c = mono_class_get_parent(c)) {
        Method *m = mono_class_get_method_from_name(c, name, count);
        if (m)
            return m;
    }
    return NULL;
}
static int call(Method *method, void *obj, void **args, void **result) {
    if (!method)
        return 0;
    void *exception = NULL;
    void *value = mono_runtime_invoke(method, obj, args, &exception);
    if (exception) {
        log_event("widget exception method=%p\n", method);
        return 0;
    }
    if (result)
        *result = value;
    return 1;
}
static int is_disposed(void *obj) {
    void *value = NULL;
    return !obj || !call(label_disposed, obj, NULL, &value) || !value ||
           *(unsigned char *)mono_object_unbox(value);
}
static int get_float(Method *m, void *obj, float *out) {
    void *value = NULL;
    if (!call(m, obj, NULL, &value) || !value)
        return 0;
    memcpy(out, mono_object_unbox(value), sizeof *out);
    return 1;
}
static int set_float(Method *m, void *obj, float value) {
    void *args[] = {&value};
    return call(m, obj, args, NULL);
}
static int copy_style(void *source, void *target) {
    for (unsigned n = 0; n < sizeof styles / sizeof styles[0]; n++) {
        void *value = NULL;
        if (!call(styles[n].get, source, NULL, &value))
            return 0;
        uint32_t handle = value ? mono_gchandle_new(value, 1) : 0;
        if (value && !handle)
            return 0;
        void *args[] = {styles[n].reference ? value : value ? mono_object_unbox(value) : NULL};
        int ok = call(styles[n].set, target, args, NULL);
        if (handle)
            mono_gchandle_free(handle);
        if (!ok)
            return 0;
    }
    return 1;
}
static void remove_annotation(struct tracked *t) {
    void *obj = t->annotation ? mono_gchandle_get_target(t->annotation) : NULL;
    if (obj && !is_disposed(obj)) {
        (void)call(remove_child, obj, NULL, NULL);
        (void)call(dispose_label, obj, NULL, NULL);
    }
    if (t->annotation)
        mono_gchandle_free(t->annotation);
    t->annotation = 0;
    t->rendered = 0;
}
static void clear_tracked(struct tracked *t) {
    remove_annotation(t);
    if (t->node)
        mono_gchandle_free(t->node);
    if (t->source)
        mono_gchandle_free(t->source);
    memset(t, 0, sizeof *t);
}
static void render(struct tracked *t) {
    void *node = mono_gchandle_get_target(t->node);
    void *source = mono_gchandle_get_target(t->source);
    if (!node || *(unsigned char *)((char *)node + disposed_off) || is_disposed(source)) {
        clear_tracked(t);
        return;
    }
    if (!atomic_load_explicit(&enabled, memory_order_acquire)) {
        remove_annotation(t);
        return;
    }
    if (atomic_load_explicit(&cache[t->index].state, memory_order_acquire) != 2 || t->rendered)
        return;
    void *parent = NULL;
    if (!call(label_parent, source, NULL, &parent) || !parent || is_disposed(parent))
        return;
    float y, height;
    if (!get_float(get_y, source, &y) || !get_float(get_height, source, &height))
        return;
    uint32_t parent_handle = mono_gchandle_new(parent, 1);
    if (!parent_handle)
        return;
    void *obj = mono_object_new(mono_domain_get(), label_class);
    if (!obj) {
        mono_gchandle_free(parent_handle);
        return;
    }
    t->annotation = mono_gchandle_new(obj, 1);
    if (!t->annotation) {
        mono_gchandle_free(parent_handle);
        t->rendered = -1;
        return;
    }
    if (!call(label_ctor, obj, NULL, NULL)) {
        mono_gchandle_free(t->annotation);
        t->annotation = 0;
        mono_gchandle_free(parent_handle);
        t->rendered = -1;
        return;
    }
    void *text = mono_string_new(mono_domain_get(), cache[t->index].label);
    uint32_t text_handle = text ? mono_gchandle_new(text, 1) : 0;
    void *text_args[] = {text};
    float width = 340.0f;
    int ok = text_handle && copy_style(source, obj) && call(label_text, obj, text_args, NULL) &&
             set_float(set_width, obj, width) && set_float(set_height, obj, height) &&
             set_float(set_x, obj, -width - 16.0f) && set_float(set_y, obj, y);
    if (text_handle)
        mono_gchandle_free(text_handle);
    void *args[] = {obj};
    if (!ok || !call(append_child, parent, args, NULL)) {
        remove_annotation(t);
        t->rendered = -1;
        log_event("annotation construction failed; waiting for next native update\n");
        mono_gchandle_free(parent_handle);
        return;
    }
    void *actual_parent = NULL;
    if (!call(label_parent, obj, NULL, &actual_parent) || actual_parent != parent) {
        remove_annotation(t);
        t->rendered = -1;
        mono_gchandle_free(parent_handle);
        return;
    }
    mono_gchandle_free(parent_handle);
    t->rendered = 1;
    void *played = invoke(label_display, source, NULL);
    char *played_utf8 = played ? mono_string_to_utf8(played) : NULL;
    log_event("rendered title=%s component=%s source=%p annotation=%p same-parent=%p played=%s "
              "completion=%s\n",
              games[t->index].title, mono_class_get_name(mono_object_get_class(obj)), source, obj,
              parent, played_utf8 ? played_utf8 : "", cache[t->index].label);
    if (played_utf8)
        mono_free(played_utf8);
}
static void inspect_label(void *source) {
    void *obj = source;
    for (int depth = 0; obj && depth < 8; depth++) {
        uint32_t object_handle = mono_gchandle_new(obj, 1);
        if (!object_handle)
            break;
        float x = 0, y = 0, width = 0, height = 0;
        (void)get_float(get_x, obj, &x);
        (void)get_float(get_y, obj, &y);
        (void)get_float(method_inherited(label_class, "get_Width", 0), obj, &width);
        (void)get_float(get_height, obj, &height);
        Method *clip = method_inherited(label_class, "get_ClipChildren", 0);
        void *value = NULL;
        int clips = call(clip, obj, NULL, &value) && value
                        ? *(unsigned char *)mono_object_unbox(value)
                        : -1;
        log_event("layout depth=%d class=%s object=%p rect=%g,%g,%g,%g clip=%d\n", depth,
                  mono_class_get_name(mono_object_get_class(obj)), obj, (double)x, (double)y,
                  (double)width, (double)height, clips);
        void *next = NULL;
        int ok = call(label_parent, obj, NULL, &next);
        mono_gchandle_free(object_handle);
        if (!ok)
            break;
        obj = next;
    }
}
static void update_hook(void *app) {
    hltb_native_update_original(app);
    int frame = atomic_fetch_add(&frames, 1);
    if (frame % 600 == 0)
        log_event("frame=%d enabled=%d\n", frame, atomic_load(&enabled));
    unsigned r = atomic_load_explicit(&read_index, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&write_index, memory_order_acquire);
    while (r < w) {
        struct event e = ring[r % 64];
        void *source = mono_gchandle_get_target(e.label);
        size_t slot;
        for (slot = 0; slot < 16; slot++)
            if (tracked[slot].source && mono_gchandle_get_target(tracked[slot].source) == source)
                break;
        if (slot == 16) {
            for (slot = 0; slot < 16; slot++)
                if (!tracked[slot].source)
                    break;
        }
        if (slot < 16 && e.thread == (uintptr_t)pthread_self()) {
            clear_tracked(&tracked[slot]);
            tracked[slot] = (struct tracked){e.node, e.label, 0, e.index, 0};
            inspect_label(source);
        } else {
            mono_gchandle_free(e.node);
            mono_gchandle_free(e.label);
            log_event("ignored played label: capacity/thread guard\n");
        }
        atomic_store_explicit(&read_index, ++r, memory_order_release);
    }
    for (size_t n = 0; n < 16; n++)
        if (tracked[n].source)
            render(&tracked[n]);
}
static int setup_widgets(Image *image) {
    label_class = mono_class_from_name(image, "Sce.PlayStation.PUI.UI3", "Label");
    if (!label_class)
        return 0;
#define METHOD(variable, name, count)                                                              \
    variable = method_inherited(label_class, name, count);                                         \
    if (!variable) {                                                                               \
        log_event("missing widget method=%s\n", name);                                             \
        return 0;                                                                                  \
    }
    METHOD(label_ctor, ".ctor", 0)
    METHOD(label_text, "set_Text", 1)
    METHOD(label_parent, "get_Parent", 0)
    METHOD(label_disposed, "get_IsDisposed", 0)
    METHOD(append_child, "AppendChild", 1)
    METHOD(remove_child, "RemoveFromParent", 0)
    METHOD(dispose_label, "Dispose", 0)
    METHOD(get_x, "get_X", 0)
    METHOD(get_y, "get_Y", 0)
    METHOD(get_height, "get_Height", 0)
    METHOD(set_x, "set_X", 1)
    METHOD(set_y, "set_Y", 1)
    METHOD(set_width, "set_Width", 1)
    METHOD(set_height, "set_Height", 1)
#undef METHOD
    const char *names[] = {"FontConfig", "Font", "TextColor"};
    for (unsigned n = 0; n < 3; n++) {
        char name[80];
        snprintf(name, sizeof name, "get_%s", names[n]);
        styles[n].get = method_inherited(label_class, name, 0);
        snprintf(name, sizeof name, "set_%s", names[n]);
        styles[n].set = method_inherited(label_class, name, 1);
        styles[n].reference = n == 0;
        if (!styles[n].get || !styles[n].set)
            return 0;
    }
    return 1;
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
            snprintf(cache[n].label, sizeof cache[n].label, "%s", time);
            atomic_store(&cache[n].state, 2);
        }
    }
    Assembly *a = mono_domain_assembly_open(domain, "/system_ex/common_ex/lib/ReactNative.PUI.dll");
    Image *i = a ? mono_assembly_get_image(a) : NULL;
    Class *raw =
        i ? mono_class_from_name(i, "ReactNative.Views.UI3.View", "ReactTextShadowNode") : NULL;
    Class *base = i ? mono_class_from_name(i, "ReactNative.UIManager", "ReactShadowNode") : NULL;
    if (!raw || !base)
        goto parked;
    Class *text_base = mono_class_get_parent(raw);
    parent_off = field_offset(base, "_parent");
    root_off = field_offset(base, "_rootNode");
    children_off = field_offset(base, "_children");
    disposed_off = field_offset(base, "_disposed");
    span_off = field_offset(text_base, "<SpannableText>k__BackingField");
    test_off = field_offset(text_base, "<TestId>k__BackingField");
    if (parent_off != 40 || root_off != 16 || children_off != 32 || span_off != 152 ||
        test_off != 184)
        goto parked;
    Method *m = mono_class_get_method_from_name(raw, "UpdateLabel", 1);
    void *text = m ? mono_compile_method(m) : NULL;
    a = mono_domain_assembly_open(domain, "/system_ex/common_ex/lib/Sce.PlayStation.PUI.dll");
    i = a ? mono_assembly_get_image(a) : NULL;
    Class *application = i ? mono_class_from_name(i, "Sce.PlayStation.PUI", "Application") : NULL;
    m = application ? mono_class_get_method_from_name(application, "Update", 0) : NULL;
    void *update = m ? mono_compile_method(m) : NULL;
    if (!text || !update)
        goto parked;
    Class *span_class = mono_class_from_name(i, "Sce.PlayStation.PUI.UI3", "SpannableText");
    Class *label_class = mono_class_from_name(i, "Sce.PlayStation.PUI.UI3", "Label");
    if (!span_class || !label_class)
        goto parked;
    span_id_off = field_offset(span_class, "<Id>k__BackingField");
    span_outer_off = field_offset(span_class, "outerSpans");
    span_inner_off = field_offset(span_class, "innerSpans");
    if (span_id_off != 16 || span_outer_off != 80 || span_inner_off != 72)
        goto parked;
    label_display = mono_class_get_method_from_name(label_class, "get_DisplayText", 0);
    if (!label_display || !setup_widgets(i))
        goto parked;
    log_event("catalog=%zu UpdateLabel=%p Update=%p playtime observer ready\n", game_count, text,
              update);
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
                    snprintf(cache[n].label, sizeof cache[n].label, "%s", time);
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
