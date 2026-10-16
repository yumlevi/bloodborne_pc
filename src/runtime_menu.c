/* Menu probe, the first step of mouse support in the menus. FromSoftware's menus are C++
 * dialogs and widgets (FrpgMenuDlg*) over Scaleform movies without mouse handlers, so the mouse
 * has to find their items through those objects. With BB_MENU_PROBE=1, F8 writes
 * logs/menu-probe-*.txt (BB_LOG_DIR: another folder): every live object of a menu class, its
 * first bytes with what they point at, and what points at it; then what those bytes point at,
 * two levels down (Bloodborne's dialogs are one unnamed class held by InGameMenuStep; most
 * FrpgMenuDlg* classes have no instances). A count of every named class comes first.
 *
 * The engine's runtime classes (DLRF) name the objects: a class's vtable starts with its
 * GetRuntimeClass, which loads the class from a slot ("mov rax,[rip+slot]; test rax,rax")
 * within its first bytes, and the runtime class holds the name at +0x40. Guest memory is read
 * through the backing view (runtime_memory_views): no GPU page protection, no faults. The probe
 * runs on the game thread reading the pad, so the menus stand still meanwhile. F8 instead
 * records the next seconds of the game's calls into Scaleform (see "Trace" below). Offsets are
 * Bloodborne v1.09 image offsets. */
#define _GNU_SOURCE
#include "runtime.h"
#include "gpu/bbgpu.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#define TEXT_END 0x50D96DCu       /* code and read-only data (class names) */
#define DATA_START 0x50DC000u     /* data and bss */
#define DATA_END 0x56D30F4u
#define VTABLES_START 0x5200000u  /* relocated data holding the vtables */
#define VTABLES_END 0x53E4000u
#define CHECK_NAME 0x493071Au     /* "FrpgMenuDlgCursorGroup" in the v1.09 image */
#define NAME_FIELD 0x40           /* runtime class: const char *name */
#define GETTER_BYTES 0x40         /* the slot load is this close to the getter's start */
#define OBJECT_BYTES 0x480       /* dumped of an object (the 0x440-byte dialogs fit) */
#define DATA_BYTES 0x100         /* dumped of plain data a dumped object points at */
#define MAX_DUMPED 4000
#define FOLLOW_DEPTH 2
#define MAX_FOLLOWED 1500
#define MAX_REFERRERS 8
#define NEAR_OBJECT 0x400         /* a pointer this far into a named object is labelled with it */
#define MAX_VIEWS 65536
#define VT_CACHE (1u << 18)       /* covers every 8-byte value of the vtable range */

typedef struct { uintptr_t vt; const char *name; int resolved; } VtEntry;
typedef struct { uintptr_t address; const char *name; } Object;
typedef struct { size_t object; int count; uintptr_t referrers[MAX_REFERRERS]; } Menu;
typedef struct { uintptr_t address, parent; uint32_t offset; int depth; } Followed;

static unsigned char *image;
static int enabled;
static unsigned probes;
static VtEntry *vt_cache;
static size_t vt_cached;
static RuntimeView *views;
static size_t view_count;
static Object *objects;
static size_t object_count, object_capacity;
static Menu *menus;
static size_t menu_count;
static FILE *probe_file;     /* open while a probe writes it (closed after a fault) */
/* Result lines stand out in a console that shows colours: a terminal, or the Windows console
 * run_game.py enabled them in (BB_CONSOLE_COLOR=1). */
static const char *on="", *off="";
static uint32_t *menu_index; /* open addressing: 1 + index into menus by address, 0 free */
static size_t menu_index_mask;
static Followed followed[MAX_FOLLOWED];
static size_t followed_count;
static uintptr_t *dumped;    /* open addressing: addresses already dumped or queued, 0 free */
static size_t dumped_mask;

static const char *log_dir(void) {
    const char *dir=getenv("BB_LOG_DIR");
    return dir && dir[0] ? dir : "logs";
}
/* [p, p+bytes) lies within image offsets [lo, hi). Offsets, not sums: guest data holds values
 * such as ~0, which p+bytes would wrap past the check. */
static int in_image(uintptr_t p, uint64_t lo, uint64_t hi, uint64_t bytes) {
    const uint64_t offset=p-(uintptr_t)image; /* p below the image wraps far above hi */
    return offset>=lo && offset<=hi && hi-offset>=bytes;
}
/* The class name a vtable's GetRuntimeClass leads to; NULL for other data. */
static const char *resolve(uintptr_t vt) {
    uintptr_t getter, slot, runtime_class, name;
    if ((vt & 7) || !in_image(vt,VTABLES_START,VTABLES_END,8)) return NULL;
    memcpy(&getter,(const void *)vt,8);
    if (!in_image(getter,0,TEXT_END,GETTER_BYTES)) return NULL;
    const unsigned char *g=(const unsigned char *)getter;
    for (int i=0;i+10<=GETTER_BYTES;++i) {
        if (g[i]!=0x48 || g[i+1]!=0x8B || g[i+2]!=0x05 || g[i+7]!=0x48 || g[i+8]!=0x85 || g[i+9]!=0xC0) continue;
        int32_t rel;
        memcpy(&rel,g+i+3,4);
        slot=getter+(uintptr_t)i+7+(uintptr_t)(intptr_t)rel;
        if (!in_image(slot,DATA_START,DATA_END,8)) return NULL;
        memcpy(&runtime_class,(const void *)slot,8);
        if (!in_image(runtime_class,DATA_START,DATA_END,NAME_FIELD+8)) return NULL;
        memcpy(&name,(const void *)(runtime_class+NAME_FIELD),8);
        if (!in_image(name,0,TEXT_END,1)) return NULL;
        const char *s=(const char *)name;
        size_t n=0, room=(uintptr_t)image+TEXT_END-name;
        while (n<room && n<96 && s[n]>=0x20 && s[n]<0x7f) ++n;
        return n>=3 && n<room && !s[n] ? s : NULL;
    }
    return NULL;
}
static const char *class_name(uintptr_t vt) {
    if (vt_cached>=VT_CACHE/4*3) return resolve(vt);
    for (size_t h=(size_t)((vt>>3)*UINT64_C(0x9E3779B97F4A7C15)>>46);;h=(h+1)&(VT_CACHE-1)) {
        if (vt_cache[h].resolved && vt_cache[h].vt==vt) return vt_cache[h].name;
        if (!vt_cache[h].resolved) {
            ++vt_cached;
            vt_cache[h]=(VtEntry){vt,resolve(vt),1};
            return vt_cache[h].name;
        }
    }
}
static int menu_class(const char *name) {
    if (!strncmp(name,"FD4Debug",8) || !strncmp(name,"Dbg",3)) return 0;
    return strstr(name,"Menu") || strstr(name,"Dlg") || strstr(name,"Dialog") || strstr(name,"Scaleform") ||
           strstr(name,"TitleFlow");
}
/* The view among sorted views holding a guest address, or NULL. */
static const RuntimeView *view_in(const RuntimeView *list, size_t count, uintptr_t address) {
    size_t lo=0, hi=count;
    while (lo<hi) {
        size_t mid=(lo+hi)/2;
        if (list[mid].start+list[mid].size<=address) lo=mid+1; else hi=mid;
    }
    return lo<count && list[lo].start<=address ? &list[lo] : NULL;
}
static const RuntimeView *view_of(uintptr_t address) { return view_in(views,view_count,address); }
/* The bytes at a guest address (backing view or the image's data) and how many follow. */
static const unsigned char *bytes_at(uintptr_t address, uint64_t *room) {
    if (in_image(address,DATA_START,DATA_END,0)) {
        *room=(uintptr_t)image+DATA_END-address;
        return (const unsigned char *)address;
    }
    const RuntimeView *v=view_of(address);
    *room=v ? v->start+v->size-address : 0;
    return v ? v->data+(address-v->start) : NULL;
}
/* The last named object starting at or before an address. */
static const Object *object_at(uintptr_t address) {
    size_t lo=0, hi=object_count;
    while (lo<hi) {
        size_t mid=(lo+hi)/2;
        if (objects[mid].address<=address) lo=mid+1; else hi=mid;
    }
    return lo ? &objects[lo-1] : NULL;
}
static int by_address(const void *a, const void *b) {
    uintptr_t x=((const Object *)a)->address, y=((const Object *)b)->address;
    return x<y ? -1 : x>y;
}
static int by_name(const void *a, const void *b) {
    return strcmp(*(const char *const *)a,*(const char *const *)b);
}
static int by_class(const void *a, const void *b) {
    const Object *x=&objects[((const Menu *)a)->object], *y=&objects[((const Menu *)b)->object];
    int c=strcmp(x->name,y->name);
    return c ? c : x->address<y->address ? -1 : x->address>y->address;
}
static void add_object(uintptr_t address, const char *name) {
    if (object_count==object_capacity) {
        size_t capacity=object_capacity ? object_capacity*2 : 65536;
        Object *next=realloc(objects,capacity*sizeof(*objects));
        if (!next) return;
        objects=next; object_capacity=capacity;
    }
    objects[object_count++]=(Object){address,name};
}
/* Every qword of guest memory, then of the image's data (image addresses are guest addresses). */
typedef void (*QwordVisitor)(uintptr_t at, uint64_t value);
static void each_qword(QwordVisitor visit, int with_image) {
    for (size_t v=0;v<view_count;++v) {
        const uint64_t *q=(const uint64_t *)views[v].data;
        for (uint64_t i=0, n=views[v].size/8;i<n;++i) visit(views[v].start+i*8,q[i]);
    }
    if (with_image)
        for (uint64_t off=DATA_START;off+8<=DATA_END;off+=8) visit((uintptr_t)image+off,*(const uint64_t *)(image+off));
}
static void find_object(uintptr_t at, uint64_t value) {
    if (value-((uintptr_t)image+VTABLES_START)>=VTABLES_END-VTABLES_START) return;
    const char *name=class_name((uintptr_t)value);
    if (name) add_object(at,name);
}
static uintptr_t menu_lo, menu_hi;
static size_t address_hash(uint64_t address) { return (size_t)((address>>3)*UINT64_C(0x9E3779B97F4A7C15)>>32); }
static void find_referrer(uintptr_t at, uint64_t value) {
    if (value<menu_lo || value>menu_hi || (value & 7)) return;
    for (size_t h=address_hash(value)&menu_index_mask;menu_index[h];h=(h+1)&menu_index_mask) {
        Menu *m=&menus[menu_index[h]-1];
        if (objects[m->object].address!=value) continue;
        if (m->count<MAX_REFERRERS) m->referrers[m->count++]=at;
        return;
    }
}
static void print_address(FILE *f, uintptr_t x) {
    if (in_image(x,0,DATA_END,0)) {
        fprintf(f,"image+%" PRIxPTR,x-(uintptr_t)image);
        return;
    }
    const Object *o=object_at(x);
    if (o && x-o->address<NEAR_OBJECT) {
        fprintf(f,"%s@%" PRIxPTR,o->name,o->address);
        if (x!=o->address) fprintf(f,"+%" PRIxPTR,x-o->address);
        return;
    }
    fprintf(f,"%" PRIxPTR,x);
}
/* What a qword could be: a pointer to something known, a string, or two floats. */
static void describe(FILE *f, uint64_t x) {
    uintptr_t p=(uintptr_t)x;
    if (in_image(p,VTABLES_START,VTABLES_END,8)) {
        const char *name=class_name(p);
        if (name) { fprintf(f,"  vtable %s",name); return; }
    }
    if (in_image(p,0,TEXT_END,1)) {
        const char *s=(const char *)p;
        size_t n=0, room=(uintptr_t)image+TEXT_END-p;
        while (n<room && n<48 && s[n]>=0x20 && s[n]<0x7f) ++n;
        fprintf(f,"  image+%" PRIxPTR,p-(uintptr_t)image);
        if (n>=4 && n<room && !s[n]) fprintf(f," \"%s\"",s);
        return;
    }
    uint64_t room;
    const unsigned char *target=bytes_at(p,&room);
    if (target) {
        fputs(in_image(p,DATA_START,DATA_END,0) ? "  " : "  -> ",f);
        print_address(f,p);
        const Object *o=object_at(p);
        if ((!o || p-o->address>=NEAR_OBJECT) && !(p&7) && room>=8) {
            uint64_t first;
            memcpy(&first,target,8);
            if (in_image((uintptr_t)first,VTABLES_START,VTABLES_END,8)) fprintf(f," (vt image+%" PRIxPTR ")",(uintptr_t)first-(uintptr_t)image);
        }
        return;
    }
    float halves[2];
    memcpy(halves,&x,8);
    int plausible=1;
    for (int i=0;i<2;++i) {
        float a=halves[i]<0 ? -halves[i] : halves[i];
        if (!(a==0.0f || (a>=1e-3f && a<=1e5f))) plausible=0;
    }
    if (plausible) fprintf(f,"  (%g, %g)",halves[0],halves[1]);
}
/* Adds an address to the dumped set; 0 when it was there already. */
static int mark_dumped(uintptr_t address) {
    for (size_t h=address_hash(address)&dumped_mask;;h=(h+1)&dumped_mask) {
        if (dumped[h]==address) return 0;
        if (!dumped[h]) { dumped[h]=address; return 1; }
    }
}
/* An object's bytes, one line per nonzero qword; pointers to guest data are queued to be
 * dumped in turn while depth allows. */
static void dump_bytes(FILE *f, uintptr_t address, const unsigned char *bytes, uint64_t size, int depth) {
    for (uint64_t off=0;off+8<=size;off+=8) {
        uint64_t x;
        memcpy(&x,bytes+off,8);
        if (!x) continue;
        fprintf(f,"   +%03" PRIx64 " %016" PRIx64,off,x);
        describe(f,x);
        fputc('\n',f);
        uint64_t room;
        if (depth<FOLLOW_DEPTH && followed_count<MAX_FOLLOWED && !(x & 7) &&
            !in_image((uintptr_t)x,0,VTABLES_END,0) && bytes_at((uintptr_t)x,&room) && room>=8 && mark_dumped((uintptr_t)x))
            followed[followed_count++]=(Followed){(uintptr_t)x,address,(uint32_t)off,depth+1};
    }
}
static void dump(FILE *f, const Menu *m) {
    const Object *o=&objects[m->object];
    uint64_t room, first;
    const unsigned char *bytes=bytes_at(o->address,&room);
    if (!bytes || room<8) return;
    memcpy(&first,bytes,8);
    fprintf(f,"\n-- %s @ %" PRIxPTR "  vt image+%" PRIx64 "\n   referenced by:",o->name,o->address,first-(uintptr_t)image);
    for (int i=0;i<m->count;++i) { fputs(i ? ", " : " ",f); print_address(f,m->referrers[i]); }
    if (m->count==MAX_REFERRERS) fputs(", ...",f);
    fputc('\n',f);
    dump_bytes(f,o->address,bytes,room<OBJECT_BYTES ? room : OBJECT_BYTES,0);
}
/* Something a dumped object points at: an object when it starts with a vtable, else data. */
static void dump_followed(FILE *f, const Followed *d) {
    uint64_t room, first;
    const unsigned char *bytes=bytes_at(d->address,&room);
    if (!bytes || room<8) return;
    memcpy(&first,bytes,8);
    const int object=in_image((uintptr_t)first,VTABLES_START,VTABLES_END,8);
    const char *name=object ? class_name((uintptr_t)first) : NULL;
    fputs("\n-- ",f);
    if (name) fputs(name,f);
    else if (object) fprintf(f,"vt image+%" PRIx64,first-(uintptr_t)image);
    else fputs("data",f);
    fprintf(f," @ %" PRIxPTR "  (depth %d, from ",d->address,d->depth);
    print_address(f,d->parent);
    fprintf(f," +%03x)\n",d->offset);
    const uint64_t size=object ? OBJECT_BYTES : DATA_BYTES;
    dump_bytes(f,d->address,bytes,room<size ? room : size,d->depth);
}

/* ---- Trace (F8): the game's calls into Scaleform's GFx::Value::ObjectInterface ----
 * Every operation the menus' C++ code does on a movie (member lookups, ActionScript calls,
 * frames, text, display info) goes through this interface's vtable, with names as strings.
 * Each slot gets a thunk: "push slot; jmp common". The common code saves the argument registers
 * (xmm0-7 too) and its frame pointer, asks trace_call for the original (logging the call while a
 * trace runs, with the game's functions that led to it: the guest code keeps frame pointers),
 * restores them and jumps there. */
#define OBJECT_INTERFACE 0x51B9570u /* the AS3 ValueObjectInterface vtable */
#define INTERFACE_SLOTS 49
#define TRACE_NS UINT64_C(4000000000)
#define TRACE_BYTES (48u << 20)
#define STACK_WINDOW 0x100000       /* strings this far above a call's saved registers: its caller's stack */
#define COMMON_IMM 123              /* the trace_call address in the common code */
#define REG_FRAME 7                 /* regs[7]: the common code's rbp: [+0] the caller's rbp, [+16] the return */
#define CALLERS 6                   /* return addresses a trace line shows */
#define THUNKS 256                  /* offset of the slot thunks, 8 bytes each */
#define DISPLAY_VISIBLE 48          /* GFx::Value::DisplayInfo: six doubles, then bool Visible */

/* Slots whose v1.09 targets identify the vtable: GetMember, SetMember, Invoke, a text method,
 * AttachMovie. */
