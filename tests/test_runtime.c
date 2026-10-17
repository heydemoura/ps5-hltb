/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Exercise the real renderer hooks with a deterministic managed-widget adapter.
 * This verifies ownership and bounded resources, not Sony's native renderer. */
#define HLTB_RUNTIME_TEST
#define access runtime_access
#include "../src/playtime_native.c"
#undef access
#include <assert.h>
#include <stdlib.h>

struct widget {
    struct widget *parent, *child;
    float x, y, w, h, opacity;
    bool disposed, visible, react_managed;
    const char *text;
    char *owned_text;
    struct widget *super_scene;
    struct widget *scene_root;
    bool app_scene;
};
static void *handles[256];
static unsigned handle_count, object_count, disposed_count;
static int fail_handle_after = -1;
static uintptr_t fail_method;
static unsigned fail_method_after;
static bool on = true;
static struct widget *allocated[10000];
enum { DISPLAY=1, CTOR, TEXT, APPEND, REMOVE, DISPOSE, HEIGHT, WIDTH, X, Y,
       GET_BACKGROUND, SET_BACKGROUND, GET_RADIUS, SET_RADIUS, SET_HEIGHT, MAX_LINES, STYLE_GET, STYLE_SET,
       MEASURE, GET_SCENE, GET_SCENE_ROOT, FONT_CTOR, MEASURE_HEIGHT, MEASURE_LINES,
       SCENE_VISIBLE, SUPER_SCENE };
