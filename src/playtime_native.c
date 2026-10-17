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
#include <time.h>
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
extern void mono_thread_detach(void *);
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

#ifndef HLTB_RUNTIME_TEST
__asm__(
    ".pushsection .text\n.p2align 4\n.global "
    "hltb_native_text_original\nhltb_native_text_original:\n.fill 64,1,0xcc\n.p2align 4\n.global "
    "hltb_native_update_original\nhltb_native_update_original:\n.fill 64,1,0xcc\n.popsection\n");
#endif
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
    atomic_long expires;
    atomic_uint revision;
    char label[320];
};
static struct cache cache[HLTB_CATALOG_MAX];
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static atomic_int enabled, frames;
static int parent_off, root_off, disposed_off, span_off, test_off, children_off;
static int span_id_off, span_outer_off, span_inner_off;
static Method *label_display;
struct event {
    uint32_t node, label, title;
    int index;
    uintptr_t thread;
};
/* UI-owned latest-value mailbox: never retain a queue of obsolete game trees. */
static struct event pending;
static unsigned selection_generation, pending_generation;
static int selected_index = -1;
static uint32_t selected_root_handle, selected_title_handle, selected_title_node_handle;
static unsigned selected_frame;
static atomic_uintptr_t ui_thread;
static atomic_int requested_index = -1;
static unsigned switches, panels_created, panels_removed, events_coalesced;
static unsigned live_handles, peak_handles;
static uint32_t retain(void *obj) {
    uint32_t handle = obj ? mono_gchandle_new(obj, 1) : 0;
    if (handle && ++live_handles > peak_handles)
        peak_handles = live_handles;
    return handle;
}
static void release(uint32_t handle) {
    if (handle) {
        mono_gchandle_free(handle);
        live_handles--;
    }
}
static void clear_pending(void) {
    release(pending.node);
    release(pending.label);
    release(pending.title);
    memset(&pending, 0, sizeof pending);
}
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
    /* Reject other threads before reading or retaining any managed UI state.
     * Application.Update establishes ownership before observation starts. */
    if (atomic_load_explicit(&ui_thread, memory_order_acquire) != (uintptr_t)pthread_self() ||
        !atomic_load_explicit(&enabled, memory_order_acquire)) {
        hltb_native_text_original(node, label);
        return;
    }
    uint32_t node_handle = retain(node), label_handle = retain(label);
    hltb_native_text_original(node, label);
    if (!node_handle || !label_handle)
        goto done;
    if (string_is(ptr(node, test_off), "post-cover-concept-name")) {
        void *text = invoke(label_display, label, NULL);
        char *s = text ? mono_string_to_utf8(text) : NULL;
        int index = s ? find_game(s) : -1;
        if (s)
            mono_free(s);
        void *root = ptr(node, root_off);
        if (index != selected_index || !selected_title_handle ||
            mono_gchandle_get_target(selected_title_handle) != label ||
            !selected_root_handle || mono_gchandle_get_target(selected_root_handle) != root) {
            log_event("selection from=%d to=%d generation=%u\n", selected_index, index, selection_generation + 1);
            clear_pending();
            release(selected_title_handle);
            release(selected_title_node_handle);
            release(selected_root_handle);
            selected_title_handle = retain(label);
            selected_title_node_handle = retain(node);
            selected_frame = (unsigned)atomic_load_explicit(&frames, memory_order_relaxed);
            selected_root_handle = retain(root);
            selected_index = index;
            selection_generation++;
            switches++;
            atomic_store_explicit(&requested_index, index, memory_order_release);
        }
    }
    if (selected_index < 0 || !selected_root_handle ||
        mono_gchandle_get_target(selected_root_handle) != ptr(node, root_off) || !is_playtime(node))
        goto done;
    if (pending.node)
        events_coalesced++;
    clear_pending();
    pending = (struct event){retain(node), retain(label),
                            retain(mono_gchandle_get_target(selected_title_handle)),
                            selected_index, (uintptr_t)pthread_self()};
    pending_generation = selection_generation;
    if (!pending.node || !pending.label || !pending.title)
        clear_pending();
done:
    release(node_handle);
    release(label_handle);
}

