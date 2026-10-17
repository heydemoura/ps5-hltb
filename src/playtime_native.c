/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "cache.h"
#include "catalog.h"
#include "hltb.h"
#include "native_protocol.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
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
    char label[320];
};
static struct cache cache[HLTB_CATALOG_MAX];
static atomic_int enabled, frames;
static int parent_off, root_off, disposed_off, span_off, test_off, children_off;
static int span_id_off, span_outer_off, span_inner_off;
static Method *label_display;
struct event {
    uint32_t node, label, title;
    int index;
    uintptr_t thread;
};
static struct event ring[64];
static atomic_uint write_index, read_index;
static atomic_flag producer_guard = ATOMIC_FLAG_INIT;
static int selected_index = -1;
static uint32_t selected_root_handle, selected_title_handle;
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
        if (selected_title_handle)
            mono_gchandle_free(selected_title_handle);
        selected_title_handle = mono_gchandle_new(label, 1);
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
            void *title =
                selected_title_handle ? mono_gchandle_get_target(selected_title_handle) : NULL;
            uint32_t nh = mono_gchandle_new(node, 1), lh = mono_gchandle_new(label, 1);
            uint32_t th = title ? mono_gchandle_new(title, 1) : 0;
            if (nh && lh && th) {
                ring[w % 64] =
                    (struct event){nh, lh, th, selected_index, (uintptr_t)pthread_self()};
                atomic_store_explicit(&write_index, w + 1, memory_order_release);
            } else {
                if (nh)
                    mono_gchandle_free(nh);
                if (lh)
                    mono_gchandle_free(lh);
                if (th)
                    mono_gchandle_free(th);
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
static Class *label_class, *panel_class;
static Method *label_ctor, *panel_ctor, *label_text, *label_parent, *label_disposed, *measure_text;
static Method *append_child, *remove_child, *dispose_label;
static Method *get_background, *set_background, *get_radius, *set_radius, *set_max_lines;
static Class *native_panel_base;
static int first_child_off, next_sibling_off;
static Method *get_x, *get_y, *get_width, *get_height, *set_x, *set_y, *set_width, *set_height;
static float (*native_x)(void *), (*native_y)(void *), (*native_width)(void *),
    (*native_height)(void *);
static float (*native_opacity)(void *), (*native_final_opacity)(void *);
static void *(*native_parent)(void *);
static bool (*native_shown)(void *), (*native_visible)(void *);
static void (*native_set_x)(void *, float), (*native_set_y)(void *, float);
static void (*native_set_opacity)(void *, float), (*native_set_visible)(void *, bool);
struct style_property {
    Method *get, *set;
    int reference;
};
static struct style_property styles[3];
struct tracked {
    uint32_t node, source, title, annotation, container, attach_parent;
    uint32_t path[12];
    unsigned path_count;
    int index, rendered, measure_attempts, measure_ready;
    float panel_width, panel_height, line_height;
    float pad_left, pad_right, pad_top, pad_bottom;
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
static void measure_padding(struct tracked *t, void *source) {
    void *row = native_parent(source);
    float w = native_width(row), h = native_height(row);
    float left = native_x(source), top = native_y(source);
    float right = w - left - native_width(source), bottom = h - top - native_height(source);
    void *child = ptr(row, first_child_off);
    for (unsigned n = 0; child && n < 32; n++, child = ptr(child, next_sibling_off)) {
        if (!native_shown(child) || native_width(child) <= 0 || native_height(child) <= 0)
            continue;
        float x = native_x(child), y = native_y(child);
        float r = w - x - native_width(child), b = h - y - native_height(child);
        if (x >= 0 && x < left)
            left = x;
        if (y >= 0 && y < top)
            top = y;
        if (r >= 0 && r < right)
            right = r;
        if (b >= 0 && b < bottom)
            bottom = b;
    }
    /* Requested horizontal spacing supersedes the native row's smaller insets. */
    t->pad_left = 24.0f;
    t->pad_right = 24.0f;
    t->pad_top = top >= 0 && top <= 128 ? top : 0;
    t->pad_bottom = bottom >= 0 && bottom <= 128 ? bottom : 0;
}
static int copy_background(struct tracked *t, void *container) {
    for (unsigned n = 0; n < t->path_count; n++) {
        void *source = mono_gchandle_get_target(t->path[n]);
        Class *cls = mono_object_get_class(source);
        while (cls && cls != native_panel_base)
            cls = mono_class_get_parent(cls);
        if (!cls)
            continue;
        void *value = NULL;
        if (!call(get_background, source, NULL, &value) || !value)
            return 0;
        uint32_t handle = mono_gchandle_new(value, 1);
        if (!handle)
            return 0;
        float rgba[4];
        memcpy(rgba, mono_object_unbox(value), sizeof rgba);
        if (rgba[3] > 0) {
            void *args[] = {mono_object_unbox(value)};
            int ok = call(set_background, container, args, NULL);
            float radius = 0;
            ok = ok && get_float(get_radius, source, &radius) &&
                 set_float(set_radius, container, radius);
            log_event("metrics background=%g,%g,%g,%g padding=%g,%g,%g,%g radius=%g\n",
                      (double)rgba[0], (double)rgba[1], (double)rgba[2], (double)rgba[3],
                      (double)t->pad_left, (double)t->pad_right, (double)t->pad_top,
                      (double)t->pad_bottom, (double)radius);
            mono_gchandle_free(handle);
            return ok;
        }
        mono_gchandle_free(handle);
    }
    log_event("played-time background not found\n");
    return 0;
}
static int metric_width(void *label, float *out) {
    float limit = 600;
    void *args[] = {&limit}, *value = NULL;
    if (!call(measure_text, label, args, &value) || !value)
        return 0;
    memcpy(out, mono_object_unbox(value), sizeof *out);
    return 1;
}
static void release_anchors(struct tracked *t) {
    for (unsigned n = 0; n < t->path_count; n++)
        mono_gchandle_free(t->path[n]);
    t->path_count = 0;
    if (t->attach_parent)
        mono_gchandle_free(t->attach_parent);
    t->attach_parent = 0;
}
static void remove_annotation(struct tracked *t) {
    void *container = t->container ? mono_gchandle_get_target(t->container) : NULL;
    void *label = t->annotation ? mono_gchandle_get_target(t->annotation) : NULL;
    if (container && !is_disposed(container)) {
        (void)call(remove_child, container, NULL, NULL);
        (void)call(dispose_label, container, NULL, NULL);
    }
    if (label && !is_disposed(label))
        (void)call(dispose_label, label, NULL, NULL);
    if (t->container)
        mono_gchandle_free(t->container);
    if (t->annotation)
        mono_gchandle_free(t->annotation);
    t->container = t->annotation = 0;
    t->rendered = 0;
    release_anchors(t);
}
/* Use the native common ancestor with the game title, outside the played-time
 * control's private wrappers, regardless of their focus-dependent dimensions. */
static int capture_anchors(struct tracked *t, void *source) {
    release_anchors(t);
    void *title = mono_gchandle_get_target(t->title);
    if (!title || is_disposed(title))
        return 0;
    uint32_t title_path[48] = {0};
    unsigned title_count = 0;
    void *obj = native_parent(title);
    while (obj && title_count < 48) {
        uint32_t handle = mono_gchandle_new(obj, 1);
        if (!handle)
            break;
        title_path[title_count++] = handle;
        obj = native_parent(obj);
    }
    void *parent = native_parent(source);
    for (unsigned depth = 0; parent && depth < 12; depth++) {
        uint32_t handle = mono_gchandle_new(parent, 1);
        if (!handle)
            break;
        int common = 0;
        for (unsigned n = 0; n < title_count; n++)
            if (mono_gchandle_get_target(title_path[n]) == parent)
                common = 1;
        if (common && t->path_count) {
            t->attach_parent = handle;
            break;
        }
        t->path[t->path_count++] = handle;
        parent = native_parent(parent);
    }
    for (unsigned n = 0; n < title_count; n++)
        mono_gchandle_free(title_path[n]);
    if (t->attach_parent)
        return 1;
    release_anchors(t);
    return 0;
}
static int position_container(struct tracked *t) {
    void *source = mono_gchandle_get_target(t->source);
    void *container = mono_gchandle_get_target(t->container);
    void *parent = t->attach_parent ? mono_gchandle_get_target(t->attach_parent) : NULL;
    if (!source || !container || !parent || is_disposed(container) || is_disposed(parent))
        return 0;
    float y = native_y(source);
    void *current = source, *control = NULL;
    for (unsigned n = 0; n < t->path_count; n++) {
        control = mono_gchandle_get_target(t->path[n]);
        if (!control || native_parent(current) != control || is_disposed(control))
            return 0;
        y += native_y(control);
        current = control;
    }
    if (!control || native_parent(control) != parent)
        return 0;
    float right = native_x(control) + native_width(control);
    float x = right - t->panel_width;
    if (x < 24.0f)
        x = 24.0f;
    float target_y = y - t->pad_top - 16.0f - t->panel_height;
    if (native_x(container) != x)
        native_set_x(container, x);
    if (native_y(container) != target_y)
        native_set_y(container, target_y);
    int visible = native_shown(source);
    if (native_visible(container) != visible)
        native_set_visible(container, visible);
    float parent_opacity = native_final_opacity(parent);
    float opacity = parent_opacity > 0 ? native_final_opacity(source) / parent_opacity : 0;
    if (opacity < 0)
        opacity = 0;
    if (opacity > 1)
        opacity = 1;
    if (native_opacity(container) != opacity)
        native_set_opacity(container, opacity);
    return 1;
}
static void clear_tracked(struct tracked *t) {
    remove_annotation(t);
    if (t->node)
        mono_gchandle_free(t->node);
    if (t->source)
        mono_gchandle_free(t->source);
    if (t->title)
        mono_gchandle_free(t->title);
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
    if (t->rendered > 0) {
        if (!t->measure_ready && t->measure_attempts++ < 60) {
            void *label = mono_gchandle_get_target(t->annotation);
            void *container = mono_gchandle_get_target(t->container);
            float width;
            if (label && container && metric_width(label, &width) && width > 0 && width <= 600) {
                t->panel_width = width + t->pad_left + t->pad_right;
                if (set_float(set_width, label, width) &&
                    set_float(set_width, container, t->panel_width))
                    t->measure_ready = 1;
            }
        }
        if (!position_container(t))
            remove_annotation(t);
        return;
    }
    if (t->rendered < 0 || atomic_load_explicit(&cache[t->index].state, memory_order_acquire) != 2)
        return;
    float height;
    if (!get_float(get_height, source, &height) || height <= 0)
        return;
    measure_padding(t, source);
    height *= 3;
    void *label = mono_object_new(mono_domain_get(), label_class);
    if (!label)
        return;
    t->annotation = mono_gchandle_new(label, 1);
    if (!t->annotation)
        return;
    if (!call(label_ctor, label, NULL, NULL))
        goto fail;
    void *text = mono_string_new(mono_domain_get(), cache[t->index].label);
    uint32_t text_handle = text ? mono_gchandle_new(text, 1) : 0;
    void *text_args[] = {text};
    int max_lines = 3;
    void *line_args[] = {&max_lines};
    int ok = text_handle && copy_style(source, label) &&
             call(set_max_lines, label, line_args, NULL) &&
             call(label_text, label, text_args, NULL);
    if (text_handle)
        mono_gchandle_free(text_handle);
    if (!ok)
        goto fail;
    float width = 340, measured;
    t->measure_attempts = t->measure_ready = 0;
    if (metric_width(label, &measured) && measured > 0 && measured <= 600) {
        width = measured;
        t->measure_ready = 1;
    }
    t->line_height = height;
    t->panel_width = width + t->pad_left + t->pad_right;
    t->panel_height = height + t->pad_top + t->pad_bottom;
    if (!capture_anchors(t, source))
        goto fail;
    void *container = mono_object_new(mono_domain_get(), panel_class);
    if (!container)
        goto fail;
    t->container = mono_gchandle_new(container, 1);
    if (!t->container || !call(panel_ctor, container, NULL, NULL))
        goto fail;
    void *parent = mono_gchandle_get_target(t->attach_parent);
    void *label_args[] = {label}, *container_args[] = {container};
    if (!set_float(set_width, label, width) || !set_float(set_height, label, height) ||
        !set_float(set_x, label, t->pad_left) || !set_float(set_y, label, t->pad_top) ||
        !set_float(set_width, container, t->panel_width) ||
        !set_float(set_height, container, t->panel_height) || !copy_background(t, container) ||
        !call(append_child, container, label_args, NULL) || !position_container(t) ||
        !call(append_child, parent, container_args, NULL))
        goto fail;
    t->rendered = 1;
    void *played = invoke(label_display, source, NULL);
    char *played_utf8 = played ? mono_string_to_utf8(played) : NULL;
    log_event("rendered title=%s source=%p annotation=%p container=%p sibling-parent=%p played=%s "
              "completion=%s\n",
              games[t->index].title, source, label, container, parent,
              played_utf8 ? played_utf8 : "", cache[t->index].label);
    if (played_utf8)
        mono_free(played_utf8);
    return;
fail:
    remove_annotation(t);
    t->rendered = -1;
    log_event("sibling annotation construction failed; waiting for native update\n");
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
            tracked[slot] = (struct tracked){
                .node = e.node, .source = e.label, .title = e.title, .index = e.index};
            inspect_label(source);
        } else {
            mono_gchandle_free(e.node);
            mono_gchandle_free(e.label);
            mono_gchandle_free(e.title);
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
    METHOD(get_width, "get_Width", 0)
    METHOD(get_height, "get_Height", 0)
    METHOD(set_x, "set_X", 1)
    METHOD(set_y, "set_Y", 1)
    METHOD(set_width, "set_Width", 1)
    METHOD(set_height, "set_Height", 1)
    METHOD(measure_text, "GetLongestLineWidth", 1)
    METHOD(set_max_lines, "set_MaxLineCount", 1)
#undef METHOD
#define NATIVE(variable, name, count)                                                              \
    do {                                                                                           \
        Method *m = method_inherited(label_class, name, count);                                    \
        *(void **)(&variable) = m ? mono_compile_method(m) : NULL;                                 \
        if (!variable) {                                                                           \
            log_event("missing scalar method=%s\n", name);                                         \
            return 0;                                                                              \
        }                                                                                          \
    } while (0)
    NATIVE(native_x, "get_X", 0);
    NATIVE(native_y, "get_Y", 0);
    NATIVE(native_width, "get_Width", 0);
    NATIVE(native_height, "get_Height", 0);
    NATIVE(native_parent, "get_Parent", 0);
    NATIVE(native_shown, "get_IsFinallyShown", 0);
    NATIVE(native_visible, "get_Visible", 0);
    NATIVE(native_set_visible, "set_Visible", 1);
    NATIVE(native_opacity, "get_Opacity", 0);
    NATIVE(native_final_opacity, "get_FinalOpacity", 0);
    NATIVE(native_set_opacity, "set_Opacity", 1);
    NATIVE(native_set_x, "set_X", 1);
    NATIVE(native_set_y, "set_Y", 1);
#undef NATIVE
    if (!panel_class || !(panel_ctor = mono_class_get_method_from_name(panel_class, ".ctor", 0)))
        return 0;
    native_panel_base = mono_class_from_name(image, "Sce.PlayStation.PUI.UI3", "Panel");
    Class *widget = mono_class_from_name(image, "Sce.PlayStation.PUI.UI3", "Widget");
    if (!native_panel_base || !widget)
        return 0;
    Field *first = mono_class_get_field_from_name(widget, "firstChild");
    Field *next = mono_class_get_field_from_name(widget, "nextSibling");
    first_child_off = first ? mono_field_get_offset(first) : -1;
    next_sibling_off = next ? mono_field_get_offset(next) : -1;
    get_background = method_inherited(native_panel_base, "get_BackgroundColor", 0);
    set_background = method_inherited(native_panel_base, "set_BackgroundColor", 1);
    get_radius = method_inherited(native_panel_base, "get_BorderRadius", 0);
    set_radius = method_inherited(native_panel_base, "set_BorderRadius", 1);
    if (first_child_off < 16 || next_sibling_off < 16 || !get_background || !set_background ||
        !get_radius || !set_radius)
        return 0;
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
        char time[320];
        if (!hltb_cache_read(games[n].title, &r) && !hltb_format_metrics(&r, time, sizeof time)) {
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
    panel_class = mono_class_from_name(i, "ReactNative.Views.UI3.View", "ReactPanel");
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
                char time[320];
                if (!rc && !hltb_format_metrics(&result, time, sizeof time)) {
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