static const struct { int slot; uint32_t target; } interface_check[]={
    {5,0x2F1E30u}, {6,0x2F2490u}, {7,0x2F2A10u}, {37,0x2F7040u}, {41,0x2F76F0u},
};
static const char *const slot_names[INTERFACE_SLOTS]={
    [2]="AddRef", [3]="Release", [4]="HasMember", [5]="GetMember", [6]="SetMember", [7]="Invoke",
    [37]="Text37", [38]="Text38", [39]="Text39", [40]="CreateEmptyMovieClip", [41]="AttachMovie",
};
static const uint8_t trace_common[]={
    0x55, 0x48,0x89,0xE5, 0x48,0x83,0xE4,0xF0, 0x48,0x81,0xEC,0xC0,0x00,0x00,0x00,
    0x48,0x89,0x3C,0x24, 0x48,0x89,0x74,0x24,0x08, 0x48,0x89,0x54,0x24,0x10, 0x48,0x89,0x4C,0x24,0x18,
    0x4C,0x89,0x44,0x24,0x20, 0x4C,0x89,0x4C,0x24,0x28, 0x48,0x89,0x44,0x24,0x30, 0x48,0x89,0x6C,0x24,0x38,
    0xF3,0x0F,0x7F,0x44,0x24,0x40, 0xF3,0x0F,0x7F,0x4C,0x24,0x50, 0xF3,0x0F,0x7F,0x54,0x24,0x60,
    0xF3,0x0F,0x7F,0x5C,0x24,0x70, 0xF3,0x0F,0x7F,0xA4,0x24,0x80,0x00,0x00,0x00,
    0xF3,0x0F,0x7F,0xAC,0x24,0x90,0x00,0x00,0x00, 0xF3,0x0F,0x7F,0xB4,0x24,0xA0,0x00,0x00,0x00,
    0xF3,0x0F,0x7F,0xBC,0x24,0xB0,0x00,0x00,0x00,
    0x48,0x8B,0x7D,0x08, 0x48,0x89,0xE6, 0x48,0xB8,0,0,0,0,0,0,0,0, 0xFF,0xD0, 0x49,0x89,0xC3,
    0xF3,0x0F,0x6F,0x44,0x24,0x40, 0xF3,0x0F,0x6F,0x4C,0x24,0x50, 0xF3,0x0F,0x6F,0x54,0x24,0x60,
    0xF3,0x0F,0x6F,0x5C,0x24,0x70, 0xF3,0x0F,0x6F,0xA4,0x24,0x80,0x00,0x00,0x00,
    0xF3,0x0F,0x6F,0xAC,0x24,0x90,0x00,0x00,0x00, 0xF3,0x0F,0x6F,0xB4,0x24,0xA0,0x00,0x00,0x00,
    0xF3,0x0F,0x6F,0xBC,0x24,0xB0,0x00,0x00,0x00,
    0x48,0x8B,0x44,0x24,0x30, 0x4C,0x8B,0x4C,0x24,0x28, 0x4C,0x8B,0x44,0x24,0x20, 0x48,0x8B,0x4C,0x24,0x18,
    0x48,0x8B,0x54,0x24,0x10, 0x48,0x8B,0x74,0x24,0x08, 0x48,0x8B,0x3C,0x24,
    0x48,0x89,0xEC, 0x5D, 0x48,0x83,0xC4,0x08, 0x41,0xFF,0xE3,
};
_Static_assert(sizeof(trace_common)==241 && sizeof(trace_common)<=THUNKS,"trace common code");

static uintptr_t interface_original[INTERFACE_SLOTS];
static unsigned char *hook_page;     /* executable: the trace's common code and thunks, the list hook's code */
static int traceable, tracing;
static unsigned traces;
static uint64_t trace_start, trace_end;
static HostMutex trace_lock=HOST_MUTEX_INIT;
static char *trace_buffer;
static size_t trace_used, trace_dropped;
static uint64_t slot_calls[INTERFACE_SLOTS];
static float trace_pointer_x, trace_pointer_y;
static int trace_window_w, trace_window_h;
/* Guest memory views for reading call arguments (names live in the game's heap too): two
 * buffers, the pad thread refreshes the idle one every VIEWS_NS and switches; a reader takes
 * microseconds, far from the next refresh of the buffer it read. The probe's views are its own. */
#define VIEWS_NS UINT64_C(1000000000)
static RuntimeView *arg_views[2];
static size_t arg_view_count[2];
static int arg_views_current;
static uint64_t arg_views_ns;
static void refresh_arg_views(void) {
    const int next=1-__atomic_load_n(&arg_views_current,__ATOMIC_ACQUIRE);
    arg_view_count[next]=runtime_memory_views(arg_views[next],MAX_VIEWS);
    __atomic_store_n(&arg_views_current,next,__ATOMIC_RELEASE);
    arg_views_ns=host_monotonic_ns();
}
static uint32_t pad_buttons=UINT32_MAX;
static uint8_t pad_x, pad_y;

/* Up to size bytes at a host address outside guest memory, stopping at a fault. */
static size_t read_host(uintptr_t p, unsigned char *out, size_t size) {
    RuntimeRecoverBuf recover;
    RuntimeRecoverBuf *previous=runtime_fault_recover;
    volatile size_t done=0;
    if (RUNTIME_RECOVER_SET(recover)) { runtime_fault_recover=previous; return done; }
    runtime_fault_recover=&recover;
    for (size_t i=0;i<size;++i) { out[i]=((const volatile unsigned char *)p)[i]; done=i+1; }
    runtime_fault_recover=previous;
    return done;
}
/* Bytes an argument points at: guest memory (backing view), the image, or the caller's stack. */
static size_t read_arg(uintptr_t p, uintptr_t stack, unsigned char *out, size_t size) {
    const int current=__atomic_load_n(&arg_views_current,__ATOMIC_ACQUIRE);
    const RuntimeView *v=view_in(arg_views[current],arg_view_count[current],p);
    if (v) {
        const uint64_t room=v->start+v->size-p;
        const size_t n=room<size ? (size_t)room : size;
        memcpy(out,v->data+(p-v->start),n);
        return n;
    }
    if (in_image(p,0,DATA_END,1)) { size_t n=(uintptr_t)image+DATA_END-p; n=n<size ? n : size; memcpy(out,(const void *)p,n); return n; }
    if (p>=stack && p-stack<STACK_WINDOW) return read_host(p,out,size);
    return 0;
}
/* Appends a C string at p, if p holds one; 0 otherwise. */
static int append_string(char **at, char *end, uintptr_t p, uintptr_t stack) {
    unsigned char s[97];
    size_t n=read_arg(p,stack,s,96), len=0;
    while (len<n && s[len]>=0x20 && s[len]<0x7F) ++len;
    if (!len || len==n || s[len]) return 0;
    s[len]=0;
    *at+=snprintf(*at,(size_t)(end-*at),"\"%s\"",(const char *)s);
    return 1;
}
/* A GFx::Value as qwords: {next, prev, ObjectInterface*, type, value, aux} (managed values are
 * linked into their movie's list). The type's low bits: 2 bool, 3 int, 4 uint, 5 double,
 * 6 string, 7 wide string, 8 object, 9 array, 10 display object, 11 closure; 0x40: managed
 * (strings then point at the string pointer). */
#define VALUE_QWORDS 6
static void append_value(char **at, char *end, const uint64_t q[VALUE_QWORDS], uintptr_t stack) {
    const uint64_t *v=q+2; /* v[0] interface, v[1] type, v[2] value */
    const unsigned type=(unsigned)v[1]&0xFF, base=type&0x0F;
    double d;
    if (*at>=end) return;
    switch (base) {
    case 0: *at+=snprintf(*at,(size_t)(end-*at),"undefined"); return;
    case 1: *at+=snprintf(*at,(size_t)(end-*at),"null"); return;
    case 2: *at+=snprintf(*at,(size_t)(end-*at),"%s",(v[2]&0xFF) ? "true" : "false"); return;
    case 3: *at+=snprintf(*at,(size_t)(end-*at),"%d",(int32_t)v[2]); return;
    case 4: *at+=snprintf(*at,(size_t)(end-*at),"%uu",(uint32_t)v[2]); return;
    case 5: memcpy(&d,&v[2],8); *at+=snprintf(*at,(size_t)(end-*at),"%g",d); return;
    case 6: {
        uintptr_t s=(uintptr_t)v[2];
        if (type&0x40) { uint64_t inner; if (read_arg(s,stack,(unsigned char *)&inner,8)!=8) break; s=(uintptr_t)inner; }
        if (append_string(at,end,s,stack)) return;
        break;
    }
    case 7: {
        uint16_t w[48];
        size_t n=read_arg((uintptr_t)v[2],stack,(unsigned char *)w,sizeof(w))/2, len=0;
        *at+=snprintf(*at,(size_t)(end-*at),"L\"");
        while (len<n && w[len] && *at<end-8) {
            *at+=w[len]>=0x20 && w[len]<0x7F ? snprintf(*at,(size_t)(end-*at),"%c",(char)w[len])
                                             : snprintf(*at,(size_t)(end-*at),"\\u%04x",w[len]);
            ++len;
        }
        *at+=snprintf(*at,(size_t)(end-*at),"\"");
        return;
    }
    case 8: case 9: case 10: case 11:
        *at+=snprintf(*at,(size_t)(end-*at),"%s@%" PRIx64,base==8 ? "object" : base==9 ? "array" : base==10 ? "clip" : "closure",v[2]);
        return;
    default: break;
    }
    *at+=snprintf(*at,(size_t)(end-*at),"type%x:%" PRIx64,type,v[2]);
}
/* An argument register: a string, a GFx::Value (its ObjectInterface is this one), or a number. */
static void append_arg(char **at, char *end, uint64_t x, uint64_t self, uintptr_t stack) {
    uint64_t v[VALUE_QWORDS];
    if (*at>=end) return;
    *at+=snprintf(*at,(size_t)(end-*at)," ");
    if (x>=0x10000 && append_string(at,end,(uintptr_t)x,stack)) return;
    if (x>=0x10000 && read_arg((uintptr_t)x,stack,(unsigned char *)v,sizeof(v))==sizeof(v) && v[2]==self) {
        *at+=snprintf(*at,(size_t)(end-*at),"{");
        append_value(at,end,v,stack);
        *at+=snprintf(*at,(size_t)(end-*at),"}");
        return;
    }
    *at+=x<0x100000 ? snprintf(*at,(size_t)(end-*at),"%" PRIu64,x) : snprintf(*at,(size_t)(end-*at),"%" PRIx64,x);
}
static void trace_append(const char *line, size_t n) {
    host_lock(&trace_lock);
    if (__atomic_load_n(&tracing,__ATOMIC_ACQUIRE)) {
        if (trace_used+n<=TRACE_BYTES) { memcpy(trace_buffer+trace_used,line,n); trace_used+=n; }
        else ++trace_dropped;
    }
    host_unlock(&trace_lock);
}
/* Called by the common code for every interface call; returns where the call goes. Logging:
 * the slot, the thread, the object (pdata) and the arguments; Invoke's argument array. */
