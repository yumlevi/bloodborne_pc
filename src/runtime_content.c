/* Offline AppContent provider for the configured base-game profile. Add-on folders in
 * <user>/addcont/<title id>/<entitlement label> are reported as installed; their files are not
 * mounted because Bloodborne 1.09 already contains The Old Hunters data. */
#include "runtime.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define INVALID_ID ((int32_t)0x805a1000)
#define NOT_LOADED ((int32_t)0x805a1001)
#define PARAMETER ((int32_t)0x80d90002)
#define BUSY ((int32_t)0x80d90003)
static unsigned refs, initialized, configured;
static uint32_t parameters[5];
static unsigned queries, lists;
void runtime_content_configure(const uint32_t values[5]) {
    if (refs || initialized || (values[0]!=1 && values[0]!=3)) {
        fputs("ERROR: invalid or late content profile\n",stderr); exit(1);
    }
    memcpy(parameters,values,sizeof(parameters)); configured=1;
}
static void require_provider(void) {
    if (!configured) { fputs("STOP: AppContent needs --content-profile\n",stderr); exit(21); }
}
/* System modules: every library this eboot imports is provided by the host
 * runtime, so loading one only tracks a reference count. AppContent (0xb4)
 * additionally requires the configured content profile. */
static unsigned module_refs[0x10000];
static ABI int32_t module_load(uint16_t id) {
    if (!id) return INVALID_ID;
    if (id==0xb4) require_provider();
    if (module_refs[id]==UINT32_MAX) { fputs("STOP: module reference overflow\n",stderr); exit(21); }
    ++module_refs[id];
    if (id==0xb4) { refs=module_refs[id]; printf("Runtime: AppContent host provider loaded; references=%u\n",refs); }
    else printf("Runtime: system module 0x%x loaded (host implementation)\n",id);
    return 0;
}
static ABI int32_t module_loaded(uint16_t id) {
    if (!id) return INVALID_ID;
    return module_refs[id] ? 0 : NOT_LOADED;
}
static ABI int32_t module_unload(uint16_t id) {
    int32_t result=module_loaded(id);
    if (result) return result;
    --module_refs[id];
    if (id==0xb4) { refs=module_refs[id]; if (!refs) initialized=0; }
    return 0;
}
static void require_loaded(void) {
    require_provider();
    if (!refs) { fputs("STOP: AppContent called before module load\n",stderr); exit(21); }
}
static ABI int32_t content_init(const unsigned char *init,unsigned char *boot) {
    require_loaded();
    if (!init || !boot) return PARAMETER;
    if (initialized) return BUSY;
    /* Only the reserved-zero init profile is implemented. */
    for (unsigned i=0;i<32;++i) if (init[i]) return PARAMETER;
    memset(boot,0,40); initialized=1;
    puts("Runtime: AppContent initialized; offline base-game profile, no mounted add-ons");
    return 0;
}
static void require_initialized(void) {
    require_loaded();
    if (!initialized) { fputs("STOP: AppContent query before initialization\n",stderr); exit(21); }
}
static ABI int32_t param_int(uint32_t id,int32_t *out) {
    require_initialized();
    if (id>4 || !out) return PARAMETER;
    memcpy(out,&parameters[id],4); ++queries;
    printf("Runtime: AppContent parameter %u = %d\n",id,*out);
    return 0;
}
#define LABEL_SIZE 17
#define ADDON_INSTALLED 4u
typedef struct { char label[LABEL_SIZE]; char padding[3]; uint32_t status; } AddonInfo;
static ABI int32_t addon_list(uint32_t service,void *list,uint32_t capacity,uint32_t *hits) {
    require_initialized();
    if (service) { fputs("STOP: nonzero AppContent service label unsupported\n",stderr); exit(21); }
    if ((!capacity || !list) && !hits) return PARAMETER;
    const int fill=list && capacity;
    uint32_t count=0;
    char root[1024];
    snprintf(root,sizeof(root),"%s/addcont",runtime_file_user_dir());
    DIR *titles=opendir(root);
    for (struct dirent *title;titles && (title=readdir(titles));) {
        if (title->d_name[0]=='.') continue;
        char title_path[1300];
        if ((size_t)snprintf(title_path,sizeof(title_path),"%s/%s",root,title->d_name)>=sizeof(title_path)) continue;
        struct stat title_stat;
        if (stat(title_path,&title_stat) || !S_ISDIR(title_stat.st_mode)) continue;
        DIR *labels=opendir(title_path);
        for (struct dirent *label;labels && (label=readdir(labels));) {
            size_t n=strlen(label->d_name);
            if (label->d_name[0]=='.' || n>=LABEL_SIZE || (fill && count>=capacity)) continue;
            char label_path[1500];
            if ((size_t)snprintf(label_path,sizeof(label_path),"%s/%s",title_path,label->d_name)>=sizeof(label_path)) continue;
            struct stat label_stat;
            if (stat(label_path,&label_stat) || !S_ISDIR(label_stat.st_mode)) continue;
            if (fill) {
                AddonInfo info;
                memset(&info,0,sizeof(info));
                memcpy(info.label,label->d_name,n);
                info.status=ADDON_INSTALLED;
                memcpy((unsigned char *)list+(size_t)count*sizeof(info),&info,sizeof(info));
            }
            ++count;
        }
        if (labels) closedir(labels);
    }
    if (titles) closedir(titles);
    if (hits) *hits=count;
    if (count) printf("Runtime: AppContent add-ons reported installed (%s): %u\n",root,count);
    ++lists; return 0;
}
uintptr_t runtime_content_resolve(const char *name) {
    if (!strcmp(name,"g8cM39EUZ6o#M#N")) return (uintptr_t)module_load;
    if (!strcmp(name,"fMP5NHUOaMk#M#N")) return (uintptr_t)module_loaded;
    if (!strcmp(name,"eR2bZFAAU0Q#M#N")) return (uintptr_t)module_unload;
    if (!strcmp(name,"R9lA82OraNs#c#d")) return (uintptr_t)content_init;
    if (!strcmp(name,"99b82IKXpH4#c#d")) return (uintptr_t)param_int;
    if (!strcmp(name,"xnd8BJzAxmk#c#d")) return (uintptr_t)addon_list;
    return 0;
}
void runtime_content_report(void) {
    printf("Runtime: AppContent references=%u, initialized=%u, parameter queries=%u, list queries=%u\n",refs,initialized,queries,lists);
}