static unsigned scene_visits;
static struct widget *mock_scene_root;
#define M(n) ((Method *)(uintptr_t)(n))
int runtime_access(const char *p, int mode) { (void)p; (void)mode; return on ? 0 : -1; }
uint32_t mono_gchandle_new(void *obj, int pinned) {
    assert(atomic_load(&ui_thread) == (uintptr_t)pthread_self());
    assert(pinned && obj);
    if (fail_handle_after == 0) return 0;
    if (fail_handle_after > 0) fail_handle_after--;
    for (unsigned i=1; i<256; i++) if (!handles[i]) {
        handles[i]=obj; handle_count++; return i;
    }
    assert(!"unbounded handle growth"); return 0;
}
void *mono_gchandle_get_target(uint32_t h) { assert(h && handles[h]); return handles[h]; }
void mono_gchandle_free(uint32_t h) { assert(h && handles[h]); handles[h]=NULL; handle_count--; }
char *mono_string_to_utf8(void *s) { return strdup(s); }
void mono_free(void *p) { free(p); }
void *mono_string_new(Domain *d, const char *s) { (void)d; return (void *)s; }
Domain *mono_domain_get(void) { return (void *)1; }
Class *mono_object_get_class(void *p) { return p; }
Class *mono_class_get_parent(Class *c) { return c==(void *)1 ? NULL : (void *)1; }
Method *mono_class_get_method_from_name(Class *c, const char *name, int count) {
    (void)c; assert(count==0);
    if (!strcmp(name,"get_SuperScene")) return M(SUPER_SCENE);
    if (!strcmp(name,"get_RootWidget")) return M(GET_SCENE_ROOT);
    if (!strcmp(name,"get_ActualVisible") || !strcmp(name,"get_IsFocusableVisibility") ||
        !strcmp(name,"get_ScreenRenderOverlay") || !strcmp(name,"get_ScreenFixedTransform"))
        return M(SCENE_VISIBLE);
    return NULL;
}
const char *mono_class_get_name(Class *c) {
    return ((struct widget *)c)->app_scene ? "AppScene" : "mock";
}
void *mono_object_unbox(void *p) { return p; }
void *mono_object_new(Domain *d, Class *c) {
    assert(atomic_load(&ui_thread) == (uintptr_t)pthread_self());
    (void)d;
    static char boxed_font[64];
    if(c==font_class) return boxed_font;
    assert(object_count < 10000);
    struct widget *w=calloc(1,sizeof *w); assert(w);
    w->visible=true; w->opacity=1;
    allocated[object_count++]=w; return w;
}
void *mono_runtime_invoke(Method *m, void *obj, void **args, void **exception) {
    assert(atomic_load(&ui_thread) == (uintptr_t)pthread_self());
    struct widget *w=obj;
    static float value, rgba[4]={0,0,0,0.7f};
    *exception=NULL;
    if ((uintptr_t)m == fail_method) {
        if (!fail_method_after--) {
            fail_method=0;
            *exception=(void *)1;
            return NULL;
        }
    }
    switch ((uintptr_t)m) {
    case SCENE_VISIBLE: return &w->visible;
    case SUPER_SCENE: scene_visits++; return w->super_scene;
    case MEASURE_HEIGHT: value=108; return &value;
    case MEASURE_LINES: { static int count=3; return &count; }
    case FONT_CTOR: assert(*(int *)args[0]==24); return NULL;
    case GET_SCENE: return mock_scene_root;
    case GET_SCENE_ROOT: return w->scene_root ? w->scene_root : mock_scene_root;
    case DISPLAY: return (void *)w->text;
    case CTOR: return NULL;
    case TEXT: free(w->owned_text); w->owned_text=strdup(args[0]); w->text=w->owned_text; return NULL;
    case APPEND: assert(!w->react_managed); w->child=args[0]; w->child->parent=w; return NULL;
    case REMOVE: if(w->parent && w->parent->child==w) w->parent->child=NULL; w->parent=NULL; return NULL;
    case DISPOSE:
        assert(!w->disposed); w->disposed=true; disposed_count++;
        free(w->owned_text); w->owned_text=NULL;
        if(w->child && !w->child->disposed) mono_runtime_invoke(M(DISPOSE),w->child,NULL,exception);
        return NULL;
    case HEIGHT: value=w->h; return &value;
    case SET_HEIGHT: w->h=*(float *)args[0]; return NULL;
    case WIDTH: w->w=*(float *)args[0]; return NULL;
    case X: w->x=*(float *)args[0]; return NULL;
    case Y: w->y=*(float *)args[0]; return NULL;
    case GET_BACKGROUND: return rgba;
    case GET_RADIUS: value=15; return &value;
    case MEASURE: value=300; return &value;
    case STYLE_GET: return rgba;
    default: return NULL;
    }
}
void hltb_native_text_original(void *n, void *l) { (void)n; (void)l; }
void hltb_native_update_original(void *a) { (void)a; }
static bool disposed(void *p) { return ((struct widget *)p)->disposed; }
static float wx(void *p) { return ((struct widget *)p)->x; }
static float wy(void *p) { return ((struct widget *)p)->y; }
static float ww(void *p) { return ((struct widget *)p)->w; }
static float wh(void *p) { return ((struct widget *)p)->h; }
static float opacity(void *p) { return ((struct widget *)p)->opacity; }
static void *parent(void *p) { return ((struct widget *)p)->parent; }
static bool visible(void *p) { return ((struct widget *)p)->visible; }
static void sx(void *p,float f) { ((struct widget *)p)->x=f; }
static void sy(void *p,float f) { ((struct widget *)p)->y=f; }
static void so(void *p,float f) { ((struct widget *)p)->opacity=f; }
static void sv(void *p,bool b) { ((struct widget *)p)->visible=b; }
static void put(void *p,int off,void *v) { memcpy((char *)p+off,&v,sizeof v); }
static void *foreign(void *arg) {
    (void)arg;
    /* Invalid managed arguments must never be inspected on another thread. */
    text_hook(NULL,NULL); update_hook(NULL); return NULL;
}
int main(void) {
    native_disposed=disposed; native_x=wx; native_y=wy; native_width=ww; native_height=wh;
    native_parent=parent; native_shown=native_visible=visible;
    native_opacity=native_final_opacity=opacity;
    native_set_x=sx; native_set_y=sy; native_set_opacity=so; native_set_visible=sv;
    font_class=(void *)2; font_ctor=M(FONT_CTOR);
    label_display=M(DISPLAY); label_ctor=panel_ctor=M(CTOR); label_text=M(TEXT);
    get_scene=M(GET_SCENE); get_scene_root=M(GET_SCENE_ROOT);
    append_child=M(APPEND); remove_child=M(REMOVE); dispose_label=M(DISPOSE);
    set_height=M(SET_HEIGHT); set_width=M(WIDTH); set_x=M(X); set_y=M(Y);
    set_background=M(SET_BACKGROUND);
    set_radius=M(SET_RADIUS); set_max_lines=M(MAX_LINES);
    measure_text=M(MEASURE); measure_height=M(MEASURE_HEIGHT); measure_lines=M(MEASURE_LINES); fit_height=M(MAX_LINES);
    for (int i=0;i<3;i++) styles[i]=(struct style_property){M(STYLE_GET),M(STYLE_SET)};
    parent_off=40; root_off=16; children_off=32; disposed_off=97; span_off=152; test_off=184;
    span_id_off=16; span_inner_off=72; span_outer_off=80;
    game_count=2; strcpy(games[0].title,"Game A"); strcpy(games[1].title,"Game B");
    for (int i=0;i<2;i++) { atomic_store(&cache[i].state,2); atomic_store(&cache[i].revision,1);
        strcpy(cache[i].label,"Main Story: 1h 00m\nMain + Extra: 2h 00m\nCompletionist: 3h 00m"); }
    unsigned char title_node[208]={0}, node[208]={0}, row_node[208]={0}, icon[208]={0}, tip[208]={0};
    unsigned char span[96]={0}, list[40]={0}, array[56]={0};
    int root;
    put(title_node,16,&root); put(title_node,184,"post-cover-concept-name");
    put(node,16,&root); put(node,40,row_node); put(node,88,"RCTText");
    put(row_node,32,list); put(list,16,array); *(int *)(list+32)=3;
    put(array,32,icon); put(array,40,tip); put(array,48,node);
    put(icon,88,"RCTImageView"); put(tip,88,"RCTText"); put(tip,152,span);
    put(span,16,"msgid_playtime_indicates_overall_time");
    struct widget common={.visible=true,.opacity=1,.w=1920,.h=1080};
    mock_scene_root=&common;
    struct widget react_root={.parent=&common,.react_managed=true,.visible=true,.opacity=1,.w=1920,.h=1080};
    struct widget row={.parent=&react_root,.react_managed=true,.w=97,.h=56,.visible=true,.opacity=1};
    struct widget source={.parent=&row,.x=50,.y=13,.w=32,.h=30,.visible=true,.opacity=1,.text="9m"};
    struct widget title={.parent=&react_root,.visible=true,.opacity=1};
    update_hook(NULL);
    /* Scene observations remain bounded even with deep or cyclic ancestry. */
    struct widget scenes[12]={0};
    for(unsigned i=0;i<11;i++) scenes[i].super_scene=&scenes[i+1];
    unsigned visits=scene_visits;
    assert(!screen_host(scenes));
    assert(scene_visits-visits==8 && !live_handles && !handle_count);
    scenes[1].super_scene=scenes;
    visits=scene_visits;
    assert(!screen_host(scenes));
    assert(scene_visits-visits==8 && !live_handles && !handle_count);
    scenes[0].super_scene=scenes;
    visits=scene_visits;
    assert(!screen_host(scenes));
    assert(scene_visits-visits==1 && !live_handles && !handle_count);
    fail_method=SUPER_SCENE;
    assert(!screen_host(scenes));
    assert(!fail_method && !live_handles && !handle_count);
    for (unsigned i=0;i<2000;i++) {
        title.text=games[i%2].title;
        text_hook(title_node,&title); text_hook(node,&source);
        for(int j=0;j<50;j++) text_hook(node,&source);
        update_hook(NULL);
        assert(active.source && active.index==(int)(i%2) && active.rendered==1);
        assert(handle_count==live_handles && live_handles==11);
        unsigned made=panels_created;
        text_hook(node,&source); update_hook(NULL);
        assert(panels_created==made);
        assert(active.panel_height==134); /* Identical refresh retains the existing panel. */
        if(i%100==0) { pthread_t t; assert(!pthread_create(&t,NULL,foreign,NULL)); pthread_join(t,NULL); }
    }
    assert(panels_created==2000 && panels_removed==1999 && events_coalesced==100000);
    assert(peak_handles<64 && object_count==4000);
    /* A new title with no played-time event must immediately release the old tree. */
    title.text="Game A"; text_hook(title_node,&title); update_hook(NULL);
    assert(!active.source && live_handles==3);
    /* Allocation failures cannot leave partially retained mailbox entries. */
    for(int fail=0;fail<5;fail++) {
        fail_handle_after=fail; text_hook(node,&source); fail_handle_after=-1;
        clear_pending(); assert(live_handles==3);
    }
    text_hook(node,&source); update_hook(NULL);
    on=false;
    for(int i=0;i<6;i++) update_hook(NULL);
    assert(!active.source && !pending.node && !handle_count && !live_handles);
    assert(panels_created==panels_removed);
    /* Cold start on an unplayed game needs no previous played-time style/tree. */
    on=true; for(int i=0;i<6;i++) update_hook(NULL);
    title.text="Game B"; title.visible=false; title.opacity=0; text_hook(title_node,&title);
    for(int i=0;i<8;i++) update_hook(NULL);
    assert(active.source && active.fallback && active.rendered==1);
    struct widget *panel=mono_gchandle_get_target(active.container);
    assert(panel->parent==mock_scene_root && panel->x>1000 && panel->y>500);
    assert(panel->visible && panel->opacity==1);
    unsigned saved_handles=live_handles, saved_objects=object_count;
    for(int i=0;i<30;i++) {
        common.visible=i%2;
        update_hook(NULL);
        assert(panel->visible==common.visible);
        assert(live_handles==saved_handles && object_count==saved_objects);
    }
    assert(active.visibility_samples==8);
    common.visible=true;
    update_hook(NULL);
    /* A fixed screen host stays visible when the selected game's scene fades. */
    struct widget viewport={.visible=true,.opacity=1,.w=1920,.h=1080};
    struct widget app_scene={.app_scene=true,.scene_root=&viewport};
    common.super_scene=&app_scene;
    clear_tracked(&active);
    update_hook(NULL);
    panel=mono_gchandle_get_target(active.container);
    assert(panel->parent==&viewport && active.origin_root);
    common.visible=false; common.opacity=0;
    update_hook(NULL);
    assert(panel->visible && panel->opacity==1 && panel->parent==&viewport);
    assert(panel->x>1000 && panel->y>500);
    /* Revealing played time keeps the same metrics component and native widgets. */
    unsigned made=panels_created;
    uint32_t panel_handle=active.container;
    source.visible=false; source.opacity=0;
    text_hook(node,&source); update_hook(NULL);
    assert(active.source && active.fallback && active.rendered==1);
    assert(active.container==panel_handle && panels_created==made);
    assert(((struct widget *)mono_gchandle_get_target(active.container))->parent==&viewport);
    assert(((struct widget *)mono_gchandle_get_target(active.container))->visible);
    on=false; for(int i=0;i<6;i++) update_hook(NULL);
    assert(!handle_count && !live_handles && panels_created==panels_removed);
    common.super_scene=NULL; common.visible=true; common.opacity=1;
    assert(!viewport.child);
    /* Managed failures must unwind retained styles, anchors, and owned widgets,
     * including a panel attached to its label but not yet to the scene root. */
    const uintptr_t failures[]={STYLE_GET, STYLE_SET, TEXT, GET_SCENE,
        GET_SCENE_ROOT, SET_HEIGHT, WIDTH, SET_BACKGROUND, APPEND, APPEND};
    for (unsigned i=0;i<sizeof failures/sizeof failures[0];i++) {
        on=true;
        for(int j=0;j<6;j++) update_hook(NULL);
        title.text=games[i%2].title;
        source.visible=true; source.opacity=1;
        text_hook(title_node,&title); text_hook(node,&source);
        fail_method=failures[i]; fail_method_after=i==9 ? 1 : 0;
        update_hook(NULL);
        assert(!fail_method); /* Each injected failure was actually exercised. */
        on=false;
        for(int j=0;j<6;j++) update_hook(NULL);
        assert(!handle_count && !live_handles && !common.child);
        assert(panels_created==panels_removed);
    }
    for(unsigned i=0;i<object_count;i++) { assert(allocated[i]->disposed); free(allocated[i]); }
    printf("2000 game switches, 100000 coalesced events, foreign-thread rejection and cleanup passed (peak handles=%u)\n",peak_handles);
}