static void menu_observe(uint64_t slot, const uint64_t *regs);
static ABI uint64_t trace_call(uint64_t slot, const uint64_t *regs) {
    if (slot>=INTERFACE_SLOTS) slot=0;
    const uint64_t original=interface_original[slot];
    menu_observe(slot,regs);
    if (!__atomic_load_n(&tracing,__ATOMIC_ACQUIRE)) return original;
    __atomic_add_fetch(&slot_calls[slot],1,__ATOMIC_RELAXED);
    const uint64_t now=host_monotonic_ns();
    if (slot==2 || slot==3 || now>=trace_end) return original;
    char line[1536], *at=line, *end=line+sizeof(line)-2;
    const uintptr_t stack=(uintptr_t)regs;
    at+=snprintf(at,(size_t)(end-at),"%9.3f T%-6" PRIu64 " %-11s obj=%" PRIx64,(now-trace_start)/1e6,
                 (uint64_t)host_thread_id(),slot_names[slot] ? slot_names[slot] : "",regs[1]);
    if (!slot_names[slot]) at+=snprintf(at,(size_t)(end-at)," slot%" PRIu64,slot);
    for (int r=2;r<6 && at<end;++r) append_arg(&at,end,regs[r],regs[0],stack);
    if (slot==7 && regs[5] && regs[5]<=16 && at<end) { /* Invoke(pdata, result, name, args, count) */
        at+=snprintf(at,(size_t)(end-at)," args(");
        for (uint64_t i=0;i<regs[5] && at<end;++i) {
            uint64_t v[VALUE_QWORDS];
            if (read_arg((uintptr_t)(regs[4]+i*sizeof(v)),stack,(unsigned char *)v,sizeof(v))!=sizeof(v)) break;
            if (i) at+=snprintf(at,(size_t)(end-at),", ");
            append_value(&at,end,v,stack);
        }
        if (at<end) at+=snprintf(at,(size_t)(end-at),")");
    }
    if (slot==29 && at<end) { /* SetDisplayInfo: the values set (checks the DisplayInfo layout) */
        double d[6];
        unsigned char visible;
        if (read_arg((uintptr_t)regs[2],stack,(unsigned char *)d,sizeof(d))==sizeof(d) &&
            read_arg((uintptr_t)regs[2]+DISPLAY_VISIBLE,stack,&visible,1)==1)
            at+=snprintf(at,(size_t)(end-at)," info(x=%g y=%g rot=%g xs=%g ys=%g alpha=%g visible=%u)",
                         d[0],d[1],d[2],d[3],d[4],d[5],visible);
    }
    /* The game's functions on the way here (image offsets): the return address, then one per frame
     * up the frame pointers ([rbp] the caller's rbp, [rbp+8] its return address). */
    uint64_t fp=regs[REG_FRAME], ret=0;
    if (at<end && read_host((uintptr_t)(fp+16),(unsigned char *)&ret,8)==8 && in_image((uintptr_t)ret,0,TEXT_END,1)) {
        at+=snprintf(at,(size_t)(end-at)," <");
        for (int k=0;k<CALLERS && at<end;++k) {
            at+=snprintf(at,(size_t)(end-at)," %" PRIx64,ret-(uintptr_t)image);
            uint64_t up=0;
            if (read_host((uintptr_t)fp,(unsigned char *)&up,8)!=8 || up<=fp || up-fp>STACK_WINDOW) break;
            fp=up;
            if (read_host((uintptr_t)(fp+8),(unsigned char *)&ret,8)!=8 || !in_image((uintptr_t)ret,0,TEXT_END,1)) break;
        }
    }
    if (at>end) at=end;
    *at++='\n';
    trace_append(line,(size_t)(at-line));
    return original;
}
/* Hooks the interface's vtable once the image is mapped (BB_MENU_PROBE=1). */
static void trace_configure(void) {
    uintptr_t *table=(uintptr_t *)(image+OBJECT_INTERFACE);
    for (size_t i=0;i<sizeof(interface_check)/sizeof(*interface_check);++i)
        if (table[interface_check[i].slot]!=(uintptr_t)image+interface_check[i].target) {
            puts("Menu trace: not the v1.09 Scaleform interface, off");
            return;
        }
    trace_buffer=malloc(TRACE_BYTES);
    arg_views[0]=malloc(MAX_VIEWS*sizeof(*arg_views[0]));
    arg_views[1]=malloc(MAX_VIEWS*sizeof(*arg_views[1]));
#ifdef _WIN32
    unsigned char *code=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
#else
    unsigned char *code=mmap(NULL,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (code==MAP_FAILED) code=NULL;
#endif
    if (!code || !trace_buffer || !arg_views[0] || !arg_views[1]) { puts("Menu trace: off (no memory)"); return; }
    memcpy(code,trace_common,sizeof(trace_common));
    const uint64_t target=(uint64_t)(uintptr_t)trace_call;
    memcpy(code+COMMON_IMM,&target,8);
    for (int i=0;i<INTERFACE_SLOTS;++i) {
        unsigned char *thunk=code+THUNKS+i*8;
        const int32_t rel=-(int32_t)(THUNKS+i*8+7); /* jmp back to the common code at 0 */
        thunk[0]=0x6A; thunk[1]=(unsigned char)i; thunk[2]=0xE9;
        memcpy(thunk+3,&rel,4);
        interface_original[i]=table[i];
    }
    for (int i=0;i<INTERFACE_SLOTS;++i) __atomic_store_n(&table[i],(uintptr_t)(code+THUNKS+i*8),__ATOMIC_RELEASE);
    hook_page=code;
    traceable=1;
}
static void trace_begin(float pointer_x, float pointer_y, int window_w, int window_h) {
    if (!traceable || __atomic_load_n(&tracing,__ATOMIC_ACQUIRE)) return;
    host_lock(&trace_lock);
    refresh_arg_views();
    trace_used=trace_dropped=0;
    memset(slot_calls,0,sizeof(slot_calls));
    trace_pointer_x=pointer_x; trace_pointer_y=pointer_y; trace_window_w=window_w; trace_window_h=window_h;
    trace_start=host_monotonic_ns();
    trace_end=trace_start+TRACE_NS;
    pad_buttons=UINT32_MAX;
    ++traces;
    __atomic_store_n(&tracing,1,__ATOMIC_RELEASE);
    host_unlock(&trace_lock);
    printf("%sMenu trace %u: recording %.0f s of menu calls%s\n",on,traces,TRACE_NS/1e9,off);
}
/* Pad changes during a trace become lines too: when the game saw a direction or a button. */
static void trace_pad(uint32_t buttons, uint8_t left_x, uint8_t left_y) {
    const uint8_t x=left_x<64 ? 0 : left_x>192 ? 2 : 1, y=left_y<64 ? 0 : left_y>192 ? 2 : 1;
    if (buttons==pad_buttons && x==pad_x && y==pad_y) return;
    pad_buttons=buttons; pad_x=x; pad_y=y;
    char line[128];
    const int n=snprintf(line,sizeof(line),"%9.3f == pad buttons=%x stick=%s%s\n",(host_monotonic_ns()-trace_start)/1e6,buttons,
                         y==0 ? "up" : y==2 ? "down" : "",x==0 ? "left" : x==2 ? "right" : (y==1 ? "center" : ""));
    trace_append(line,(size_t)n);
}
static void trace_finish(void) {
    int running=1;
    if (host_monotonic_ns()<trace_end ||
        !__atomic_compare_exchange_n(&tracing,&running,0,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED)) return;
    host_lock(&trace_lock); /* appends still in flight finish first */
    host_unlock(&trace_lock);
    char path[512], stamp[32];
    time_t now=time(NULL);
    strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",localtime(&now));
#ifdef _WIN32
    _mkdir(log_dir());
#else
    mkdir(log_dir(),0755);
#endif
    snprintf(path,sizeof(path),"%s/menu-trace-%s-%u.txt",log_dir(),stamp,traces);
    FILE *f=fopen(path,"w");
    if (!f) { printf("Menu trace %u: cannot write %s\n",traces,path); return; }
    fprintf(f,"Menu trace %u, %s\nPointer %.0f,%.0f in a %dx%d window (points)\nCalls per slot:",traces,stamp,
            trace_pointer_x,trace_pointer_y,trace_window_w,trace_window_h);
    for (int i=0;i<INTERFACE_SLOTS;++i)
        if (slot_calls[i] && slot_names[i]) fprintf(f," %s=%" PRIu64,slot_names[i],slot_calls[i]);
        else if (slot_calls[i]) fprintf(f," slot%d=%" PRIu64,i,slot_calls[i]);
    fprintf(f,"\n%zu lines dropped\n\n",trace_dropped);
    fwrite(trace_buffer,1,trace_used,f);
    fclose(f);
    printf("%sMenu trace %u: %zu bytes -> %s (done)%s\n",on,traces,trace_used,path,off);
}

/* ---- Mouse in menus ----
 * A menu's selectable items are clips named Item_<a>_<b> under a container clip, each holding
 * a "Cursor" clip: GetMember(container,"Item_1_0"), GetMember(item,"Cursor"), then calls on the
 * cursor. Each refresh (the menu opening, the selection moving) restarts Item_0_0's cursor
 * ("Loop", then SetDisplayInfo, slot 29), looks at every cursor (GetDisplayInfo, slot 28) and
 * ends with SetDisplayInfo on the selected item's cursor: the last cursor set is the selection.
 * The hook learns items and selections from those calls; a container the game asks for its
 * "CurrentItem" is also searched for Item_<a>_<b> clips with a cursor right away, so a menu is
 * known before its first refresh. On the game's own thread it asks Scaleform for each item's
 * bounds (getBounds(stage): the cursor's, which frame what is shown, else the item's, which may
 * hold hidden text). While a menu is on screen (the game touches its container every frame) the
 * window frees the mouse; the item under the pointer is reached with d-pad presses toward its
 * top left corner, each waiting for the selection to move (a selection unknown yet is revealed
 * by one press; one that comes back to an item already passed gives up: a list too short, an
 * empty row); the left button confirms, the right one cancels. Menu movies are 1920x1080
 * stages over the game's picture, letterboxed in the window as the presenter fits it: positions
 * are stage units at any resolution.
 *
 * Most menus are the game's list widget: a grid of those clips showing a scrolled run of entries,
 * the selection kept in the widget. A hook at the start of its per-frame update (every list type
 * runs it, on the game's thread) learns each live list: its container (through the clip namer it
 * holds), its selection and its layout. Pointing at an item of a list then selects it in one jump:
 * the hook calls the widget's SetSelection, then tells its listener (the dialog: description,
 * help text) and requests the cursor sound, as the update does after a key press. A list the game
 * stops updating has lost the focus (a screen over it, like the equipment slots over the in-game
 * menu): the mouse stays free, but its items take nothing. An options screen shows each row's
 * value beside the row's name, as a list of one clip or as a slider (a gauge between two arrows,
 * hooked at its update as well), a dropdown or a text field: pointing at a value selects its row
 * (a click on a dropdown's or a text field's confirms it, as on the row's name); a click on a list's half
 * presses left or right (the game steps it, around the ends: a switch toggles at every click), one
 * on a slider's arrow steps it and one on its bar (or a drag along it) sets it (its SetValue: the
 * game applies and sounds it). A box's spinner (a quantity: a shop's) is hooked at its update too:
 * a click above its number's middle presses up, below it down. Menus without a list widget take
 * d-pad steps as above. */
#define MAX_ITEMS 256
#define MAX_CONTAINERS 16
#define STAGE_W 1920.0f
#define STAGE_H 1080.0f
#define MENU_IDLE_NS UINT64_C(300000000)   /* a container untouched this long: its menu closed */
#define BOUNDS_NS UINT64_C(1000000000)     /* item bounds are measured again after this */
#define MEASURES_PER_CALL 4
#define PRESS_NS UINT64_C(34000000)        /* two frames held, two released: the shortest steps the game takes */
#define GAP_NS UINT64_C(34000000)          /* released between presses: each is a new press */
#define SETTLE_NS UINT64_C(350000000)      /* a press that moved nothing this long: out of reach */
#define REFRESH_NS UINT64_C(5000000)       /* a selection this fresh may be mid-refresh (Item_0_0 first) */
#define MAX_STEPS 12                       /* presses toward one target before giving up on it */
#define APPROX_RADIUS 0.6f                 /* items known as points: a pointer within this many pitches */
#define PROBE_NS UINT64_C(2000000000)      /* a container searched for items is searched again after this */
#define PROBE_ROWS 16
#define PROBE_COLUMNS 10
#define MAX_VISITED 24
/* libScePad buttons (runtime_pad.c). */
#define PAD_UP 0x10u
#define PAD_RIGHT 0x20u
#define PAD_DOWN 0x40u
#define PAD_LEFT 0x80u
#define PAD_CIRCLE 0x2000u
#define PAD_CROSS 0x4000u
/* The list widget (v1.09). */
#define LIST_UPDATE 0x1ACFF30u        /* update(list, input): every frame the list has the focus */
#define LIST_SET_SELECTION 0x1ACF6B0u /* int SetSelection(list, entry): scrolls to it, moves the cursor; 1 done */
#define LIST_PROLOGUE 20              /* push rbp ... sub rsp,0x98: the bytes the hook's jump replaces */
#define LIST_LISTENER 0x30            /* object told of a new selection: vtable slot 2 (self, int *entry) */
#define LIST_COUNT 0x70               /* int: entries */
#define LIST_SELECTED 0x74            /* int: the selected entry */
#define LIST_OVERRIDE 0x78            /* int: when >= 0, the selection instead of LIST_SELECTED */
#define LIST_COLUMNS 0x7C             /* clips per row */
#define LIST_ROWS 0x80                /* rows of clips; 1: one row holding every entry */
#define LIST_FIRST_COLUMN 0x84        /* scrolling: the entry offset of the first clip */
#define LIST_FIRST_ROW 0x24C          /* scrolling: the entry row in the top row of clips */
#define LIST_NAMER 0x258              /* names the clips Item_<row>_<column>: a GFx::Value of the container at +40 */
#define LIST_WRAPS 0x2B0              /* byte: the entries wrap around */
#define NAMER_OI 0x50
#define NAMER_TYPE 0x58
#define NAMER_CONTAINER 0x60
#define MENU_SOUND 0x557FC60u         /* the menu sound to play: {int, int priority, const char *name} */
#define CURSOR_SOUND 0x49344E4u       /* "カーソル移動" (cursor move), UTF-8 */
#define LIST_LIVE_NS UINT64_C(250000000) /* a list updated this lately has the focus */
#define CONFIRM_NS UINT64_C(40000000)    /* a jump's selection settles this long before the confirm press */
#define MAX_LIST_NOTES 512
static const uint8_t list_update_code[26]={0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,
                                           0x48,0x81,0xEC,0x98,0x00,0x00,0x00,0x49,0x89,0xF7,0x49,0x89,0xFD};
static const uint8_t list_set_selection_code[26]={0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,
                                                  0x48,0x81,0xEC,0xE8,0x00,0x00,0x00,0x41,0x89,0xF6,0x49,0x89,0xFC};
static const char cursor_sound[]="\xE3\x82\xAB\xE3\x83\xBC\xE3\x82\xBD\xE3\x83\xAB\xE7\xA7\xBB\xE5\x8B\x95";
/* The slider (v1.09): an options row's gauge, the clips "Bar", "LeftArrow" and "RightArrow"
 * under the row's, each in a holder (vtable CLIP_HOLDER): a value wrapper at +0x28, its
 * GFx::Value at +0x30 (the holder's getter, 0x1ADB830, reads the interface, type and clip). */
#define SLIDER_UPDATE 0x1ADC970u      /* update(slider, input): every frame for every row, the input on in the selected one's */
#define SLIDER_SET_VALUE 0x1ADCAA0u   /* SetValue(slider, value, notify, sound id, sound name): clamps, shows, tells, sounds */
#define SLIDER_PROLOGUE 16            /* push rbp ... mov rbx,rdi */
#define CLIP_HOLDER 0x539A070u
#define SLIDER_BAR 0xA70              /* the holders */
#define SLIDER_LEFT 0xB30
#define SLIDER_RIGHT 0xB90
#define HOLDER_OI 0x40
#define HOLDER_TYPE 0x48
#define HOLDER_CLIP 0x50
#define SLIDER_MIN 0xBF0              /* ints */
#define SLIDER_MAX 0xBF4
#define SLIDER_VALUE 0xBF8
#define SLIDER_STEP 0xBFC
#define SLIDER_SOUND 0x493387Bu       /* "スライダー調整" (slider adjustment), UTF-8, with sound id 0x190 as the update passes */
#define SLIDER_SOUND_ID 0x190
/* The spinner (v1.09): a quantity to choose, a shop's or an item's, the number "Text_1" between an
 * up and a down arrow (unnamed clips just above and below it: itemquantityselect.gfx, numselect.gfx).
 * Built by 0x1B56750 at +0x1F20 of its box (vtable 0x539C8A0), its number's holder at +0x10. */
#define SPINNER_UPDATE 0x1ADD210u     /* update(spinner, input): up and down step it (around the ends) */
#define SPINNER_PROLOGUE 16           /* push rbp ... mov rbx,rsi */
#define SPINNER_VTABLE 0x539A100u
#define SPINNER_NUMBER 0x10           /* the holder of "Text_1" */
#define SPINNER_MIN 0x70              /* ints */
#define SPINNER_MAX 0x74
#define SPINNER_VALUE 0x78
#define SPINNER_STEP 0x7C
#define SPINNER_REACH_X 40.0f         /* its arrows' area: this far either side of the number's middle, */
#define SPINNER_REACH_Y 28.0f         /* and above and below the number (the arrows 10 px off it, 11 high) */
static const uint8_t spinner_update_code[26]={0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x53,0x48,0x83,0xEC,0x28,
                                              0x48,0x89,0xF3,0x49,0x89,0xFE,0x4C,0x8B,0x3D,0xB6,0x69,0x90,0x03};
static const uint8_t slider_update_code[26]={0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x53,0x50,0x49,0x89,0xF6,
                                             0x48,0x89,0xFB,0x44,0x8B,0xBB,0xF8,0x0B,0x00,0x00,0x4C,0x89,0xF7};
static const uint8_t slider_set_value_code[16]={0x55,0x48,0x89,0xE5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,
                                                0x48,0x83,0xEC};
static const char slider_sound[]="\xE3\x82\xB9\xE3\x83\xA9\xE3\x82\xA4\xE3\x83\x80\xE3\x83\xBC\xE8\xAA\xBF\xE6\x95\xB4";
/* A hook's code: the arguments kept around hook(rdi, rsi), the replaced prologue (NOPs after a
 * shorter one), back. */
static const uint8_t hook_trampoline[]={
    0x57, 0x56, 0x48,0x83,0xEC,0x08,                   /*  0 push rdi; push rsi; sub rsp,8 */
    0x48,0xB8,0,0,0,0,0,0,0,0, 0xFF,0xD0,              /*  6 mov rax,hook; call rax */
    0x48,0x83,0xC4,0x08, 0x5E, 0x5F,                   /* 18 add rsp,8; pop rsi; pop rdi */
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,            /* 24 the prologue */
    0xFF,0x25,0x00,0x00,0x00,0x00,                     /* 44 jmp [rip+0]; after the prologue */
};
#define TRAMPOLINE_HOOK 8
#define TRAMPOLINE_PROLOGUE 24
#define TRAMPOLINE_SLOT 20
#define TRAMPOLINE_BACK 50
#define TRAMPOLINE_SIZE 64
#define TRAMPOLINE_AT 1024            /* in the trace's code page: the list's, the slider's, the spinner's */
_Static_assert(sizeof(hook_trampoline)==TRAMPOLINE_BACK && TRAMPOLINE_PROLOGUE+TRAMPOLINE_SLOT==44 &&
               LIST_PROLOGUE<=TRAMPOLINE_SLOT && SLIDER_PROLOGUE<=TRAMPOLINE_SLOT && SPINNER_PROLOGUE<=TRAMPOLINE_SLOT &&
               TRAMPOLINE_BACK+8<=TRAMPOLINE_SIZE,
               "hook code");

/* GFx::Value (0x30 bytes): a managed value (type bit 0x40) is linked into its movie's list
 * (next, prev) until released, so every one returned to us is released before it goes. */
typedef struct { uint64_t next, prev, oi; uint32_t type, pad; uint64_t value, aux; } GfxValue;
_Static_assert(sizeof(GfxValue)==VALUE_QWORDS*8,"GFx::Value layout");
typedef ABI uint64_t (*DisplayFn)(uint64_t oi, uint64_t pdata, double *info);
typedef ABI uint64_t (*GetMemberFn)(uint64_t oi, uint64_t pdata, const char *name, GfxValue *out, uint64_t isdobj);
typedef ABI uint64_t (*InvokeFn)(uint64_t oi, uint64_t pdata, GfxValue *result, const char *name, const GfxValue *args,
                                 uint64_t count, uint64_t isdobj);
typedef ABI void (*ReleaseFn)(uint64_t oi, GfxValue *value, uint64_t data);

typedef struct {
    uint64_t oi, container, item, cursor;
    int a, b;                  /* Item_<a>_<b> */
    float x0, y0, x1, y1;      /* stage bounds, valid with bounds_ns; a point when approx */
    uint64_t bounds_ns, seen_ns;
    int approx;                /* from display info: the item's origin only */
    int whole;                 /* the item's bounds, not its cursor's (hidden text may widen them) */
    float value_x0, value_x1;  /* an options row's value beside its name that stands for the row (a
                                * dropdown's current item, a text field); none where x1 <= x0 */
    int select_only;           /* the row is its Cursor alone, a highlight over what is drawn elsewhere
                                * (levelup.gfx's attributes, iconhelp.gfx): a click selects it and
                                * confirms nothing (a level-up's confirm asks to level up) */
    float step_x0, step_x1;    /* such a row's number beside it (a level-up's attribute's Value_0): a
                                * click on its left half presses left, on its right half right */
    int absent;                /* a list's clip that shows no entry (a dialog's second button, rows past the end) */
} MenuItem;
/* active_ns: when its selection last moved or its items were first found (the newest is on top:
 * a submenu over the menu that is still drawn behind it). */
typedef struct {
    uint64_t container, touch_ns, select_ns, probe_ns, active_ns;
    uint64_t behind;           /* a press here moved this other container: in the background while it lives;
                                * COVERED: presses here went unanswered (a screen over it the mouse does not
                                * know, like the equipment slots), until its selection moves again */
    uint64_t covered_ns;
    uint64_t list, list_ns;    /* the list widget showing its items, and its last update */
    int list_rows, list_columns; /* its clips (one row of several: a bar, like an inventory's tabs) */
    int focused;               /* its last update had the input on */
    int selected, unanswered;
} MenuContainer;
#define COVERED UINT64_MAX
#define UNANSWERED_MAX 2
/* A list's state, read on its update. */
typedef struct { int count, selected, columns, rows, first_column, first_row, wraps; } ListState;
/* Pad thread -> list hook: select the entry under Item_<jump_a>_<jump_b> of jump_list. */
enum { JUMP_IDLE, JUMP_POSTED, JUMP_TAKEN, JUMP_DONE };
/* A control, learnt on its update: an options row's slider (its arrows and bar) or list of one clip
 * (its arrows, LeftItem_0 and RightItem_0, or its clip's halves), a box's spinner (above and below
 * its number). PART_LEFT steps back (left; a spinner's down), PART_RIGHT on (right; up). */
enum { CONTROL_SLIDER=1, CONTROL_VALUES, CONTROL_SPINNER };
enum { PART_LEFT, PART_RIGHT, PART_BAR, PARTS };
typedef struct {
    uint64_t object, container; /* the slider or the list; the list's container */
    int kind, focused;          /* focused: its row is the selected one (updated with the input on) */
    int min, max, value, step;  /* a slider's */
    float part[PARTS][4];       /* stage bounds; none where x1 <= x0 */
    uint64_t seen_ns, bounds_ns;
} Control;
#define MAX_CONTROLS 16
#define ACTION_NS UINT64_C(500000000)  /* a click on a control waits this long for its row to be selected */
#define ARROW_MARGIN 8.0f               /* an arrow takes clicks this far beyond its glyph */
#define VALUE_REACH 24.0f               /* a value without the widget's arrows: its halves reach this far out */

static int menu_mouse;
static HostMutex menu_lock=HOST_MUTEX_INIT;
static MenuItem menu_items[MAX_ITEMS];
static int menu_item_count;
static MenuContainer containers[MAX_CONTAINERS];
static __thread int pending_item=-1, pending_cursor=-1, measuring;
static int bounds_broken;            /* a measurement failed badly: no more */
/* Pad thread: the navigation. */
static int menu_shown, blocked=-1, nav_target=-1, confirm_target=-1, press_from=-1, steps;
static int visited[MAX_VISITED], visited_count;
static float last_pointer_x=-1, last_pointer_y=-1;
static uint32_t press_button, previous_free;
static uint64_t press_container, press_ns; /* where the last d-pad press was aimed, and when */
static uint64_t press_until, gap_until, settle_until;
static uint32_t confirm_button=PAD_CROSS, cancel_button=PAD_CIRCLE;
/* The list hook and the jumps (under menu_lock). */
static int jump_state=JUMP_IDLE, jump_a, jump_b, jump_target=-1, jump_moved;
static uint64_t jump_list, jump_ns;
static uint64_t noted_lists[MAX_LIST_NOTES];
static int listable;                 /* the list hook is in */
/* The slider hook and the controls (under menu_lock): pad thread -> slider hook, set slider_target
 * to slider_value (slider_value steps from its value when slider_relative). */
static Control controls[MAX_CONTROLS];
static int slider_state=JUMP_IDLE, slider_value, slider_relative, slidable;
static uint64_t slider_target, slider_ns;
/* Pad thread: a click on a control, applied once its row is selected; a drag along a bar. */
static int action_control=-1, action_part, action_value, dragging;
/* Pad thread: a click on a cursor-only row's number: its row, then the d-pad button, once selected. */
static int step_item=-1;
static uint32_t step_button;
static uint64_t step_ns;
static uint64_t action_object, action_ns;

static void menu_note(const char *format, ...) __attribute__((format(printf,1,2)));
static void menu_note(const char *format, ...) {
    char line[512];
    int n=snprintf(line,sizeof(line),"%9.3f == menu: ",(host_monotonic_ns()-trace_start)/1e6);
    va_list args;
    va_start(args,format);
    n+=vsnprintf(line+n,sizeof(line)-(size_t)n-1,format,args);
    va_end(args);
    if (n>(int)sizeof(line)-2) n=(int)sizeof(line)-2;
    line[n++]='\n';
    trace_append(line,(size_t)n);
}
static int read_name(uintptr_t p, uintptr_t stack, char *out, size_t size) {
    size_t n=p>=0x10000 ? read_arg(p,stack,(unsigned char *)out,size-1) : 0, len=0;
    while (len<n && out[len]>=0x20 && out[len]<0x7F) ++len;
    if (len==n || out[len]) return 0;
    return 1;
}
static MenuContainer *container_of(uint64_t container, int add) {
    MenuContainer *oldest=&containers[0];
    for (int i=0;i<MAX_CONTAINERS;++i) {
        if (containers[i].container==container) return &containers[i];
        if (containers[i].touch_ns<oldest->touch_ns) oldest=&containers[i];
    }
    if (!add) return NULL;
    *oldest=(MenuContainer){.container=container,.touch_ns=host_monotonic_ns(),.selected=-1};
    return oldest;
}
static int item_index(uint64_t container, int a, int b) {
    int oldest=0;
    for (int i=0;i<menu_item_count;++i) {
        if (menu_items[i].container==container && menu_items[i].a==a && menu_items[i].b==b) return i;
        if (menu_items[i].seen_ns<menu_items[oldest].seen_ns) oldest=i;
    }
    if (menu_item_count<MAX_ITEMS) return menu_item_count++;
    for (int c=0;c<MAX_CONTAINERS;++c) if (containers[c].selected==oldest) containers[c].selected=-1;
    return oldest;
}
/* The item Item_<a>_<b> of a container, known from now on (seen now). */
static int item_add(uint64_t oi, uint64_t container, int a, int b, uint64_t now) {
    const int i=item_index(container,a,b);
    MenuItem *m=&menu_items[i];
    if (m->container!=container || m->a!=a || m->b!=b) *m=(MenuItem){.oi=oi,.container=container,.a=a,.b=b,.seen_ns=now};
    m->seen_ns=now;
    return i;
}
static int item_by(uint64_t clip, int cursor) {
    for (int i=0;i<menu_item_count;++i)
        if (clip && (cursor ? menu_items[i].cursor : menu_items[i].item)==clip) return i;
    return -1;
}
static void value_release(GfxValue *v) {
    if ((v->type&0x40) && v->oi) ((ReleaseFn)interface_original[3])(v->oi,v,v->value);
    memset(v,0,sizeof(*v));
}
static int number_of(const GfxValue *v, double *out) {
    switch (v->type&0x0F) {
    case 5: memcpy(out,&v->value,8); return 1;
    case 3: *out=(int32_t)v->value; return 1;
    case 4: *out=(uint32_t)v->value; return 1;
    default: return 0;
    }
}
/* A clip's bounds on the stage: getBounds(stage), then the rectangle's x, y, width, height. */
static int bounds_of(uint64_t oi, uint64_t clip, const GfxValue *stage, float out[4]) {
    const GetMemberFn get=(GetMemberFn)interface_original[5];
    const InvokeFn invoke=(InvokeFn)interface_original[7];
    GfxValue rect;
    memset(&rect,0,sizeof(rect));
    int ok=0;
    if ((invoke(oi,clip,&rect,"getBounds",stage,1,1)&0xFF) && (rect.type&0x0F)==8) {
        static const char *const names[4]={"x","y","width","height"};
        double r[4];
        ok=1;
        for (int k=0;k<4 && ok;++k) {
            GfxValue v;
            memset(&v,0,sizeof(v));
            ok=(get(oi,rect.value,names[k],&v,0)&0xFF) && number_of(&v,&r[k]);
            value_release(&v);
        }
        if (ok) { out[0]=(float)r[0]; out[1]=(float)r[1]; out[2]=(float)(r[0]+r[2]); out[3]=(float)(r[1]+r[3]); }
    }
    value_release(&rect);
    return ok;
}
/* An item's bounds on the stage, through the interface's originals (not traced). The container is
 * alive (the game is calling it), so the item is fetched from it afresh and never kept: menus
 * rebuild their clips. getBounds(stage) where the interface reaches AS3 properties: the cursor's,
 * else the item's (*whole); else the item's and the container's display info (x, y, scale) give
 * the item's origin (*approx). An options row's value too (row_value; value[2] <= value[0]: none). */
static int row_value(uint64_t oi, uint64_t item, float out[4]);
static int row_number(uint64_t oi, uint64_t container, const float row[4], float out[4]);
static int measure(uint64_t oi, uint64_t container, int a, int b, float out[4], int *approx, int *whole, float value[4],
                   int *select_only, float number[4]) {
    const GetMemberFn get=(GetMemberFn)interface_original[5];
    const DisplayFn display=(DisplayFn)interface_original[28];
    GfxValue item, stage;
    memset(&item,0,sizeof(item)); memset(&stage,0,sizeof(stage));
    char name[24];
    snprintf(name,sizeof(name),"Item_%d_%d",a,b);
    int ok=0;
    *approx=*whole=*select_only=0;
    memset(value,0,4*sizeof(*value));
    memset(number,0,4*sizeof(*number));
    if ((get(oi,container,name,&item,1)&0xFF) && (item.type&0x0F)==10) {
        if (!row_value(oi,item.value,value)) memset(value,0,4*sizeof(*value));
        GfxValue children;
        memset(&children,0,sizeof(children));
        double count=0;
        *select_only=(get(oi,item.value,"numChildren",&children,1)&0xFF) && number_of(&children,&count) && count==1;
        value_release(&children);
        if ((get(oi,item.value,"stage",&stage,1)&0xFF) && ((stage.type&0x0F)==10 || (stage.type&0x0F)==8)) {
            GfxValue cursor;
            memset(&cursor,0,sizeof(cursor));
            if ((get(oi,item.value,"Cursor",&cursor,1)&0xFF) && (cursor.type&0x0F)==10)
                ok=bounds_of(oi,cursor.value,&stage,out) && out[2]-out[0]>4 && out[3]-out[1]>4;
            value_release(&cursor);
            if (!ok) ok=*whole=bounds_of(oi,item.value,&stage,out);
        }
        if (!ok) {
            double di[64], ci[64]; /* DisplayInfo: x, y, rotation, xscale, yscale (percent), alpha, ... */
            memset(di,0,sizeof(di)); memset(ci,0,sizeof(ci));
            display(oi,item.value,di);
            display(oi,container,ci);
            if (ci[3]>0 && ci[3]<10000 && ci[4]>0 && ci[4]<10000) {
                out[0]=out[2]=(float)(ci[0]+di[0]*ci[3]/100);
                out[1]=out[3]=(float)(ci[1]+di[1]*ci[4]/100);
                ok=*approx=1;
            }
        }
    }
    value_release(&stage);
    value_release(&item);
    if (ok && !*approx && *select_only && !row_number(oi,container,out,number)) memset(number,0,4*sizeof(*number));
    return ok;
}
/* The container's Item_<a>_<b> clips that have a cursor, fetched afresh (the container is alive:
 * the game is calling it): rows until one has no Item_<a>_0, columns until a gap. */
typedef struct { int a, b; uint64_t item, cursor; } Found;
static int probe_items(uint64_t oi, uint64_t container, Found *found, int max) {
    const GetMemberFn get=(GetMemberFn)interface_original[5];
    int count=0;
    for (int a=0;a<PROBE_ROWS;++a) {
        int row=0;
        for (int b=0;b<PROBE_COLUMNS && count<max;++b) {
            char name[24];
            snprintf(name,sizeof(name),"Item_%d_%d",a,b);
            GfxValue item, cursor;
            memset(&item,0,sizeof(item)); memset(&cursor,0,sizeof(cursor));
            const int is_item=(get(oi,container,name,&item,1)&0xFF) && (item.type&0x0F)==10;
            if (is_item) {
                ++row;
                if ((get(oi,item.value,"Cursor",&cursor,1)&0xFF) && (cursor.type&0x0F)==10)
                    found[count++]=(Found){a,b,item.value,cursor.value};
            }
            value_release(&cursor);
            value_release(&item);
            if (!is_item) break;
        }
        if (!row) break;
    }
    return count;
}
static int cursor_items(uint64_t container);
static void scan_container(uint64_t oi, uint64_t container, int probe_now, int measure_now, int first, uint64_t now);
/* Every interface call (the hook, before the original runs). */
static void menu_observe(uint64_t slot, const uint64_t *regs) {
    if (!menu_mouse || measuring || slot==2 || slot==3) return;
    const uint64_t obj=regs[1], now=host_monotonic_ns();
    const uintptr_t stack=(uintptr_t)regs;
    int measure_now=0, probe_now=0;
    host_lock(&menu_lock);
    /* The call after GetMember(container,"Item_a_b") is on that item; the one after
     * GetMember(item,"Cursor") on its cursor. */
    if (pending_item>=0) {
        if (obj!=menu_items[pending_item].container) menu_items[pending_item].item=obj;
        pending_item=-1;
    } else if (pending_cursor>=0) {
        if (obj!=menu_items[pending_cursor].item) menu_items[pending_cursor].cursor=obj;
        pending_cursor=-1;
    }
    char name[40];
    if (slot==5 && read_name((uintptr_t)regs[2],stack,name,sizeof(name))) {
        int a, b, end=0;
        if (!strncmp(name,"Item_",5) && sscanf(name,"Item_%d_%d%n",&a,&b,&end)==2 && !name[end]) {
            pending_item=item_add(regs[0],obj,a,b,now);
            if (__atomic_load_n(&menu_shown,__ATOMIC_RELAXED)) {
                MenuContainer *c=container_of(obj,1);
                if (!c->probe_ns || now-c->probe_ns>PROBE_NS) { c->probe_ns=now; probe_now=1; }
            }
        } else if (!strcmp(name,"Cursor")) {
            pending_cursor=item_by(obj,0);
        } else if (!strcmp(name,"CurrentItem")) {
            MenuContainer *c=container_of(obj,1);
            if (!c->probe_ns || now-c->probe_ns>PROBE_NS) { c->probe_ns=now; probe_now=1; }
        }
    }
    if (slot==29) { /* SetDisplayInfo on a cursor: the last one of a refresh is the selection */
        const int i=item_by(obj,1);
        MenuContainer *c=i>=0 ? container_of(menu_items[i].container,1) : NULL;
        if (c && !(c->list && now-c->list_ns<LIST_LIVE_NS)) { /* a live list tells its selection itself */
            if (c->selected!=i) menu_note("selected Item_%d_%d (container %" PRIx64 ")",menu_items[i].a,menu_items[i].b,c->container);
            c->selected=i; c->select_ns=c->touch_ns=c->active_ns=now; c->unanswered=0;
        }
    }
    MenuContainer *c=container_of(obj,0);
    if (c) { c->touch_ns=now; measure_now=!bounds_broken; }
    host_unlock(&menu_lock);
    scan_container(regs[0],obj,probe_now,measure_now,-1,now);
}
/* On the game's thread, during its call on a live container: finds its items (probe_now) and
 * measures a few whose bounds are old (measure_now), the item `first` first. */
static void scan_container(uint64_t oi, uint64_t container, int probe_now, int measure_now, int first, uint64_t now) {
    if (probe_now) {
        Found found[PROBE_ROWS*PROBE_COLUMNS];
        measuring=1;
        const int count=probe_items(oi,container,found,PROBE_ROWS*PROBE_COLUMNS);
        measuring=0;
        host_lock(&menu_lock);
        MenuContainer *found_in=container_of(container,1);
        if (count && !cursor_items(container)) found_in->active_ns=now;
        for (int k=0;k<count;++k) {
            const int i=item_add(oi,container,found[k].a,found[k].b,now);
            menu_items[i].item=found[k].item; menu_items[i].cursor=found[k].cursor;
        }
        host_unlock(&menu_lock);
        if (count) menu_note("found %d items with a cursor (container %" PRIx64 ")",count,container);
    }
    if (!measure_now) return;
    /* A few of its items whose bounds are old. */
    for (int n=0;n<MEASURES_PER_CALL;) {
        MenuItem m;
        int index=-1;
        host_lock(&menu_lock);
        for (int k=-1;k<menu_item_count && index<0;++k) {
            const int i=k<0 ? first : k;
            if (i>=0 && i<menu_item_count && menu_items[i].container==container && menu_items[i].cursor &&
                (!menu_items[i].bounds_ns || now-menu_items[i].bounds_ns>BOUNDS_NS)) { index=i; m=menu_items[i]; }
        }
        if (index>=0) menu_items[index].bounds_ns=now; /* tried, even if it fails */
        host_unlock(&menu_lock);
        if (index<0) break;
        ++n;
        float r[4]={0,0,0,0};
        int approx=0, whole=0;
        float value[4], number[4];
        int select_only=0;
        measuring=1;
        const int ok=measure(oi,container,m.a,m.b,r,&approx,&whole,value,&select_only,number);
        measuring=0;
        host_lock(&menu_lock);
        MenuItem *t=&menu_items[index];
        if (t->container==container && t->a==m.a && t->b==m.b) {
            if (ok) { t->x0=r[0]; t->y0=r[1]; t->x1=r[2]; t->y1=r[3]; t->approx=approx; t->whole=whole; }
            else { t->x1=t->x0; t->y1=t->y0; t->approx=0; } /* never hit; tried again after BOUNDS_NS */
            t->value_x0=value[0]; t->value_x1=value[2];
            if (select_only && !t->select_only) menu_note("Item_%d_%d is its cursor alone: a click selects it only",m.a,m.b);
            t->select_only=select_only;
            t->step_x0=number[0]; t->step_x1=number[2];
        }
        host_unlock(&menu_lock);
        menu_note("bounds Item_%d_%d %s (%.0f,%.0f)-(%.0f,%.0f)",m.a,m.b,!ok ? "failed" : approx ? "origin" : "ok",
                  r[0],r[1],r[2],r[3]);
    }
}
/* The item under a stage point: the smallest whose bounds hold it; for items known by their
 * origins (display info), the nearest one within APPROX_RADIUS pitches of its cell's middle
 * (origins at the top left, as the cursors' offsets suggest), along the axes the items spread on
 * (a column gives no width: the pointer only has to be near its side of the screen). */
/* An item to point at: measured, with a cursor, showing something. */
static int pointable(const MenuItem *m, uint64_t container) {
    return m->container==container && m->cursor && m->bounds_ns && !m->absent;
}
/* The spacing of a container's measured items: the smallest gap between top left corners along a
 * row (x) and along a column (y), 0 where they do not spread; and the glow, how far the smallest
 * cursor reaches past a pitch on each side. */
typedef struct { float pitch[2], glow[2]; } Spacing;
static Spacing spacing(uint64_t container, int approx) {
    Spacing g={{0,0},{0,0}};
    float least[2]={0,0};
    for (int i=0;i<menu_item_count;++i) {
        const MenuItem *m=&menu_items[i];
        if (!pointable(m,container) || m->approx!=approx || (!approx && m->x1<=m->x0)) continue;
        const float size[2]={m->x1-m->x0,m->y1-m->y0};
        for (int k=0;k<2;++k) if (!m->whole && (!least[k] || size[k]<least[k])) least[k]=size[k];
        for (int j=i+1;j<menu_item_count;++j) {
            const MenuItem *n=&menu_items[j];
            if (!pointable(n,container) || n->approx!=approx || (!approx && n->x1<=n->x0)) continue;
            const float dx=m->x0>n->x0 ? m->x0-n->x0 : n->x0-m->x0, dy=m->y0>n->y0 ? m->y0-n->y0 : n->y0-m->y0;
            if (dx>2 && dy<=8 && (!g.pitch[0] || dx<g.pitch[0])) g.pitch[0]=dx;
            if (dy>2 && dx<=8 && (!g.pitch[1] || dy<g.pitch[1])) g.pitch[1]=dy;
        }
    }
    for (int k=0;k<2;++k) if (g.pitch[k] && least[k]>g.pitch[k]) g.glow[k]=(least[k]-g.pitch[k])/2;
    return g;
}
/* An item's area to point at: its bounds, but no more than one pitch across. Cursors' bounds hold
 * their glow (a 108 px band over rows 56 px apart, 710 px over buttons 162 px apart), as much on
 * each side of what they highlight: the pitch starts that far in. A bigger cursor (a description
 * panel) or an item's own bounds (hidden text) swell down and right. */
static void hit_rect(const MenuItem *m, const Spacing *g, float r[4]) {
    const float lo[2]={m->x0,m->y0}, hi[2]={m->x1,m->y1};
    for (int k=0;k<2;++k) {
        r[k]=lo[k]; r[k+2]=hi[k];
        if (!g->pitch[k] || hi[k]-lo[k]<=g->pitch[k]) continue;
        if (!m->whole) r[k]+=g->glow[k];
        r[k+2]=r[k]+g->pitch[k];
    }
}
static int hit(uint64_t container, float sx, float sy) {
    int found=-1, approx=0;
    float best=0, pitch_x=0, pitch_y=0;
    const Spacing g=spacing(container,0);
    for (int i=0;i<menu_item_count;++i) {
        const MenuItem *m=&menu_items[i];
        if (!pointable(m,container)) continue;
        if (m->approx) { approx=1; continue; }
        float r[4];
        hit_rect(m,&g,r);
        const float area=(r[2]-r[0])*(r[3]-r[1]);
        if (r[2]>r[0] && sx>=r[0] && sx<r[2] && sy>=r[1] && sy<r[3] && (found<0 || area<best)) { found=i; best=area; }
    }
    if (found>=0 || !approx) return found;
    for (int i=0;i<menu_item_count;++i) {
        const MenuItem *m=&menu_items[i];
        if (!pointable(m,container) || !m->approx) continue;
        for (int j=i+1;j<menu_item_count;++j) {
            const MenuItem *n=&menu_items[j];
            if (!pointable(n,container) || !n->approx) continue;
            const float dx=m->x0>n->x0 ? m->x0-n->x0 : n->x0-m->x0, dy=m->y0>n->y0 ? m->y0-n->y0 : n->y0-m->y0;
            if (dx>2 && (!pitch_x || dx<pitch_x)) pitch_x=dx;
            if (dy>2 && (!pitch_y || dy<pitch_y)) pitch_y=dy;
        }
    }
    for (int i=0;i<menu_item_count;++i) {
        const MenuItem *m=&menu_items[i];
        if (!pointable(m,container) || !m->approx) continue;
        float ex=0, ey=0;
        if (pitch_x) ex=(sx-(m->x0+pitch_x/2))/pitch_x;
        else if (sx<m->x0-100 || sx>m->x0+900) continue;
        if (pitch_y) ey=(sy-(m->y0+pitch_y/2))/pitch_y;
        else if (sy<m->y0-100 || sy>m->y0+300) continue;
        if (ex<0) ex=-ex;
        if (ey<0) ey=-ey;
        if (ex>APPROX_RADIUS || ey>APPROX_RADIUS) continue;
        if (found<0 || ex*ex+ey*ey<best) { found=i; best=ex*ex+ey*ey; }
    }
    return found;
}
/* A list showing one clip: an options row's value (on/off, a region), a list of its own beside
 * the row's name, with its own highlight and arrows. */
static int one_clip(const MenuContainer *c) {
    return c && c->list && c->list_rows<=1 && c->list_columns<=1;
}
/* A control's part on the stage; 0 for none. A list of one clip without the widget's arrows (an
 * options value draws its own in its highlight) is split at its clip's middle: the left half
 * steps back, the right half on. */
static int control_part(const Control *c, int k, float r[4]) {
    memcpy(r,c->part[k],sizeof(c->part[k]));
    if (r[2]>r[0] || c->kind!=CONTROL_VALUES || k==PART_BAR || c->part[!k][2]>c->part[!k][0]) return r[2]>r[0];
    for (int i=0;i<menu_item_count;++i) {
        const MenuItem *m=&menu_items[i];
        if (!pointable(m,c->container) || m->approx || m->x1<=m->x0) continue;
        const float mid=(m->x0+m->x1)/2;
        r[0]=k==PART_LEFT ? m->x0-VALUE_REACH : mid;
        r[2]=k==PART_LEFT ? mid : m->x1+VALUE_REACH;
        r[1]=m->y0; r[3]=m->y1;
        return 1;
    }
    return 0;
}
/* The row of a one-column list whose value the pointer is on: in the band of the row at the
 * pointer's height, within the clip of a value the game updates beside the rows (the selected
 * row's at least; the others may stand still). -1 elsewhere. */
static int value_row(const MenuContainer *rows, float sx, float sy, uint64_t now) {
    if (!rows->list || one_clip(rows) || rows->list_columns>1) return -1;
    float top=0, bottom=0, height=0;
    int row=-1, bands=0;
    const Spacing g=spacing(rows->container,0);
    for (int i=0;i<menu_item_count;++i) {
        const MenuItem *m=&menu_items[i];
        if (!pointable(m,rows->container) || m->approx) continue;
        float r[4];
        hit_rect(m,&g,r);
        if (!bands++ || r[1]<top) top=r[1];
        if (bands==1 || r[3]>bottom) bottom=r[3];
        if (sy>=r[1] && sy<r[3] && (row<0 || r[3]-r[1]<height)) { row=i; height=r[3]-r[1]; }
    }
    if (row<0) return -1;
    /* Its own value (a dropdown's current item: a click opens the list, as one on its name). */
    if (menu_items[row].value_x1>menu_items[row].value_x0 && sx>=menu_items[row].value_x0-ARROW_MARGIN &&
        sx<menu_items[row].value_x1+ARROW_MARGIN)
        return row;
    for (int c=0;c<MAX_CONTAINERS;++c) {
        const MenuContainer *v=&containers[c];
        if (v==rows || !one_clip(v) || now-v->list_ns>=LIST_LIVE_NS) continue;
        for (int i=0;i<menu_item_count;++i) {
            const MenuItem *m=&menu_items[i];
            if (!pointable(m,v->container) || m->approx || (m->y0+m->y1)/2<top || (m->y0+m->y1)/2>=bottom) continue;
            if (sx>=m->x0 && sx<m->x1) return row;
        }
    }
    /* A control's span: from its left arrow to its right one (a slider's bar between them). */
    for (int i=0;i<MAX_CONTROLS;++i) {
        const Control *c=&controls[i];
        float x0=0, x1=0, y0=0, y1=0;
        int parts=0;
        for (int k=0;k<PARTS && c->object && now-c->seen_ns<LIST_LIVE_NS;++k) {
            float r[4];
            if (!control_part(c,k,r)) continue;
            if (!parts++) { x0=r[0]; y0=r[1]; x1=r[2]; y1=r[3]; continue; }
            if (r[0]<x0) x0=r[0];
            if (r[1]<y0) y0=r[1];
            if (r[2]>x1) x1=r[2];
            if (r[3]>y1) y1=r[3];
        }
        if (parts && (y0+y1)/2>=top && (y0+y1)/2<bottom && sx>=x0-ARROW_MARGIN && sx<x1+ARROW_MARGIN) return row;
    }
    return -1;
}
/* The direction from one item to another by their names: Item_<row>_<column> in every menu seen
 * (title, in-game menu, lists, options), rows down, columns right. Their bounds would mislead:
 * a last entry's cursor can sit elsewhere (an options list's last line, to the right). */
static uint32_t toward(const MenuItem *from, const MenuItem *to) {
    if (to->a!=from->a) return to->a>from->a ? PAD_DOWN : PAD_UP;
    return to->b>from->b ? PAD_RIGHT : PAD_LEFT;
}
static int cursor_items(uint64_t container) {
    int n=0;
    for (int i=0;i<menu_item_count;++i) n+=menu_items[i].container==container && menu_items[i].cursor;
    return n;
}

/* ---- The list widget ---- */
static int32_t guest_i32(uint64_t a) { int32_t v; memcpy(&v,(const void *)(uintptr_t)a,4); return v; }
static uint64_t guest_u64(uint64_t a) { uint64_t v; memcpy(&v,(const void *)(uintptr_t)a,8); return v; }
static void list_state(uint64_t list, ListState *s) {
    s->count=guest_i32(list+LIST_COUNT);
    const int32_t override=guest_i32(list+LIST_OVERRIDE);
    s->selected=override>=0 ? override : guest_i32(list+LIST_SELECTED);
    s->columns=guest_i32(list+LIST_COLUMNS); s->rows=guest_i32(list+LIST_ROWS);
    s->first_column=guest_i32(list+LIST_FIRST_COLUMN); s->first_row=guest_i32(list+LIST_FIRST_ROW);
    s->wraps=*(const volatile uint8_t *)(uintptr_t)(list+LIST_WRAPS)!=0;
}
/* The widget's arithmetic: entries per row (one row of clips holds them all), and its wrapping. */
static int64_t list_stride(const ListState *s) {
    int64_t n=s->columns;
    if (s->rows==1 && s->count>n) n=s->count;
    return n>1 ? n : 1;
}
static int64_t list_wrap(const ListState *s, int64_t e) {
    return s->wraps && s->count>0 ? (int64_t)((uint32_t)e%(uint32_t)s->count) : e;
}
/* The entry the clip Item_<row>_<column> shows; -1 for none. */
static int list_entry(const ListState *s, int row, int column) {
    const int rows=s->rows>1 ? s->rows : 1, columns=s->columns>1 ? s->columns : 1;
    if (row<0 || row>=rows || column<0 || column>=columns) return -1;
    const int64_t e=list_wrap(s,((int64_t)s->first_row+row)*list_stride(s)+s->first_column+column);
    return e>=0 && e<s->count ? (int)e : -1;
}
/* The clip that shows an entry; 0 when it is scrolled off. */
static int list_clip(const ListState *s, int entry, int *row, int *column) {
    const int rows=s->rows>1 ? s->rows : 1, columns=s->columns>1 ? s->columns : 1;
    if (entry<0 || entry>=s->count) return 0;
    int64_t k=entry-list_wrap(s,(int64_t)s->first_row*list_stride(s)+s->first_column);
    if (s->wraps && k<0) k+=s->count;
    if (k<0 || k>=(int64_t)rows*columns) return 0;
    *row=(int)(k/columns); *column=(int)(k%columns);
    return 1;
}
typedef ABI int32_t (*SetSelectionFn)(uint64_t list, int32_t entry);
typedef ABI void (*ListenerFn)(uint64_t self, int32_t *entry);
/* Selects an entry as the update does after a key press: the widget's SetSelection (scrolling,
 * the cursor), then its listener and the cursor sound. On the game's thread, in the list's update,
 * without menu_lock (the widget calls the hooked interface). 1 when the selection moved. */
static int list_select(uint64_t list, int entry) {
    ListState before, after;
    list_state(list,&before);
    ((SetSelectionFn)(uintptr_t)(image+LIST_SET_SELECTION))(list,entry);
    list_state(list,&after);
    if (after.selected==before.selected) return 0;
    const uint64_t listener=guest_u64(list+LIST_LISTENER);
    if (listener) {
        int32_t selected=after.selected;
        ((ListenerFn)(uintptr_t)guest_u64(guest_u64(listener)+16))(listener,&selected);
    }
    unsigned char *sound=image+MENU_SOUND;
    if (guest_i32((uintptr_t)sound+4)<=0) { /* nothing more important asked for */
        const uint64_t none=0, name=(uintptr_t)image+CURSOR_SOUND;
        memcpy(sound,&none,8);
        memcpy(sound+8,&name,8);
    }
    return 1;
}
/* A list's container (the Scaleform object its clip namer holds), its state, and whether the
 * input it is updated with is on (its first byte: a parent menu behind a submenu is updated with
 * the input off); 0 for a list without a container. Guarded: a list type with another namer
 * would not fault the game. */
static int list_read(uint64_t list, uint64_t input, uint64_t *oi, uint64_t *container, ListState *s, int *focused) {
    RuntimeRecoverBuf recover;
    RuntimeRecoverBuf *previous=runtime_fault_recover;
    if (RUNTIME_RECOVER_SET(recover)) { runtime_fault_recover=previous; return 0; }
    runtime_fault_recover=&recover;
    int ok=0;
    const uint64_t namer=guest_u64(list+LIST_NAMER);
    if (namer && (guest_i32(namer+NAMER_TYPE)&0x0F)==10) {
        *oi=guest_u64(namer+NAMER_OI); *container=guest_u64(namer+NAMER_CONTAINER);
        ok=*oi && *container && guest_u64(*oi)==(uintptr_t)image+OBJECT_INTERFACE;
        if (ok) { list_state(list,s); *focused=!input || *(const volatile uint8_t *)(uintptr_t)input!=0; }
    }
    runtime_fault_recover=previous;
    return ok;
}
/* ---- Options rows' controls ---- */
static Control *control_of(uint64_t object, int add) {
    Control *oldest=&controls[0];
    for (int i=0;i<MAX_CONTROLS;++i) {
        if (controls[i].object==object) return &controls[i];
        if (controls[i].seen_ns<oldest->seen_ns) oldest=&controls[i];
    }
    if (!add) return NULL;
    *oldest=(Control){.object=object};
    return oldest;
}
/* A clip's bounds on the stage, the stage fetched from the clip (the game's thread, measuring). */
static int stage_bounds(uint64_t oi, uint64_t clip, float out[4]) {
    const GetMemberFn get=(GetMemberFn)interface_original[5];
    GfxValue stage;
    memset(&stage,0,sizeof(stage));
    const int ok=(get(oi,clip,"stage",&stage,1)&0xFF) && ((stage.type&0x0F)==10 || (stage.type&0x0F)==8) &&
                 bounds_of(oi,clip,&stage,out);
    value_release(&stage);
    return ok;
}
/* A container's child clip's bounds on the stage. */
static int child_bounds(uint64_t oi, uint64_t container, const char *name, float out[4]) {
    const GetMemberFn get=(GetMemberFn)interface_original[5];
    GfxValue child;
    memset(&child,0,sizeof(child));
    const int ok=(get(oi,container,name,&child,1)&0xFF) && (child.type&0x0F)==10 && stage_bounds(oi,child.value,out);
    value_release(&child);
    return ok;
}
/* A clip's visible flag (true when it cannot be read). */
static int clip_visible(uint64_t oi, uint64_t clip) {
    const GetMemberFn get=(GetMemberFn)interface_original[5];
    GfxValue visible;
    memset(&visible,0,sizeof(visible));
    const int read=(get(oi,clip,"visible",&visible,1)&0xFF) && (visible.type&0x0F)==2; /* a Boolean */
    const int shown=!read || (visible.value&0xFF)!=0;
    value_release(&visible);
    return shown;
}
/* An options row's value beside its name when it stands for the row: a dropdown's current item
 * (a click opens the list) or a text field (a click opens the text dialog). In optionsetting.gfx
 * a row's "Widgets" holds a TextInput, a ComboBox (a list of one clip), a Slider and a
 * DropdownList, the row's own shown; the ComboBox and the Slider are controls of their own. */
static int row_value(uint64_t oi, uint64_t item, float out[4]) {
    static const char *const kinds[][2]={{"DropdownList","CurrentItem"},{"TextInput","Text_0"}};
    const GetMemberFn get=(GetMemberFn)interface_original[5];
    GfxValue widgets;
    memset(&widgets,0,sizeof(widgets));
    int ok=0;
    if ((get(oi,item,"Widgets",&widgets,1)&0xFF) && (widgets.type&0x0F)==10)
        for (size_t k=0;k<sizeof(kinds)/sizeof(*kinds) && !ok;++k) {
            GfxValue widget;
            memset(&widget,0,sizeof(widget));
            if ((get(oi,widgets.value,kinds[k][0],&widget,1)&0xFF) && (widget.type&0x0F)==10 && clip_visible(oi,widget.value))
                ok=child_bounds(oi,widget.value,kinds[k][1],out) && out[2]-out[0]>4;
            value_release(&widget);
        }
    value_release(&widgets);
    return ok;
}
/* A cursor-only row's number: in levelup.gfx the list "Control" and the panels "Status_0" and
 * "Status_1" are its parent's children; the attributes' rows there (Item_<n>: Text_0, Value_0) are
 * drawn under the list's rows. The panels' first Value_0 at the row's height, by its middle. */
#define PANEL_ROWS 16
static int row_number(uint64_t oi, uint64_t container, const float row[4], float out[4]) {
    static const char *const panels[]={"Status_0","Status_1"};
    const GetMemberFn get=(GetMemberFn)interface_original[5];
    GfxValue parent;
    memset(&parent,0,sizeof(parent));
    int ok=0;
    if ((get(oi,container,"parent",&parent,1)&0xFF) && (parent.type&0x0F)==10)
        for (size_t p=0;p<sizeof(panels)/sizeof(*panels) && !ok;++p) {
            GfxValue panel;
            memset(&panel,0,sizeof(panel));
            if ((get(oi,parent.value,panels[p],&panel,1)&0xFF) && (panel.type&0x0F)==10)
                for (int i=0;i<PANEL_ROWS && !ok;++i) {
                    char name[16];
                    snprintf(name,sizeof(name),"Item_%d",i);
                    GfxValue line;
                    memset(&line,0,sizeof(line));
                    float r[4];
                    if ((get(oi,panel.value,name,&line,1)&0xFF) && (line.type&0x0F)==10 && child_bounds(oi,line.value,"Value_0",r) &&
                        (r[1]+r[3])/2>=row[1] && (r[1]+r[3])/2<row[3] && r[2]-r[0]>4) {
                        memcpy(out,r,sizeof(r));
                        ok=1;
                    }
                    value_release(&line);
                }
            value_release(&panel);
        }
    value_release(&parent);
    return ok;
}
/* Measured parts into a control (none where a measurement failed). */
static void control_parts(uint64_t object, const float part[PARTS][4], const int ok[PARTS]) {
    host_lock(&menu_lock);
    Control *c=control_of(object,0);
    for (int k=0;c && k<PARTS;++k) {
        if (ok[k]) memcpy(c->part[k],part[k],sizeof(c->part[k]));
        else memset(c->part[k],0,sizeof(c->part[k]));
    }
    host_unlock(&menu_lock);
}
typedef ABI void (*SetValueFn)(uint64_t slider, int32_t value, int32_t notify, uint64_t sound, uint64_t name);
/* A slider gauge's track, measured: in optionsetting.gfx the "Bar" clip (100 frames) holds a
 * background (child 0, 311x14 around the track), a mask (child 1, 305x7, sliding from all left of
 * the fill at frame 1 to over it at frame 100) and the fill it uncovers (child 2, 305x7, still): the
 * value's range. The fill's bounds are its whole length (the mask only hides it); the clip's own
 * bounds hold the mask too, wherever it is. 0 for a gauge built otherwise. */
#define GAUGE_CHILDREN 3
#define GAUGE_FILL 2
static int gauge_track(uint64_t oi, uint64_t bar, const float bounds[4], float out[4]) {
    const GetMemberFn get=(GetMemberFn)interface_original[5];
    const InvokeFn invoke=(InvokeFn)interface_original[7];
    GfxValue count, index, fill;
    memset(&count,0,sizeof(count)); memset(&index,0,sizeof(index)); memset(&fill,0,sizeof(fill));
    double children=0;
    const int built=(get(oi,bar,"numChildren",&count,1)&0xFF) && number_of(&count,&children) && children==GAUGE_CHILDREN;
    value_release(&count);
    if (!built) return 0;
    const double at=GAUGE_FILL;
    index.type=5; /* a Number */
    memcpy(&index.value,&at,8);
    const int ok=(invoke(oi,bar,&fill,"getChildAt",&index,1,1)&0xFF) && (fill.type&0x0F)==10 && stage_bounds(oi,fill.value,out);
    value_release(&fill);
    return ok && out[2]-out[0]>16 && out[0]>=bounds[0]-1 && out[2]<=bounds[2]+1;
}
static const int slider_holders[PARTS]={SLIDER_LEFT,SLIDER_RIGHT,SLIDER_BAR};
/* A slider's state and its clips (0 for one that is not a display object of this interface), and
 * whether its clip holders are this class's (another layout is left alone). Guarded, as list_read. */
static int slider_read(uint64_t slider, uint64_t input, Control *out, uint64_t *oi, uint64_t clip[PARTS]) {
    RuntimeRecoverBuf recover;
    RuntimeRecoverBuf *previous=runtime_fault_recover;
    if (RUNTIME_RECOVER_SET(recover)) { runtime_fault_recover=previous; return 0; }
    runtime_fault_recover=&recover;
    const uint64_t holder=(uintptr_t)image+CLIP_HOLDER;
    const int ok=guest_u64(slider+SLIDER_BAR)==holder && guest_u64(slider+SLIDER_LEFT)==holder &&
                 guest_u64(slider+SLIDER_RIGHT)==holder;
    if (ok) {
        out->min=guest_i32(slider+SLIDER_MIN); out->max=guest_i32(slider+SLIDER_MAX);
        out->value=guest_i32(slider+SLIDER_VALUE); out->step=guest_i32(slider+SLIDER_STEP);
        out->focused=!input || *(const volatile uint8_t *)(uintptr_t)input!=0;
        for (int k=0;k<PARTS;++k) {
            const uint64_t h=slider+slider_holders[k], held=guest_u64(h+HOLDER_OI);
            clip[k]=guest_u64(h+HOLDER_CLIP);
            if (!held || !clip[k] || (guest_i32(h+HOLDER_TYPE)&0x0F)!=10 || guest_u64(held)!=(uintptr_t)image+OBJECT_INTERFACE) clip[k]=0;
            else *oi=held;
        }
    }
    runtime_fault_recover=previous;
    return ok;
}
/* The hook at the start of every slider update (the game's thread): its state and its clips'
 * bounds; a value the pad thread asked for, once its row is the selected one (the game's
 * SetValue, as the update calls it after a key press: the listener applies it, the sound plays). */
static ABI void slider_update(uint64_t slider, uint64_t input) {
    if (!menu_mouse) return;
    const uint64_t now=host_monotonic_ns();
    Control s;
    uint64_t oi=0, clip[PARTS]={0,0,0};
    if (!slider_read(slider,input,&s,&oi,clip)) return;
    int value=0;
    host_lock(&menu_lock);
    Control *c=control_of(slider,1);
    const int fresh=!c->seen_ns || now-c->seen_ns>=LIST_LIVE_NS;
    c->kind=CONTROL_SLIDER; c->container=0; c->focused=s.focused; c->seen_ns=now;
    c->min=s.min; c->max=s.max; c->value=s.value; c->step=s.step;
    const int measure_now=!bounds_broken && (!c->bounds_ns || now-c->bounds_ns>BOUNDS_NS);
    if (measure_now) c->bounds_ns=now;
    const int take=slider_state==JUMP_POSTED && slider_target==slider && s.focused;
    if (take) { value=slider_relative ? s.value+slider_value*(s.step>0 ? s.step : 1) : slider_value; slider_state=JUMP_TAKEN; }
    int first=0;
    for (int i=0;fresh && measure_now && i<MAX_LIST_NOTES;++i) {
        if (noted_lists[i]==slider) break;
        if (!noted_lists[i]) { noted_lists[i]=slider; first=1; break; }
    }
    host_unlock(&menu_lock);
    if (fresh) menu_note("slider %" PRIx64 ": %d in %d..%d by %d%s",slider,s.value,s.min,s.max,s.step,s.focused ? ", focused" : "");
    if (measure_now) {
        float part[PARTS][4]={{0}};
        int ok[PARTS];
        measuring=1;
        for (int k=0;k<PARTS;++k) ok[k]=clip[k] && stage_bounds(oi,clip[k],part[k]);
        float *bar=part[PART_BAR], track[4];
        const int measured=ok[PART_BAR] && gauge_track(oi,clip[PART_BAR],bar,track);
        measuring=0;
        if (measured) { bar[0]=track[0]; bar[2]=track[2]; } /* its height: the clip's (the fill is 7 px) */
        else if (ok[PART_BAR] && s.max>s.min) {
            /* Another gauge: as that one, the clip's bounds reach left of the track by the mask's
             * share still off it, (1 - full) of the track's length. */
            const float full=(float)(s.value-s.min)/(float)(s.max-s.min), length=(bar[2]-bar[0])/(2-full);
            if (full>=0 && full<=1 && length>8) bar[0]=bar[2]-length;
        }
        control_parts(slider,(const float (*)[4])part,ok);
        if (fresh || !ok[PART_BAR])
            menu_note("slider %" PRIx64 ": bar (%.0f,%.0f)-(%.0f,%.0f), arrows %.0f..%.0f and %.0f..%.0f",slider,
                      part[PART_BAR][0],part[PART_BAR][1],part[PART_BAR][2],part[PART_BAR][3],
                      part[PART_LEFT][0],part[PART_LEFT][2],part[PART_RIGHT][0],part[PART_RIGHT][2]);
        if (first)
            printf("Menu mouse: slider %" PRIx64 " (%d in %d..%d), track (%.0f,%.0f)-(%.0f,%.0f) %s, arrows %.0f..%.0f and %.0f..%.0f\n",
                   slider,s.value,s.min,s.max,bar[0],bar[1],bar[2],bar[3],measured ? "measured" : "estimated",
                   part[PART_LEFT][0],part[PART_LEFT][2],part[PART_RIGHT][0],part[PART_RIGHT][2]);
    }
    if (take) {
        ((SetValueFn)(uintptr_t)(image+SLIDER_SET_VALUE))(slider,value,1,SLIDER_SOUND_ID,(uintptr_t)image+SLIDER_SOUND);
        const int32_t now_value=guest_i32(slider+SLIDER_VALUE);
        host_lock(&menu_lock);
        slider_state=JUMP_DONE;
        if ((c=control_of(slider,0))) c->value=now_value;
        host_unlock(&menu_lock);
        menu_note("slider %" PRIx64 " set to %d (asked %d)",slider,now_value,value);
    }
}
static int control_in_front(const Control *c, uint64_t now, int by_focus);
/* A spinner's state and its number's clip (0 when it is not a display object of this interface),
 * and whether it is this class (its vtable, its number's holder). Guarded, as list_read. */
static int spinner_read(uint64_t spinner, uint64_t input, Control *out, uint64_t *oi, uint64_t *clip) {
    RuntimeRecoverBuf recover;
    RuntimeRecoverBuf *previous=runtime_fault_recover;
    if (RUNTIME_RECOVER_SET(recover)) { runtime_fault_recover=previous; return 0; }
    runtime_fault_recover=&recover;
    const uint64_t h=spinner+SPINNER_NUMBER;
    const int ok=guest_u64(spinner)==(uintptr_t)image+SPINNER_VTABLE && guest_u64(h)==(uintptr_t)image+CLIP_HOLDER;
    if (ok) {
        out->min=guest_i32(spinner+SPINNER_MIN); out->max=guest_i32(spinner+SPINNER_MAX);
        out->value=guest_i32(spinner+SPINNER_VALUE); out->step=guest_i32(spinner+SPINNER_STEP);
        out->focused=!input || *(const volatile uint8_t *)(uintptr_t)input!=0;
        *oi=guest_u64(h+HOLDER_OI); *clip=guest_u64(h+HOLDER_CLIP);
        if (!*oi || !*clip || (guest_i32(h+HOLDER_TYPE)&0x0F)!=10 || guest_u64(*oi)!=(uintptr_t)image+OBJECT_INTERFACE) *clip=0;
    }
    runtime_fault_recover=previous;
    return ok;
}
/* The hook at the start of every spinner update (the game's thread): its state, and its arrows'
 * areas from its number's bounds (the arrows are clips without a name, just above and below it):
 * above the number's middle steps up, below it down. A click presses the d-pad (menu_pad). */
static ABI void spinner_update(uint64_t spinner, uint64_t input) {
    if (!menu_mouse) return;
    const uint64_t now=host_monotonic_ns();
    Control s;
    uint64_t oi=0, clip=0;
    if (!spinner_read(spinner,input,&s,&oi,&clip)) return;
    host_lock(&menu_lock);
    Control *c=control_of(spinner,1);
    const int fresh=!c->seen_ns || now-c->seen_ns>=LIST_LIVE_NS;
    c->kind=CONTROL_SPINNER; c->container=0; c->focused=s.focused; c->seen_ns=now;
    c->min=s.min; c->max=s.max; c->value=s.value; c->step=s.step;
    const int measure_now=!bounds_broken && (!c->bounds_ns || now-c->bounds_ns>BOUNDS_NS);
    if (measure_now) c->bounds_ns=now;
    int first=0;
    for (int i=0;fresh && measure_now && i<MAX_LIST_NOTES;++i) {
        if (noted_lists[i]==spinner) break;
        if (!noted_lists[i]) { noted_lists[i]=spinner; first=1; break; }
    }
    host_unlock(&menu_lock);
    if (!measure_now) return;
    float number[4]={0,0,0,0}, part[PARTS][4]={{0}};
    measuring=1;
    const int measured=clip && stage_bounds(oi,clip,number) && number[2]>number[0];
    measuring=0;
    const int ok[PARTS]={measured,measured,0};
    if (measured) {
        const float mx=(number[0]+number[2])/2, my=(number[1]+number[3])/2;
        const float up[4]={mx-SPINNER_REACH_X,number[1]-SPINNER_REACH_Y,mx+SPINNER_REACH_X,my};
        const float down[4]={mx-SPINNER_REACH_X,my,mx+SPINNER_REACH_X,number[3]+SPINNER_REACH_Y};
        memcpy(part[PART_RIGHT],up,sizeof(up));
        memcpy(part[PART_LEFT],down,sizeof(down));
    }
    control_parts(spinner,(const float (*)[4])part,ok);
    if (fresh)
        menu_note("spinner %" PRIx64 ": %d in %d..%d by %d, number (%.0f,%.0f)-(%.0f,%.0f)%s",spinner,s.value,s.min,s.max,s.step,
                  number[0],number[1],number[2],number[3],s.focused ? ", focused" : "");
    if (first)
        printf("Menu mouse: spinner %" PRIx64 " (%d in %d..%d), number (%.0f,%.0f)-(%.0f,%.0f)%s\n",spinner,s.value,s.min,s.max,
               number[0],number[1],number[2],number[3],measured ? "" : ", not measured");
}
/* The control part under a stage point: a live control's arrow (a little beyond its glyph), else
 * its bar (a little above and below it too: the track is 14 px high), on the screen in front; -1
 * for none. */
static int control_at(float sx, float sy, uint64_t now, int by_focus, int *part) {
    for (int k=0;k<PARTS;++k)
        for (int i=0;i<MAX_CONTROLS;++i) {
            const Control *c=&controls[i];
            const float m=k==PART_BAR ? 0 : ARROW_MARGIN;
            float r[4];
            if (!c->object || now-c->seen_ns>=LIST_LIVE_NS || !control_part(c,k,r) || !control_in_front(c,now,by_focus)) continue;
            if (sx>=r[0]-m && sx<r[2]+m && sy>=r[1]-ARROW_MARGIN && sy<r[3]+ARROW_MARGIN) { *part=k; return i; }
        }
    return -1;
}
/* A slider's value at a point of its bar: the bar spans min..max. */
static int bar_value(const Control *c, float sx) {
    const float *r=c->part[PART_BAR];
    float f=(sx-r[0])/(r[2]-r[0]);
    if (f<0) f=0;
    if (f>1) f=1;
    const int step=c->step>0 ? c->step : 1;
    return c->min+(int)(f*(float)((c->max-c->min)/step)+0.5f)*step;
}
/* The hook at the start of every list update (the game's thread): the list's container and
 * selection; a jump the pad thread asked for; its container's items found and measured. */
static ABI void list_update(uint64_t list, uint64_t input) {
    if (!menu_mouse) return;
    const uint64_t now=host_monotonic_ns();
    uint64_t oi, container;
    ListState s;
    int focused=1;
    if (!list_read(list,input,&oi,&container,&s,&focused)) return;
    int row, column, a=0, b=0, entry=-1, first=0;
    host_lock(&menu_lock);
    MenuContainer *c=container_of(container,1);
    const int fresh=c->list!=list || now-c->list_ns>=LIST_LIVE_NS;
    if (fresh) c->active_ns=now; /* (back) in front: a submenu over the menu that is still drawn */
    c->list=list; c->list_ns=c->touch_ns=now; c->behind=0; c->unanswered=0; c->list_rows=s.rows; c->list_columns=s.columns;
    if (focused!=c->focused) {
        menu_note("list %" PRIx64 " (container %" PRIx64 ") %s",list,container,focused ? "takes the input" : "updated without input");
        c->focused=focused;
    }
    const int selected=list_clip(&s,s.selected,&row,&column) ? item_add(oi,container,row,column,now) : -1;
    if (selected!=c->selected) { c->selected=selected; c->select_ns=c->active_ns=now; }
    for (int i=0;i<menu_item_count;++i) /* clips past the entries: hidden, maybe over the shown ones */
        if (menu_items[i].container==container) menu_items[i].absent=list_entry(&s,menu_items[i].a,menu_items[i].b)<0;
    const int take=jump_state==JUMP_POSTED && jump_list==list;
    if (take) { a=jump_a; b=jump_b; entry=list_entry(&s,a,b); jump_state=JUMP_TAKEN; }
    const int probe_now=!c->probe_ns || now-c->probe_ns>PROBE_NS;
    if (probe_now) c->probe_ns=now;
    /* A list of one clip, an options row's value: a control with arrows. */
    int arrows_now=0;
    if (s.rows<=1 && s.columns<=1) {
        Control *v=control_of(list,1);
        v->kind=CONTROL_VALUES; v->container=container; v->focused=focused; v->seen_ns=now;
        arrows_now=!bounds_broken && (!v->bounds_ns || now-v->bounds_ns>BOUNDS_NS);
        if (arrows_now && !v->bounds_ns) arrows_now=2; /* the first time: printed */
        if (arrows_now) v->bounds_ns=now;
    }
    if (fresh) {
        for (int i=0;i<MAX_LIST_NOTES;++i) {
            if (noted_lists[i]==list) break;
            if (!noted_lists[i]) { noted_lists[i]=list; first=1; break; } /* full: printed no more */
        }
    }
    host_unlock(&menu_lock);
    if (fresh) {
        menu_note("list %" PRIx64 " in front: container %" PRIx64 ", %d entries, %d columns x %d rows, selected %d",
                  list,container,s.count,s.columns,s.rows,s.selected);
        if (first) printf("Menu mouse: list %" PRIx64 " (%d entries, %d columns x %d rows)\n",list,s.count,s.columns,s.rows);
    }
    if (take) {
        const int moved=entry>=0 && list_select(list,entry);
        list_state(list,&s);
        host_lock(&menu_lock);
        c=container_of(container,1);
        const int now_selected=list_clip(&s,s.selected,&row,&column) ? item_add(oi,container,row,column,now) : -1;
        if (now_selected!=c->selected) { c->selected=now_selected; c->select_ns=c->active_ns=now; }
        jump_moved=moved; jump_state=JUMP_DONE;
        host_unlock(&menu_lock);
        menu_note("jump to Item_%d_%d: entry %d of %d, %s",a,b,entry,s.count,moved ? "selected" : "not selectable");
    }
    if (arrows_now) {
        static const char *const names[PARTS]={"LeftItem_0","RightItem_0",NULL};
        float part[PARTS][4]={{0}};
        int ok[PARTS]={0,0,0};
        measuring=1;
        for (int k=0;k<PART_BAR;++k) ok[k]=child_bounds(oi,container,names[k],part[k]);
        measuring=0;
        control_parts(list,(const float (*)[4])part,ok);
        if (arrows_now==2 && first)
            printf("Menu mouse: list %" PRIx64 " arrows %.0f..%.0f and %.0f..%.0f at %.0f%s\n",list,part[PART_LEFT][0],part[PART_LEFT][2],
                   part[PART_RIGHT][0],part[PART_RIGHT][2],part[PART_LEFT][1],ok[PART_LEFT] && ok[PART_RIGHT] ? "" : " (none: its clip's halves)");
    }
    scan_container(oi,container,probe_now,!bounds_broken,selected,now);
}
/* Rewrites guest code (the image's code pages are read+execute). */
static int patch_code(uint32_t offset, const void *bytes, size_t size) {
    unsigned char *p=image+offset;
    uintptr_t page=(uintptr_t)p&~(uintptr_t)4095, end=((uintptr_t)p+size+4095)&~(uintptr_t)4095;
#ifdef _WIN32
    DWORD old;
    if (!VirtualProtect((void *)page,end-page,PAGE_EXECUTE_READWRITE,&old)) return -1;
    memcpy(p,bytes,size);
    VirtualProtect((void *)page,end-page,old,&old);
    FlushInstructionCache(GetCurrentProcess(),p,size);
#else
    if (mprotect((void *)page,end-page,PROT_READ|PROT_WRITE|PROT_EXEC)) return -1;
    memcpy(p,bytes,size);
    mprotect((void *)page,end-page,PROT_READ|PROT_EXEC);
#endif
    return 0;
}
/* Hooks a guest function: its prologue (whole instructions without rip-relative operands, at most
 * TRAMPOLINE_SLOT bytes) becomes a jump to hook code at t (in the trace's page) that calls
 * hook(rdi, rsi), runs the prologue and goes on after it. */
typedef ABI void (*HookFn)(uint64_t, uint64_t);
static int hook_function(unsigned char *t, uint32_t offset, size_t prologue, HookFn hook) {
    const uint64_t to=(uint64_t)(uintptr_t)hook, back=(uint64_t)(uintptr_t)(image+offset+prologue);
    memcpy(t,hook_trampoline,sizeof(hook_trampoline));
    memcpy(t+TRAMPOLINE_HOOK,&to,8);
    memset(t+TRAMPOLINE_PROLOGUE,0x90,TRAMPOLINE_SLOT);
    memcpy(t+TRAMPOLINE_PROLOGUE,image+offset,prologue);
    memcpy(t+TRAMPOLINE_BACK,&back,8);
    uint8_t jump[TRAMPOLINE_SLOT]={0xFF,0x25,0x00,0x00,0x00,0x00};   /* jmp [rip+0]; the hook's code */
    const uint64_t target=(uint64_t)(uintptr_t)t;
    memcpy(jump+6,&target,8);
    memset(jump+14,0x90,sizeof(jump)-14);
    return patch_code(offset,jump,prologue);
}
/* Hooks the list update and the slider update (their code in the trace's page). */
static void list_configure(unsigned char *code) {
    if (memcmp(image+LIST_UPDATE,list_update_code,sizeof(list_update_code)) ||
        memcmp(image+LIST_SET_SELECTION,list_set_selection_code,sizeof(list_set_selection_code)) ||
        memcmp(image+CURSOR_SOUND,cursor_sound,sizeof(cursor_sound))) {
        puts("Menu mouse: not the v1.09 list widget (menus take d-pad steps)");
        return;
    }
    if (hook_function(code+TRAMPOLINE_AT,LIST_UPDATE,LIST_PROLOGUE,list_update)) {
        puts("Menu mouse: cannot hook the list widget (menus take d-pad steps)");
        return;
    }
    listable=1;
    if (memcmp(image+SLIDER_UPDATE,slider_update_code,sizeof(slider_update_code)) ||
        memcmp(image+SLIDER_SET_VALUE,slider_set_value_code,sizeof(slider_set_value_code)) ||
        memcmp(image+SLIDER_SOUND,slider_sound,sizeof(slider_sound))) {
        puts("Menu mouse: not the v1.09 slider (sliders take the keys only)");
        return;
    }
    slidable=!hook_function(code+TRAMPOLINE_AT+TRAMPOLINE_SIZE,SLIDER_UPDATE,SLIDER_PROLOGUE,slider_update);
    if (memcmp(image+SPINNER_UPDATE,spinner_update_code,sizeof(spinner_update_code))) {
        puts("Menu mouse: not the v1.09 spinner (quantities take the keys only)");
        return;
    }
    hook_function(code+TRAMPOLINE_AT+2*TRAMPOLINE_SIZE,SPINNER_UPDATE,SPINNER_PROLOGUE,spinner_update);
}
/* The menus' 1920x1080 stage in the window (points): the presenter fits the game's 16:9 picture
 * into it, centred, with bars beside a wider one or above and below a taller one (FitImage). */
static float stage_scale(int window_w, int window_h, float *x, float *y) {
    const float sw=(float)window_w/STAGE_W, sh=(float)window_h/STAGE_H, scale=sw<sh ? sw : sh;
    *x=((float)window_w-STAGE_W*scale)/2; *y=((float)window_h-STAGE_H*scale)/2;
    return scale;
}
static void stage_point(float pointer_x, float pointer_y, int window_w, int window_h, float *sx, float *sy) {
    float x, y;
    const float scale=stage_scale(window_w,window_h,&x,&y);
    *sx=(pointer_x-x)/scale; *sy=(pointer_y-y)/scale;
}
static void window_point(float sx, float sy, int window_w, int window_h, float *pointer_x, float *pointer_y) {
    float x, y;
    const float scale=stage_scale(window_w,window_h,&x,&y);
    *pointer_x=x+sx*scale; *pointer_y=y+sy*scale;
}
/* Pad thread: whether a menu is on screen (the window frees the mouse), and the buttons the
 * pointer adds. A moving pointer makes the item under it the target, reached with d-pad presses
 * toward it; keys and the wheel stay free otherwise (a resting pointer does not pull the
 * selection back).
 * The left button targets the item under the pointer and confirms on it; the right one cancels. */
/* A list the game updates now with the input on (by_focus: when any list has it; else every
 * list it updates), alone or sharing the screen with another (an inventory's tab bar and its
 * items). A parent menu still updated behind a submenu has the input off. */
static int input_list(const MenuContainer *c, uint64_t now, int by_focus) {
    return c && c->list && now-c->list_ns<LIST_LIVE_NS && !c->behind && (!by_focus || c->focused);
}
/* A control on the screen in front: its row is the selected one (its update has the input), or it
 * stands beside a row of a list that has the input. Not one under a box over its screen (the
 * game updates it still, without the input): a yes/no box's buttons over an options screen's
 * sliders are the box's. */
static int control_in_front(const Control *c, uint64_t now, int by_focus) {
    if (c->focused) return 1;
    float y0=0, y1=0;
    int parts=0;
    for (int k=0;k<PARTS;++k) {
        float r[4];
        if (!control_part(c,k,r)) continue;
        if (!parts++ || r[1]<y0) y0=r[1];
        if (parts==1 || r[3]>y1) y1=r[3];
    }
    const float middle=(y0+y1)/2;
    for (int i=0;parts && i<MAX_CONTAINERS;++i) {
        const MenuContainer *rows=&containers[i];
        if (!input_list(rows,now,by_focus) || one_clip(rows) || rows->list_columns>1) continue;
        const Spacing g=spacing(rows->container,0);
        for (int j=0;j<menu_item_count;++j) {
            float r[4];
            if (!pointable(&menu_items[j],rows->container) || menu_items[j].approx) continue;
            hit_rect(&menu_items[j],&g,r);
            if (middle>=r[1] && middle<r[3]) return 1;
        }
    }
    return 0;
}
/* An item the pointer may act on: in the menu on top, or in any input list beside it. */
static int pointable_item(int item, const MenuContainer *menu, uint64_t now, int by_focus) {
    if (item<0 || item>=menu_item_count) return 0;
    if (menu_items[item].container==menu->container) return 1;
    return input_list(menu,now,by_focus) && input_list(container_of(menu_items[item].container,0),now,by_focus);
}
static uint32_t menu_pad(float pointer_x, float pointer_y, int window_w, int window_h, uint32_t free_buttons) {
    const uint64_t now=host_monotonic_ns();
    uint32_t out=0;
    host_lock(&menu_lock);
    /* The menu on screen: the container whose selection moved last, among those touched lately. */
    MenuContainer *menu=NULL;
    int menus=0, live_lists=0, by_focus=0;
    for (int i=0;i<MAX_CONTAINERS;++i) by_focus|=input_list(&containers[i],now,0) && containers[i].focused;
    for (int i=0;i<MAX_CONTAINERS;++i) {
        MenuContainer *c=&containers[i];
        if (c->behind==COVERED) { if (c->select_ns>c->covered_ns) c->behind=0; }
        else if (c->behind) { /* back in front once the container that took the focus has closed */
            const MenuContainer *front=container_of(c->behind,0);
            if (!front || now-front->touch_ns>=MENU_IDLE_NS) c->behind=0;
        }
        /* A menu: items with cursors (a list's, or a selection seen); the HUD's lists have none. */
        const int items=cursor_items(c->container);
        if (!c->container || now-c->touch_ns>=MENU_IDLE_NS || (c->list ? items<1 : (c->selected<0 && items<2))) continue;
        ++menus;
        live_lists+=input_list(c,now,by_focus);
        /* Its list is no longer updated (covered), or updated without the input (a submenu over it). */
        const int lost=c->list && (now-c->list_ns>=LIST_LIVE_NS || (by_focus && !c->focused));
        if (!c->behind && !lost && (!menu || c->active_ns>menu->active_ns)) menu=c;
    }
    /* A press aimed at one container that another one answered: that one has the focus. */
    if (press_container && menu && menu->container!=press_container && menu->select_ns>press_ns) {
        MenuContainer *aimed=container_of(press_container,0);
        if (aimed) {
            aimed->behind=menu->container;
            menu_note("container %" PRIx64 " answered a press aimed at %" PRIx64 ": it has the focus",menu->container,press_container);
        }
        press_container=0;
    }
    const int shown=menus>0; /* the mouse stays free over a covered menu, but takes no steps there */
    int hovered=-1, control=-1, part=0;
    float sx=0;
    const int selected=menu ? menu->selected : -1;
    if (menu && window_w>0 && window_h>0) {
        float sy;
        stage_point(pointer_x,pointer_y,window_w,window_h,&sx,&sy);
        hovered=hit(menu->container,sx,sy);
        control=control_at(sx,sy,now,by_focus,&part);
        /* Lists sharing the screen: the pointer works in each one the game updates. */
        for (int i=0;i<MAX_CONTAINERS && hovered<0 && input_list(menu,now,by_focus);++i)
            if (&containers[i]!=menu && input_list(&containers[i],now,by_focus)) hovered=hit(containers[i].container,sx,sy);
        /* An options row's value stands for its row (beside the row's name, highlighted with it). */
        if (hovered<0 || one_clip(container_of(menu_items[hovered].container,0)))
            for (int i=0;i<MAX_CONTAINERS;++i) {
                const int row=&containers[i]==menu || input_list(&containers[i],now,by_focus) ? value_row(&containers[i],sx,sy,now) : -1;
                if (row>=0) { hovered=row; break; }
            }
        /* A spinner's arrows have the pointer: a box's buttons under them are not chosen. */
        if (control>=0 && controls[control].kind==CONTROL_SPINNER) hovered=-1;
    }
    int moved=pointer_x!=last_pointer_x || pointer_y!=last_pointer_y;
    last_pointer_x=pointer_x; last_pointer_y=pointer_y;
    if (shown!=menu_shown) {
        menu_shown=shown;
        /* The pointer starts on the selected item, where the highlight is. */
        float wx=-1, wy=-1;
        if (shown && selected>=0 && menu_items[selected].bounds_ns && window_w>0 && window_h>0) {
            float r[4];
            const Spacing g=spacing(menu->container,menu_items[selected].approx);
            hit_rect(&menu_items[selected],&g,r);
            if (menu_items[selected].approx) { r[2]=r[0]+g.pitch[0]; r[3]=r[1]+g.pitch[1]; }
            window_point((r[0]+r[2])/2,(r[1]+r[3])/2,window_w,window_h,&wx,&wy);
            last_pointer_x=wx; last_pointer_y=wy;
        }
        bbgpu_menu_active(shown,wx,wy);
        press_until=settle_until=0; blocked=nav_target=confirm_target=action_control=step_item=-1; steps=dragging=0;
        if (jump_state!=JUMP_TAKEN) jump_state=JUMP_IDLE;
        if (slider_state!=JUMP_TAKEN) slider_state=JUMP_IDLE;
        moved=0; /* where the pointer rests when a menu opens is not a choice */
        if (menu) menu_note("on screen (container %" PRIx64 ")%s",menu->container,wx>=0 ? ", pointer on the selection" : "");
    }
    uint32_t pressed=free_buttons&~previous_free;
    previous_free=free_buttons;
    if (!shown || !menu) { host_unlock(&menu_lock); return 0; }
    /* A list jumps to the item under a moving pointer, whichever list it is in: the mouse first,
     * so a move within the item a key or wheel step left brings the selection back. A resting
     * pointer pulls nothing back (a jump may scroll other entries under it; the wheel steps
     * through a long list). */
    const int direct=input_list(menu,now,by_focus);
    /* A click on a control (an arrow, a slider's bar) selects its row and presses nothing: the
     * step or the value goes to the control once its row is the selected one (a spinner has no
     * row: a box's buttons under it are not chosen). Held on a bar, the button drags along it (the
     * rows under the pointer meanwhile are not chosen). */
    if (!(free_buttons&1)) dragging=0;
    if (direct && (pressed&1) && control>=0) {
        const Control *c=&controls[control];
        action_control=control; action_object=c->object; action_part=part; action_ns=now;
        action_value=part==PART_BAR ? bar_value(c,sx) : part==PART_LEFT ? -1 : 1;
        dragging=part==PART_BAR;
        pressed&=~1u;
        if (hovered>=0 && c->kind!=CONTROL_SPINNER) nav_target=hovered;
        confirm_target=-1;
    } else if (direct && dragging && moved) {
        Control *c=control_of(action_object,0);
        if (c && c->part[PART_BAR][2]>c->part[PART_BAR][0] && bar_value(c,sx)!=c->value) {
            action_control=(int)(c-controls); action_part=PART_BAR; action_value=bar_value(c,sx); action_ns=now;
        }
    }
    if (direct) { if (!dragging && moved && hovered>=0 && hovered!=blocked) nav_target=hovered; }
    else if (moved && hovered>=0 && hovered!=nav_target && hovered!=blocked) { nav_target=hovered; steps=visited_count=0; }
    if (hovered!=blocked) blocked=-1;
    if ((pressed&1) && hovered>=0) {
        const MenuItem *h=&menu_items[hovered];
        nav_target=hovered; steps=visited_count=0;
        confirm_target=h->select_only ? -1 : hovered;
        /* A level-up's attribute's number: its left half lowers it, its right half raises it. */
        if (h->select_only && h->step_x1>h->step_x0 && sx>=h->step_x0-VALUE_REACH && sx<h->step_x1+VALUE_REACH) {
            step_item=hovered; step_ns=now;
            step_button=sx<(h->step_x0+h->step_x1)/2 ? PAD_LEFT : PAD_RIGHT;
        }
    }
    if (!pointable_item(nav_target,menu,now,by_focus)) nav_target=-1;
    if (!pointable_item(confirm_target,menu,now,by_focus)) confirm_target=-1;
    /* The list the target is in (the menu on top, or a live list beside it). */
    const int target=nav_target>=0 ? nav_target : confirm_target;
    MenuContainer *lc=direct && target>=0 ? container_of(menu_items[target].container,0) : NULL;
    if (!lc) lc=menu;
    if (press_until) {
        if (now<press_until) { out=press_button; goto done; }
        gap_until=press_until+GAP_NS; /* from the press's end, however late this sample is */
        if (press_button&(PAD_UP|PAD_DOWN|PAD_LEFT|PAD_RIGHT)) settle_until=press_until+SETTLE_NS;
        press_until=0;
    }
    if (now<gap_until || now-lc->select_ns<REFRESH_NS) goto done;
    if (direct) {
        if (jump_state==JUMP_DONE) {
            jump_state=JUMP_IDLE;
            const MenuContainer *jumped=jump_target>=0 ? container_of(menu_items[jump_target].container,0) : NULL;
            if (!jump_moved && jump_target>=0 && (!jumped || jump_target!=jumped->selected)) { /* an entry it cannot select */
                blocked=jump_target;
                if (nav_target==jump_target) nav_target=-1;
                if (confirm_target==jump_target) confirm_target=-1;
            }
        } else if (jump_state==JUMP_POSTED && now-jump_ns>LIST_LIVE_NS) jump_state=JUMP_IDLE; /* its list went */
        if (slider_state==JUMP_DONE || (slider_state==JUMP_POSTED && now-slider_ns>LIST_LIVE_NS)) slider_state=JUMP_IDLE;
        if (nav_target>=0 && nav_target==lc->selected) nav_target=-1;
        /* A click on a cursor-only row's number, once the row is selected. */
        if (step_item>=0) {
            if (now-step_ns>ACTION_NS || menu_items[step_item].container!=lc->container) step_item=-1;
            else if (lc->selected==step_item && nav_target<0 && jump_state==JUMP_IDLE) {
                press_button=step_button; press_until=now+PRESS_NS;
                menu_note("Item_%d_%d's number: %s",menu_items[step_item].a,menu_items[step_item].b,step_button==PAD_LEFT ? "left" : "right");
                step_item=-1;
            }
        }
        /* A click on a control, once its row is selected (its update has the input on). */
        if (action_control>=0) {
            const Control *c=&controls[action_control];
            if (c->object!=action_object || now-action_ns>ACTION_NS) action_control=-1; /* gone, or its row not selected */
            else if (c->focused && now-c->seen_ns<LIST_LIVE_NS && nav_target<0 && jump_state==JUMP_IDLE && slider_state==JUMP_IDLE) {
                if (c->kind==CONTROL_SLIDER) {
                    slider_target=c->object; slider_value=action_value; slider_relative=action_part!=PART_BAR;
                    slider_ns=now; slider_state=JUMP_POSTED;
                } else if (action_part!=PART_BAR) {
                    const int spinner=c->kind==CONTROL_SPINNER;
                    press_button=action_value<0 ? (spinner ? PAD_DOWN : PAD_LEFT) : (spinner ? PAD_UP : PAD_RIGHT);
                    press_until=now+PRESS_NS;
                }
                menu_note("%s %s: %+d",c->kind==CONTROL_SLIDER ? "slider" : c->kind==CONTROL_SPINNER ? "spinner" : "values",
                          action_part==PART_LEFT ? "back" : action_part==PART_RIGHT ? "on" : "bar",action_value);
                action_control=-1;
            }
        }
        /* A bar (one row of three or more) beside another list, like an inventory's tabs, only
         * switches: the confirm button acts on the other list's selection. A box's buttons (OK,
         * yes/no) are one row of one or two. */
        const int bar=live_lists>1 && lc->list_rows==1 && lc->list_columns>=3;
        if (pressed&4) {
            press_button=cancel_button; press_until=now+PRESS_NS; nav_target=confirm_target=-1; menu_note("cancel");
        } else if (nav_target>=0 && jump_state==JUMP_IDLE) {
            jump_list=lc->list; jump_a=menu_items[nav_target].a; jump_b=menu_items[nav_target].b;
            jump_target=nav_target; jump_ns=now; jump_state=JUMP_POSTED;
            nav_target=-1;
        } else if (confirm_target>=0 && confirm_target==lc->selected && jump_state==JUMP_IDLE && bar) {
            confirm_target=-1;
        } else if (confirm_target>=0 && confirm_target==lc->selected && jump_state==JUMP_IDLE && now-lc->select_ns>=CONFIRM_NS) {
            press_button=confirm_button; press_until=now+PRESS_NS; confirm_target=-1; menu_note("confirm");
        }
        if (press_until) out=press_button;
        goto done;
    }
    if (settle_until) {
        if (selected!=press_from) {
            settle_until=0;
            /* Back at an item already passed on the way: the target is not reachable this way. */
            int seen=0;
            for (int k=0;k<visited_count;++k) seen|=visited[k]==selected;
            if (seen && nav_target>=0 && selected!=nav_target) {
                menu_note("Item_%d_%d: the selection came back, given up",menu_items[nav_target].a,menu_items[nav_target].b);
                blocked=nav_target; nav_target=confirm_target=-1;
            } else if (visited_count<MAX_VISITED) visited[visited_count++]=selected;
        }
        else if (now<settle_until) goto done;
        else {
            settle_until=0;
            if (nav_target>=0) menu_note("Item_%d_%d out of reach",menu_items[nav_target].a,menu_items[nav_target].b);
            blocked=nav_target; nav_target=confirm_target=-1;
            /* Presses here keep going unanswered: something over it has the focus. */
            MenuContainer *aimed=container_of(press_container,0);
            if (aimed && ++aimed->unanswered>=UNANSWERED_MAX) {
                aimed->behind=COVERED; aimed->covered_ns=now; aimed->unanswered=0;
                menu_note("container %" PRIx64 " does not answer: covered",aimed->container);
            }
            press_container=0;
        }
    }
    if (nav_target>=0 && nav_target==selected) nav_target=-1;
    if (nav_target>=0 && steps>=MAX_STEPS) {
        menu_note("Item_%d_%d not reached in %d steps",menu_items[nav_target].a,menu_items[nav_target].b,steps);
        blocked=nav_target; nav_target=confirm_target=-1;
    }
    if (pressed&4) {
        press_button=cancel_button; press_until=now+PRESS_NS; nav_target=confirm_target=-1; menu_note("cancel");
    } else if (nav_target>=0 && selected<0) { /* the selection is unknown: one press shows it */
        press_button=PAD_DOWN; press_until=now+PRESS_NS; press_from=selected; ++steps;
        press_container=menu->container; press_ns=now;
        menu_note("toward Item_%d_%d: down (finds the selection)",menu_items[nav_target].a,menu_items[nav_target].b);
    } else if (nav_target>=0 && selected>=0) {
        if (!visited_count) visited[visited_count++]=selected;
        press_button=toward(&menu_items[selected],&menu_items[nav_target]);
        press_until=now+PRESS_NS; press_from=selected; ++steps;
        press_container=menu->container; press_ns=now;
        menu_note("toward Item_%d_%d: %s",menu_items[nav_target].a,menu_items[nav_target].b,
                  press_button==PAD_UP ? "up" : press_button==PAD_DOWN ? "down" : press_button==PAD_LEFT ? "left" : "right");
    } else if (confirm_target>=0 && confirm_target==selected) {
        press_button=confirm_button; press_until=now+PRESS_NS; confirm_target=-1; menu_note("confirm");
    }
    if (press_until) out=press_button;
done:
    host_unlock(&menu_lock);
    return out;
}

void runtime_menu_configure(unsigned char *base, uint64_t size) {
    const char *env=getenv("BB_MENU_PROBE");
    if (!env || strcmp(env,"1")) return;
    if (size<DATA_END || memcmp(base+CHECK_NAME,"FrpgMenuDlgCursorGroup",23)) {
        puts("Menu probe: not the v1.09 image, off");
        return;
    }
    vt_cache=calloc(VT_CACHE,sizeof(*vt_cache));
    views=malloc(MAX_VIEWS*sizeof(*views));
    if (!vt_cache || !views) return;
    image=base;
    enabled=1;
    const char *color=getenv("BB_CONSOLE_COLOR");
    if ((color && !strcmp(color,"1")) || isatty(STDOUT_FILENO)) { on="\x1b[1;93m"; off="\x1b[0m"; }
    trace_configure();
    const char *enter=getenv("BB_ENTER_BUTTON");
    if (enter && !strcmp(enter,"circle")) { confirm_button=PAD_CIRCLE; cancel_button=PAD_CROSS; }
    menu_mouse=traceable;
    if (menu_mouse) list_configure(hook_page);
    if (menu_mouse) printf("%sMenu mouse: on (menus free the mouse; point, left button confirms, right cancels%s)%s\n",on,
                           slidable ? "; lists jump to the pointed item, sliders and arrows take clicks" :
                           listable ? "; lists jump to the pointed item" : "",off);
    printf("Menu probe: F7 writes %s/menu-probe-*.txt%s\n",log_dir(),
           traceable ? ", F8 menu-trace-*.txt (4 s of menu calls)" : "");
}

static double seconds(void) {
    struct timespec t;
    timespec_get(&t,TIME_UTC);
    return (double)t.tv_sec+t.tv_nsec*1e-9;
}

static void probe(float pointer_x, float pointer_y, int window_w, int window_h) {
    const double start=seconds();
    ++probes;
    view_count=runtime_memory_views(views,MAX_VIEWS);
    uint64_t bytes=0;
    for (size_t v=0;v<view_count;++v) bytes+=views[v].size;
    object_count=0;
    each_qword(find_object,1);
    qsort(objects,object_count,sizeof(*objects),by_address);
    free(menus);
    menus=NULL; menu_count=0;
    size_t named_menus=0;
    for (size_t i=0;i<object_count;++i) named_menus+=menu_class(objects[i].name)!=0;
    menus=calloc(named_menus ? named_menus : 1,sizeof(*menus));
    if (!menus) return;
    for (size_t i=0;i<object_count;++i) if (menu_class(objects[i].name)) menus[menu_count++].object=i;
    if (menu_count) {
        menu_lo=objects[menus[0].object].address;
        menu_hi=objects[menus[menu_count-1].object].address;
        size_t slots=16;
        while (slots<menu_count*4) slots*=2;
        free(menu_index);
        menu_index=calloc(slots,sizeof(*menu_index));
        if (!menu_index) return;
        menu_index_mask=slots-1;
        for (size_t i=0;i<menu_count;++i) {
            size_t h=address_hash(objects[menus[i].object].address)&menu_index_mask;
            while (menu_index[h]) h=(h+1)&menu_index_mask;
            menu_index[h]=(uint32_t)i+1;
        }
        each_qword(find_referrer,1);
    }
    char path[512], stamp[32];
    time_t now=time(NULL);
    strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",localtime(&now));
#ifdef _WIN32
    _mkdir(log_dir());
#else
    mkdir(log_dir(),0755);
#endif
    snprintf(path,sizeof(path),"%s/menu-probe-%s-%u.txt",log_dir(),stamp,probes);
    FILE *f=probe_file=fopen(path,"w");
    if (!f) { printf("Menu probe %u: cannot write %s\n",probes,path); return; }
    fprintf(f,"Menu probe %u, %s\nPointer %.0f,%.0f in a %dx%d window (points)\n",probes,stamp,pointer_x,pointer_y,window_w,window_h);
    fprintf(f,"Guest memory: %zu ranges, %.2f GiB; runtime class objects: %zu, menu objects: %zu\n",
            view_count,bytes/1073741824.0,object_count,menu_count);
    const char **names=malloc((object_count ? object_count : 1)*sizeof(*names));
    if (names) {
        for (size_t i=0;i<object_count;++i) names[i]=objects[i].name;
        qsort(names,object_count,sizeof(*names),by_name);
        fputs("\nAll classes:\n",f);
        for (size_t i=0;i<object_count;) {
            size_t j=i;
            while (j<object_count && !strcmp(names[j],names[i])) ++j;
            fprintf(f,"  %6zu  %s\n",j-i,names[i]);
            i=j;
        }
        free(names);
    }
    qsort(menus,menu_count,sizeof(*menus),by_class);
    fputs("\nMenu classes:\n",f);
    for (size_t i=0;i<menu_count;) {
        size_t j=i;
        while (j<menu_count && !strcmp(objects[menus[j].object].name,objects[menus[i].object].name)) ++j;
        fprintf(f,"  %5zu  %s\n",j-i,objects[menus[i].object].name);
        i=j;
    }
    size_t slots=16;
    while (slots<(menu_count+MAX_FOLLOWED)*4) slots*=2;
    free(dumped);
    dumped=calloc(slots,sizeof(*dumped));
    if (!dumped) { fclose(f); probe_file=NULL; return; }
    dumped_mask=slots-1;
    followed_count=0;
    for (size_t i=0;i<menu_count;++i) mark_dumped(objects[menus[i].object].address);
    for (size_t i=0;i<menu_count && i<MAX_DUMPED;++i) dump(f,&menus[i]);
    if (menu_count>MAX_DUMPED) fprintf(f,"\n(%zu more not dumped)\n",menu_count-MAX_DUMPED);
    fputs("\n== What the menu objects point at\n",f);
    for (size_t i=0;i<followed_count;++i) dump_followed(f,&followed[i]);
    if (followed_count==MAX_FOLLOWED) fputs("\n(the follow limit was reached)\n",f);
    fclose(f);
    probe_file=NULL;
    printf("%sMenu probe %u: %zu menu objects of %zu named, %.1f s -> %s%s\n",on,probes,menu_count,object_count,seconds()-start,path,off);
}

/* A fault in the probe (a bug here: guest data is only read through checked pointers) ends
 * the probe, not the game. */
static void probe_guarded(float pointer_x, float pointer_y, int window_w, int window_h) {
    static int busy;
    static RuntimeRecoverBuf recover;
    if (__atomic_exchange_n(&busy,1,__ATOMIC_ACQUIRE)) return;
    RuntimeRecoverBuf *previous=runtime_fault_recover;
    if (RUNTIME_RECOVER_SET(recover)) {
        runtime_fault_recover=previous;
        if (probe_file) fclose(probe_file);
        probe_file=NULL;
        printf("%sMenu probe %u: stopped by a fault, the file is incomplete%s\n",on,probes,off);
    } else {
        runtime_fault_recover=&recover;
        probe(pointer_x,pointer_y,window_w,window_h);
        runtime_fault_recover=previous;
    }
    __atomic_store_n(&busy,0,__ATOMIC_RELEASE);
}

/* Every pad sample: new F7/F8 presses (several threads may read the pad, one acts on each),
 * then the running trace's pad lines and its end. */
uint32_t runtime_menu_update(unsigned probe_requests, unsigned trace_requests, float pointer_x, float pointer_y,
                             int window_w, int window_h, uint32_t free_buttons, uint32_t buttons, uint8_t left_x, uint8_t left_y) {
    static unsigned probes_seen, traces_seen;
    if (!enabled) return 0;
    unsigned seen=__atomic_load_n(&probes_seen,__ATOMIC_RELAXED);
    if (probe_requests!=seen &&
        __atomic_compare_exchange_n(&probes_seen,&seen,probe_requests,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED))
        probe_guarded(pointer_x,pointer_y,window_w,window_h);
    seen=__atomic_load_n(&traces_seen,__ATOMIC_RELAXED);
    if (trace_requests!=seen &&
        __atomic_compare_exchange_n(&traces_seen,&seen,trace_requests,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED))
        trace_begin(pointer_x,pointer_y,window_w,window_h);
    if (traceable && host_monotonic_ns()-arg_views_ns>VIEWS_NS) refresh_arg_views();
    const uint32_t added=menu_mouse ? menu_pad(pointer_x,pointer_y,window_w,window_h,free_buttons) : 0;
    if (__atomic_load_n(&tracing,__ATOMIC_ACQUIRE)) {
        trace_pad(buttons|added,left_x,left_y);
        trace_finish();
    }
    return added;
}