/* All widget construction, mutation and disposal occurs in Application.Update. */
static Class *label_class, *panel_class, *font_class;
static Method *font_ctor;
static Method *label_ctor, *panel_ctor, *label_text, *measure_text;
static Method *measure_height, *measure_lines, *fit_height;
static bool (*native_disposed)(void *);
static Method *append_child, *remove_child, *dispose_label;
static Method *get_scene, *get_scene_root;
static Method *set_background, *set_radius, *set_max_lines;
static Method *set_x, *set_y, *set_width, *set_height;
static float (*native_x)(void *), (*native_y)(void *), (*native_width)(void *),
    (*native_height)(void *);
static float (*native_opacity)(void *), (*native_final_opacity)(void *);
static void *(*native_parent)(void *);
static bool (*native_shown)(void *), (*native_visible)(void *);
static void (*native_set_x)(void *, float), (*native_set_y)(void *, float);
static void (*native_set_opacity)(void *, float), (*native_set_visible)(void *, bool);
struct style_property {
    Method *get, *set;
};
static struct style_property styles[3];
struct tracked {
    uint32_t node, source, title, annotation, container, attach_parent, origin_root;
    uint32_t path[48];
    unsigned path_count;
    int index, rendered, measure_attempts, measure_ready;
    int fallback;
    int label_initialized, container_initialized;
    unsigned visibility_samples;
    int visibility_state;
    unsigned revision;
    float panel_width, panel_height;
    float pad_left, pad_right, pad_top, pad_bottom;
};
static struct tracked active;
static unsigned active_generation;
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
    /* The native scalar getter avoids allocating a boxed Boolean every frame. */
    return !obj || native_disposed(obj);
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
static int metrics_style(void *source, void *target) {
    /* All games use the title's native font configuration and one explicit size. */
    void *config = NULL;
    if (!call(styles[0].get, source, NULL, &config) || !config)
        return 0;
    uint32_t handle = retain(config);
    if (!handle)
        return 0;
    void *config_args[] = {config};
    int ok = call(styles[0].set, target, config_args, NULL);
    release(handle);
    /* A detached Label starts with an empty UIFont; provide a concrete native
     * font instead of relying on React's text-span/font initialization. */
    void *font = mono_object_new(mono_domain_get(), font_class);
    uint32_t font_handle = retain(font);
    if (!font_handle)
        return 0;
    int size = 24; /* Native played-time row is 30 px at 1.25 line height. */
    void *size_args[] = {&size}, *font_args[] = {mono_object_unbox(font)};
    ok = ok && call(font_ctor, mono_object_unbox(font), size_args, NULL) &&
         call(styles[1].set, target, font_args, NULL);
    release(font_handle);
    float white[4] = {1, 1, 1, 1};
    void *color_args[] = {white};
    return ok && call(styles[2].set, target, color_args, NULL);
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
        release(t->path[n]);
    t->path_count = 0;
    if (t->attach_parent)
        release(t->attach_parent);
    t->attach_parent = 0;
    release(t->origin_root);
    t->origin_root = 0;
}
static uint32_t screen_host(void *scene) {
    uint32_t handle = retain(scene), host = 0;
    for (unsigned depth = 0; handle && depth < 8; depth++) {
        scene = mono_gchandle_get_target(handle);
        Class *klass = mono_object_get_class(scene);
        const char *name = mono_class_get_name(klass);
        if (name && !strcmp(name, "AppScene")) {
            void *root = NULL;
            Method *getter = method_inherited(klass, "get_RootWidget", 0);
            if (getter && call(getter, scene, NULL, &root) && root && !is_disposed(root)) {
                name = mono_class_get_name(mono_object_get_class(root));
                if (name && !strstr(name, "React"))
                    host = retain(root);
            }
            break;
        }
        void *next = NULL;
        Method *getter = method_inherited(klass, "get_SuperScene", 0);
        uint32_t next_handle = getter && call(getter, scene, NULL, &next) && next &&
                               next != scene ? retain(next) : 0;
        release(handle);
        handle = next_handle;
    }
    release(handle);
    return host;
}
static void remove_annotation(struct tracked *t) {
    void *container = t->container ? mono_gchandle_get_target(t->container) : NULL;
    void *label = t->annotation ? mono_gchandle_get_target(t->annotation) : NULL;
    /* Detach and dispose each owned widget once, without depending on whether
     * a parent's Dispose recursively destroys its children. Never call native
     * widget methods on an object whose constructor failed. */
    if (t->annotation)
        log_event("dispose label=%p container=%p initialized=%d/%d\n", label, container, t->label_initialized, t->container_initialized);
    if (label && t->label_initialized && !is_disposed(label)) {
        (void)call(remove_child, label, NULL, NULL);
        (void)call(dispose_label, label, NULL, NULL);
    }
    if (container && t->container_initialized && !is_disposed(container)) {
        (void)call(remove_child, container, NULL, NULL);
        (void)call(dispose_label, container, NULL, NULL);
    }
    t->label_initialized = t->container_initialized = 0;
    if (t->container)
        log_event("dispose done\n");
    if (t->container)
        release(t->container);
    if (t->annotation)
        release(t->annotation);
    if (t->rendered > 0)
        panels_removed++;
    t->container = t->annotation = 0;
    t->rendered = 0;
    release_anchors(t);
}
/* Never append untagged widgets beneath a React-managed view: React's next
 * recursive child traversal expects every descendant to have a registered tag.
 * The native Scene root is a sibling host outside the React content subtree. */
