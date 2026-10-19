/* Exercise actual guest file operations on a linked overlay and writable saves. */
#include "../src/runtime_file.c"
#include <assert.h>
static int32_t guest_errno;
int32_t *runtime_errno(void) { return &guest_errno; }
int32_t runtime_guest_errno(int e) { return e; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t n,const char *name) {
    for (size_t i=0;i<n;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
int main(void) {
#ifdef _WIN32
    char root[]=".test-tmp/bbport-mod-files-XXXXXX";
#else
    char root[]="/tmp/bbport-mod-files-XXXXXX";
#endif
    assert(mkdtemp(root));
    char game[512],user[512],source[512],link[512],savedir[512],savepath[512],stale[512];
    snprintf(game,sizeof(game),"%s/game",root);
    snprintf(user,sizeof(user),"%s/user",root);
    snprintf(source,sizeof(source),"%s/mod.dcx",root);
    snprintf(link,sizeof(link),"%s/game/asset.dcx",root);
    snprintf(savedir,sizeof(savedir),"%s/saves",root);
    snprintf(savepath,sizeof(savepath),"%s/slot.bin",savedir);
    snprintf(stale,sizeof(stale),"%s/orphan.99.bbtmp",savedir);
    assert(!mkdir(game,0755));
    assert(!mkdir(savedir,0755));
    FILE *f=fopen(source,"w"); assert(f); assert(fputs("modded",f)>=0); assert(!fclose(f));
#ifdef _WIN32
    f=fopen(link,"w"); assert(f); assert(fputs("modded",f)>=0); assert(!fclose(f));
#else
    assert(!symlink(source,link));
#endif
    runtime_file_configure(game,user);
    assert(!runtime_file_mount("/savedata0",savedir));
    for (int i=0;i<3;++i) {
        const char *path=i==0 ? "/app0/asset.dcx" : i==1 ? "/hostapp/asset.dcx" : "asset.dcx";
        int fd=(int)do_open(path,0,0); assert(fd>=3);
        char content[8]={0}; assert(do_read(fd,content,6)==6 && !strcmp(content,"modded"));
        GuestStat info; assert(!do_stat(path,&info) && info.size==6);
        assert(!do_close(fd));
        assert(do_open(path,2,0)==-EROFS);
        assert(do_open(path,0x400,0)==-EROFS);
        assert(do_truncate(path,0)==-EROFS);
        assert(path_op(path,2,0)==-EROFS);
        assert(do_rename(path,"/data/moved")==-EROFS);
    }
    int dir=(int)do_open("/app0",0x20000,0); assert(dir>=3);
    char entries[1024]; int64_t count=do_getdents(dir,entries,sizeof(entries),NULL); assert(count>0);
    int found=0;
    for (int64_t p=0;p<count;) {
        uint16_t length; memcpy(&length,entries+p+4,2);
        assert(length);
        if (!strcmp(entries+p+8,"asset.dcx")) { assert(entries[p+6]==8); found=1; }
        p+=length;
    }
    assert(found && !do_close(dir));
    int save=(int)do_open("/data/test-save",0x202,0644); assert(save>=3);
    assert(do_write(save,"save",4)==4 && !do_close(save));
    assert(!path_op("/data/test-save",2,0));

    /* A writable save is invisible until close commits its complete temporary copy. */
    f=fopen(savepath,"wb"); assert(f); assert(fwrite("old",1,3,f)==3); assert(!fclose(f));
    save=(int)do_open("/savedata0/slot.bin",0x402,0644); assert(save>=3);
    assert(do_write(save,"new save",8)==8);
    f=fopen(savepath,"rb"); assert(f); char before[16]={0}; assert(fread(before,1,3,f)==3); fclose(f);
    assert(!strcmp(before,"old"));
    assert(!do_close(save));
    f=fopen(savepath,"rb"); assert(f); char after[16]={0}; assert(fread(after,1,8,f)==8); fclose(f);
    assert(!strcmp(after,"new save"));

    /* POSIX open side effects also commit without an explicit write. */
    save=(int)do_open("/savedata0/slot.bin",0x402,0644); assert(save>=3 && !do_close(save));
    struct stat empty_stat; assert(!stat(savepath,&empty_stat) && empty_stat.st_size==0);
    save=(int)do_open("/savedata0/created.bin",0x202,0644); assert(save>=3 && !do_close(save));
    char created[512]; snprintf(created,sizeof(created),"%s/created.bin",savedir);
    assert(!stat(created,&empty_stat) && empty_stat.st_size==0);

    /* Opening without a write discards the copy; a stale copy from a crash is removed at mount. */
    save=(int)do_open("/savedata0/slot.bin",2,0644); assert(save>=3 && !do_close(save));
    f=fopen(stale,"wb"); assert(f); assert(fputs("partial",f)>=0); assert(!fclose(f));
    runtime_file_unmount("/savedata0");
    assert(!runtime_file_mount("/savedata0",savedir));
    assert(access(stale,F_OK)!=0);
    assert(!unlink(link) && !unlink(source) && !rmdir(game));
    const char *dirs[]={"temp0","download0","data"};
    for (int i=0;i<3;++i) { char p[1024]; snprintf(p,sizeof(p),"%s/%s",user,dirs[i]); assert(!rmdir(p)); }
    assert(!unlink(savepath) && !unlink(created) && !rmdir(savedir));
    assert(!rmdir(user) && !rmdir(root));
    puts("Guest files: overlays, readonly assets and atomic writable saves PASS");
}