static int capture_anchors(struct tracked *t, void *source) {
    release_anchors(t);
    void *scene = NULL, *root = NULL;
    if (!call(get_scene, source, NULL, &scene) || !scene)
        return 0;
    uint32_t scene_handle = retain(scene);
    if (!scene_handle)
        return 0;
    int ok = call(get_scene_root, scene, NULL, &root) && root && !is_disposed(root);
    uint32_t root_handle = ok ? retain(root) : 0;
    uint32_t host_handle = root_handle ? screen_host(scene) : 0;
    release(scene_handle);
    if (!root_handle)
        return 0;
    /* Fail closed if an unexpected runtime maps Scene.RootWidget into React. */
    const char *name = mono_class_get_name(mono_object_get_class(root));
    if (!name || strstr(name, "React")) {
        release(root_handle);
        release(host_handle);
        return 0;
    }
    void *parent = native_parent(source);
    for (unsigned depth = 0; parent && depth < 48; depth++) {
        if (parent == root) {
            t->attach_parent = host_handle ? host_handle : root_handle;
            t->origin_root = host_handle ? root_handle : 0;
            log_event("overlay root=%p class=%s path=%u fallback=%d\n", root, name, t->path_count, t->fallback);
            return 1;
        }
        if (is_disposed(parent))
            break;
        uint32_t handle = retain(parent);
        if (!handle)
            break;
        t->path[t->path_count++] = handle;
        parent = native_parent(parent);
    }
    release(root_handle);
    release(host_handle);
    release_anchors(t);
    return 0;
}
static int metrics_background(void *container) {
    /* Captured native played-time values; no managed objects from old trees. */
    float rgba[4] = {0, 0, 0, 0.701961f};
    void *args[] = {rgba};
    return call(set_background, container, args, NULL) &&
           set_float(set_radius, container, 15.0f);
}
static int position_container(struct tracked *t) {
    void *source = mono_gchandle_get_target(t->source);
    void *container = mono_gchandle_get_target(t->container);
    void *parent = t->attach_parent ? mono_gchandle_get_target(t->attach_parent) : NULL;
    void *origin = t->origin_root ? mono_gchandle_get_target(t->origin_root) : parent;
    if (!source || !container || !parent || is_disposed(container) || is_disposed(parent) ||
        is_disposed(origin))
        return 0;
    void *current = source, *control = NULL;
    for (unsigned n = 0; n < t->path_count; n++) {
        control = mono_gchandle_get_target(t->path[n]);
        if (!control || native_parent(current) != control || is_disposed(control))
            return 0;
        current = control;
    }
    if ((t->path_count && (!control || native_parent(control) != origin)) ||
        (!t->path_count && native_parent(source) != origin))
        return 0;
    /* Screen placement never depends on the game's focus-dependent transforms. */
    float right = native_width(parent) - 144.0f;
    float x = right - t->panel_width;
    if (x < 24.0f)
        x = 24.0f;
    float target_y = native_height(parent) - 162.0f - t->panel_height;
    if (target_y < 24.0f)
        target_y = 24.0f;
    if (native_x(container) != x)
        native_set_x(container, x);
    if (native_y(container) != target_y)
        native_set_y(container, target_y);
    /* Metrics belong to the selected game's scene, independently of whether
     * React reveals its Play Game / played-time controls for the current focus. */
    int visible = native_shown(parent);
    /* Diagnose focus transitions only on the owning UI thread, using retained
     * current-selection objects. Never inspect live scenes from a probe worker. */
    int visibility_state = visible;
    if (t->visibility_samples < 8 &&
        (!t->visibility_samples || t->visibility_state != visibility_state)) {
        t->visibility_state = visibility_state;
        t->visibility_samples++;
        log_event("overlay visibility index=%d fallback=%d root=%p shown=%d "
                  "fixed=1 root-size=%g,%g root-opacity=%g position=%g,%g\n",
                  t->index, t->fallback, parent, visible,
                  (double)native_width(parent), (double)native_height(parent),
                  (double)native_final_opacity(parent), (double)x, (double)target_y);
    }
    if (native_visible(container) != visible)
        native_set_visible(container, visible);
    if (native_opacity(container) != 1.0f)
        native_set_opacity(container, 1.0f);
    return 1;
}
static void clear_tracked(struct tracked *t) {
    if (t->source)
        log_event("clear begin index=%d panel=%u label=%u\n", t->index, t->container, t->annotation);
    remove_annotation(t);
    if (t->node)
        release(t->node);
    if (t->source)
        release(t->source);
    if (t->title)
        release(t->title);
    if (t->source)
        log_event("clear done index=%d handles=%u\n", t->index, live_handles);
    memset(t, 0, sizeof *t);
}
static void render(struct tracked *t) {
    void *node = mono_gchandle_get_target(t->node);
    void *source = mono_gchandle_get_target(t->source);
    if (!node || *(unsigned char *)((char *)node + disposed_off) || is_disposed(source) ||
        is_disposed(mono_gchandle_get_target(t->title))) {
        clear_tracked(t);
        return;
    }
    if (!atomic_load_explicit(&enabled, memory_order_acquire)) {
        remove_annotation(t);
        return;
    }
    unsigned revision = atomic_load_explicit(&cache[t->index].revision, memory_order_acquire);
    if (t->rendered > 0 && t->revision != revision)
        remove_annotation(t);
    if (t->rendered > 0) {
        if (is_disposed(mono_gchandle_get_target(t->annotation)) ||
            is_disposed(mono_gchandle_get_target(t->container))) {
            clear_tracked(t);
            return;
        }
        if (!t->measure_ready && t->measure_attempts++ < 60) {
            void *label = mono_gchandle_get_target(t->annotation);
            void *container = mono_gchandle_get_target(t->container);
            float width, height;
            void *line_count = NULL;
            if (label && container && metric_width(label, &width) && width > 0 && width <= 600 &&
                get_float(measure_height, label, &height) && height > 0 && height <= 480 &&
                call(measure_lines, label, NULL, &line_count) && line_count &&
                *(int *)mono_object_unbox(line_count) == 3) {
                t->panel_width = width + t->pad_left + t->pad_right;
                t->panel_height = height + t->pad_top + t->pad_bottom;
                if (set_float(set_width, label, width) && set_float(set_height, label, height) &&
                    set_float(set_width, container, t->panel_width) &&
                    set_float(set_height, container, t->panel_height)) {
                    t->measure_ready = 1;
                    log_event("metrics measured lines=3 text=%g,%g panel=%g,%g\n",
                              (double)width, (double)height, (double)t->panel_width,
                              (double)t->panel_height);
                }
            }
        }
        if (!position_container(t))
            remove_annotation(t);
        return;
    }
    if (t->rendered < 0 || atomic_load_explicit(&cache[t->index].state, memory_order_acquire) != 2)
        return;
    const float height = 48.0f * 3;
    t->pad_left = t->pad_right = 24.0f;
    t->pad_top = t->pad_bottom = 13.0f;
    void *label = mono_object_new(mono_domain_get(), label_class);
    if (!label)
        return;
    t->annotation = retain(label);
    if (!t->annotation)
        return;
    if (!call(label_ctor, label, NULL, NULL))
        goto fail;
    t->label_initialized = 1;
    char metrics[320];
    pthread_mutex_lock(&cache_mutex);
    snprintf(metrics, sizeof metrics, "%s", cache[t->index].label);
    pthread_mutex_unlock(&cache_mutex);
    void *text = mono_string_new(mono_domain_get(), metrics);
    uint32_t text_handle = text ? retain(text) : 0;
    void *text_args[] = {text};
    int max_lines = 3;
    void *line_args[] = {&max_lines};
    bool fit = true;
    void *fit_args[] = {&fit};
    int styled = metrics_style(mono_gchandle_get_target(t->title), label);
    int ok = text_handle && styled &&
             call(set_max_lines, label, line_args, NULL) &&
             call(fit_height, label, fit_args, NULL) &&
             call(label_text, label, text_args, NULL);
    if (text_handle)
        release(text_handle);
    if (!ok)
        goto fail;
    float width = 340, measured;
    t->measure_attempts = t->measure_ready = 0;
    if (metric_width(label, &measured) && measured > 0 && measured <= 600) {
        width = measured;
    }
    t->panel_width = width + t->pad_left + t->pad_right;
    t->panel_height = height + t->pad_top + t->pad_bottom;
    if (!capture_anchors(t, source))
        goto fail;
    void *container = mono_object_new(mono_domain_get(), panel_class);
    if (!container)
        goto fail;
    t->container = retain(container);
    if (!t->container || !call(panel_ctor, container, NULL, NULL))
        goto fail;
    t->container_initialized = 1;
    void *parent = mono_gchandle_get_target(t->attach_parent);
    void *label_args[] = {label}, *container_args[] = {container};
    if (!set_float(set_width, label, width) || !set_float(set_height, label, height) ||
        !set_float(set_x, label, t->pad_left) || !set_float(set_y, label, t->pad_top) ||
        !set_float(set_width, container, t->panel_width) ||
        !set_float(set_height, container, t->panel_height) ||
        !metrics_background(container) ||
        !call(append_child, container, label_args, NULL) || !position_container(t) ||
        !call(append_child, parent, container_args, NULL))
        goto fail;
    t->rendered = 1;
    t->revision = revision;
    panels_created++;
    void *played = invoke(label_display, source, NULL);
    char *played_utf8 = played ? mono_string_to_utf8(played) : NULL;
    log_event("rendered title=%s source=%p annotation=%p container=%p sibling-parent=%p played=%s "
              "completion=%s\n",
              games[t->index].title, source, label, container, parent,
              played_utf8 ? played_utf8 : "", metrics);
    if (played_utf8)
        mono_free(played_utf8);
    return;
fail:
    remove_annotation(t);
    t->rendered = -1;
    log_event("sibling annotation construction failed; waiting for native update\n");
}
static void update_hook(void *app) {
    uintptr_t thread = (uintptr_t)pthread_self(), unset = 0;
    atomic_compare_exchange_strong_explicit(&ui_thread, &unset, thread,
                                           memory_order_release, memory_order_relaxed);
    hltb_native_update_original(app);
    if (atomic_load_explicit(&ui_thread, memory_order_acquire) != thread)
        return;
    unsigned frame = (unsigned)atomic_fetch_add(&frames, 1);
    /* Disabling must not wait for a blocked HTTPS lookup on the worker. */
    if (frame % 6 == 0)
        atomic_store_explicit(&enabled, access("/system_tmp/hltb-native.enable", F_OK) == 0,
                              memory_order_release);
    if (!atomic_load_explicit(&enabled, memory_order_acquire)) {
        clear_pending();
        clear_tracked(&active);
        release(selected_root_handle);
        release(selected_title_handle);
        release(selected_title_node_handle);
        selected_root_handle = selected_title_handle = selected_title_node_handle = 0;
        selected_index = -1;
        atomic_store_explicit(&requested_index, -1, memory_order_release);
        return;
    }
    if (active.source && active_generation != selection_generation)
        clear_tracked(&active);
    if (pending.node) {
        struct event e = pending;
        memset(&pending, 0, sizeof pending);
        void *source = mono_gchandle_get_target(e.label);
        if (pending_generation == selection_generation && !is_disposed(source)) {
            if (!active.source || active.index != e.index || mono_gchandle_get_target(active.title) !=
                                             mono_gchandle_get_target(e.title)) {
                clear_tracked(&active);
                active = (struct tracked){
                    .node = e.node, .source = e.label, .title = e.title, .index = e.index};
                active_generation = selection_generation;
                memset(&e, 0, sizeof e); /* Ownership transferred, not duplicated. */
            } else if (active.rendered < 0)
                active.rendered = 0;
        }
        release(e.node);
        release(e.label);
        release(e.title);
    }
    if (!active.source && selected_index >= 0 &&
        frame - selected_frame >= 6 && selected_title_node_handle && selected_title_handle) {
        void *title = mono_gchandle_get_target(selected_title_handle);
        void *node = mono_gchandle_get_target(selected_title_node_handle);
        if (node && !*(unsigned char *)((char *)node + disposed_off) && !is_disposed(title)) {
            active = (struct tracked){.node = retain(node), .source = retain(title),
                                      .title = retain(title), .index = selected_index,
                                      .fallback = 1};
            active_generation = selection_generation;
            if (!active.node || !active.source || !active.title)
                clear_tracked(&active);
        }
    }
    if (active.source)
        render(&active);
    if (frame % 600 == 0)
        log_event("runtime frame=%u switches=%u panels=%u/%u handles=%u peak=%u coalesced=%u "
                  "active=%d pending=%d\n", frame, switches, panels_created, panels_removed,
                  live_handles, peak_handles, events_coalesced, active.source != 0,
                  pending.node != 0);
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
    METHOD(append_child, "AppendChild", 1)
    METHOD(remove_child, "RemoveFromParent", 0)
    METHOD(dispose_label, "Dispose", 0)
    METHOD(get_scene, "get_ParentScene", 0)
    METHOD(set_x, "set_X", 1)
    METHOD(set_y, "set_Y", 1)
    METHOD(set_width, "set_Width", 1)
    METHOD(set_height, "set_Height", 1)
    METHOD(measure_text, "GetLongestLineWidth", 1)
    METHOD(measure_height, "GetTextHeight", 0)
    METHOD(measure_lines, "GetLineCount", 0)
    METHOD(fit_height, "set_FitHeightToText", 1)
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
    NATIVE(native_disposed, "get_IsDisposed", 0);
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
    font_class = mono_class_from_name(image, "Sce.PlayStation.PUI.UI3", "UIFont");
    font_ctor = font_class ? mono_class_get_method_from_name(font_class, ".ctor", 1) : NULL;
    if (!font_ctor)
        return 0;
    panel_class = mono_class_from_name(image, "Sce.PlayStation.PUI.UI3", "Panel");
    if (!panel_class || !(panel_ctor = mono_class_get_method_from_name(panel_class, ".ctor", 0)))
        return 0;
    Class *scene_class = mono_class_from_name(image, "Sce.PlayStation.PUI.UI3", "Scene");
    get_scene_root = scene_class ? method_inherited(scene_class, "get_RootWidget", 0) : NULL;
    if (!get_scene_root)
        return 0;
    set_background = method_inherited(panel_class, "set_BackgroundColor", 1);
    set_radius = method_inherited(panel_class, "set_BorderRadius", 1);
    if (!set_background || !set_radius)
        return 0;
    const char *names[] = {"FontConfig", "Font", "TextColor"};
    for (unsigned n = 0; n < 3; n++) {
        char name[80];
        snprintf(name, sizeof name, "get_%s", names[n]);
        styles[n].get = n == 0 ? method_inherited(label_class, name, 0) : NULL;
        snprintf(name, sizeof name, "set_%s", names[n]);
        styles[n].set = method_inherited(label_class, name, 1);
        if ((n == 0 && !styles[n].get) || !styles[n].set)
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
#ifndef HLTB_RUNTIME_TEST
__attribute__((visibility("default"), noreturn)) void *elf_main(void *args) {
    (void)args;

    logfile = fopen(HLTB_NATIVE_LOG, "w");
    if (logfile) {
        log_event("entered pid=%d\n", getpid());
    }
    domain = mono_get_root_domain();
    void *worker_thread = NULL;
    if (!domain)
        goto parked;
    worker_thread = mono_thread_attach(domain);
    if (!worker_thread)
        goto parked;
    game_count = hltb_catalog_read("/system_tmp/hltb-catalog.bin", games, HLTB_CATALOG_MAX);
    if (!game_count)
        game_count = hltb_catalog_load(games, HLTB_CATALOG_MAX);
    Assembly *a = mono_domain_assembly_open(domain, "/system_ex/common_ex/lib/ReactNative.PUI.dll");
    Image *i = a ? mono_assembly_get_image(a) : NULL;
    Class *raw =
        i ? mono_class_from_name(i, "ReactNative.Views.UI3.View", "ReactTextShadowNode") : NULL;
    Class *base = i ? mono_class_from_name(i, "ReactNative.UIManager", "ReactShadowNode") : NULL;
    if (!raw || !base)
        goto parked;
    /* Use a plain PUI panel: this widget has no React shadow-node owner. */
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
    /* Initialization needs Mono, networking does not. Do not leave a native
     * worker attached to the managed runtime during blocking socket calls. */
    mono_thread_detach(worker_thread);
    worker_thread = NULL;
    int last_request = -1;
    unsigned stable_ticks = 0;
    for (;;) {
        int index = atomic_load_explicit(&requested_index, memory_order_acquire);
        if (index != last_request) {
            last_request = index;
            stable_ticks = 0;
        }
        /* Coalesce rapid navigation; one worker and at most one lookup in flight.
         * An in-flight result may fill its cache, but never touches UI objects. */
        if (index >= 0 && (size_t)index < game_count && ++stable_ticks >= 3 &&
            (atomic_load_explicit(&cache[index].state, memory_order_acquire) == 0 ||
             (atomic_load_explicit(&cache[index].state, memory_order_acquire) == 2 &&
              time(NULL) >= atomic_load_explicit(&cache[index].expires, memory_order_acquire)))) {
            atomic_store_explicit(&cache[index].state, 1, memory_order_release);
            hltb_result result = {0};
            time_t expires = 0;
            int cached = !hltb_cache_read_until(games[index].title, &result, &expires);
            int rc = cached ? 0 : hltb_lookup(games[index].title, &result);
            char metrics[320];
            if (!rc && !hltb_format_metrics(&result, metrics, sizeof metrics)) {
                if (!cached) {
                    int saved = hltb_cache_write(games[index].title, &result);
                    expires = time(NULL) + HLTB_CACHE_TTL;
                    log_event("cache write title=%s status=%d\n", games[index].title, saved);
                }
                pthread_mutex_lock(&cache_mutex);
                snprintf(cache[index].label, sizeof cache[index].label, "%s", metrics);
                pthread_mutex_unlock(&cache_mutex);
                atomic_store_explicit(&cache[index].expires, expires, memory_order_release);
                atomic_fetch_add_explicit(&cache[index].revision, 1, memory_order_release);
                atomic_store_explicit(&cache[index].state, 2, memory_order_release);
            } else
                atomic_store_explicit(&cache[index].state, 3, memory_order_release);
            log_event("cache source=%s title=%s\n", cached ? "file" : "network", games[index].title);
            log_event("lookup title=%s status=%d seconds=%u frames=%d\n", games[index].title,
                      rc, result.main_seconds, atomic_load(&frames));
        }
        usleep(100000);
    }

parked:
    if (worker_thread)
        mono_thread_detach(worker_thread);
    if (logfile) {
        log_event("initialization failed; parked\n");
    }
    for (;;)
        sleep(60);
}

#endif
