/* Guest file system: PS4 mount points mapped onto host directories.
 *   /app0, /hostapp  -> game package root (read-only by convention)
 *   /temp0, /download0, /data, and mounts added by SaveData -> user directory
 * Guest descriptors are small integers in our own table; stdio 0-2 pass through.
 * Paths containing ".." components are rejected rather than normalized. */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#ifdef _WIN32
/* 64-bit sizes and times; directories have no host descriptor (open() rejects them). */
typedef struct _stat64 HostStat;
#define host_stat _stat64
#define host_fstat _fstat64
#define lseek _lseeki64
#else
typedef struct stat HostStat;
#define host_stat stat
#define host_fstat fstat
#endif
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define MAX_FILES 1024
#define MAX_MOUNTS 16

typedef struct { int64_t sec, nsec; } GuestTimespec;
typedef struct {
    uint32_t dev, ino;
    uint16_t mode, nlink;
    uint32_t uid, gid, rdev;
    GuestTimespec atime, mtime, ctime;
    int64_t size, blocks;
    uint32_t blksize, flags, gen;
    int32_t lspare;
    GuestTimespec birthtime;
} GuestStat;
_Static_assert(sizeof(GuestStat)==120,"FreeBSD stat layout");

typedef struct { char *names; size_t count, *offsets; unsigned char *types; } Listing;
/* Save files opened for writing use a private copy. commit is the final path and temp is the
 * copy; close flushes the copy and replaces the final path in one rename. */
typedef struct {
    int used, host, dirty;
    Listing *dir;
    size_t position;
    char path[512];
    char *commit, *temp;
} File;
typedef struct { char guest[64]; char host[512]; } Mount;
static File files[MAX_FILES];
static Mount mounts[MAX_MOUNTS];
static size_t mount_count, opens, reads, writes, missing;
static uint64_t bytes_read;
static HostMutex lock=HOST_MUTEX_INIT;

static int save_path(const char *path) { return path && !strncmp(path,"/savedata",9); }
static int temp_name(const char *name) {
    size_t n=strlen(name);
    return n>6 && !strcmp(name+n-6,".bbtmp");
}
/* Remove copies left by an interrupted save. The old committed file is still intact. */
static void remove_stale_temps(const char *dir,int depth) {
    DIR *d=opendir(dir);
    if (!d) return;
    for (struct dirent *e;(e=readdir(d));) {
        if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
        char path[1300];
        if ((size_t)snprintf(path,sizeof(path),"%s/%s",dir,e->d_name)>=sizeof(path)) continue;
        if (temp_name(e->d_name)) {
            int open_now=0;
            for (int i=3;i<MAX_FILES;++i)
                if (files[i].used && files[i].temp && !strcmp(files[i].temp,path)) open_now=1;
            if (!open_now && !unlink(path))
                printf("Runtime: removed interrupted save copy %s (committed file kept)\n",path);
            continue;
        }
        if (depth<4) {
            HostStat s;
            if (!host_stat(path,&s) && S_ISDIR(s.st_mode)) remove_stale_temps(path,depth+1);
        }
    }
    closedir(d);
}

int runtime_file_mount(const char *guest,const char *host) {
    host_lock(&lock);
    for (size_t i=0;i<mount_count;++i) if (!strcmp(mounts[i].guest,guest)) {
        snprintf(mounts[i].host,sizeof(mounts[i].host),"%s",host);
        host_unlock(&lock); return 0;
    }
    if (mount_count==MAX_MOUNTS || strlen(guest)>=64 || strlen(host)>=512) { host_unlock(&lock); return -1; }
    snprintf(mounts[mount_count].guest,64,"%s",guest);
    snprintf(mounts[mount_count].host,512,"%s",host);
    ++mount_count;
    if (save_path(guest)) remove_stale_temps(host,0);
    host_unlock(&lock);
    return 0;
}
void runtime_file_unmount(const char *guest) {
    host_lock(&lock);
    for (size_t i=0;i<mount_count;++i) if (!strcmp(mounts[i].guest,guest)) {
        mounts[i]=mounts[--mount_count]; break;
    }
    host_unlock(&lock);
}
static char user_root[512]="user";
const char *runtime_file_user_dir(void) { return user_root; }
void runtime_file_configure(const char *app0,const char *user) {
    char path[600];
    snprintf(user_root,sizeof(user_root),"%s",user);
    runtime_file_mount("/app0",app0);
    runtime_file_mount("/hostapp",app0);
    const char *writable[]={"temp0","download0","data"};
    mkdir(user,0755);
    for (int i=0;i<3;++i) {
        snprintf(path,sizeof(path),"%s/%s",user,writable[i]);
        mkdir(path,0755);
        char guest[32]; snprintf(guest,sizeof(guest),"/%s",writable[i]);
        runtime_file_mount(guest,path);
    }
}
/* Resolve a guest path to a host path; returns 0 or a host errno. */
static int translate(const char *guest,char *out,size_t size) {
    if (!guest || !*guest) return ENOENT;
    char buffer[1024];
    if (guest[0]!='/') snprintf(buffer,sizeof(buffer),"/app0/%s",guest);
    else snprintf(buffer,sizeof(buffer),"%s",guest);
    for (const char *p=buffer;(p=strstr(p,".."));p+=2)
        if ((p==buffer || p[-1]=='/') && (p[2]==0 || p[2]=='/')) return EACCES;
    host_lock(&lock);
    size_t best=0; const Mount *m=NULL;
    for (size_t i=0;i<mount_count;++i) {
        size_t n=strlen(mounts[i].guest);
        if (!strncmp(buffer,mounts[i].guest,n) && (buffer[n]=='/' || !buffer[n]) && n>best) { best=n; m=&mounts[i]; }
    }
    int result=0;
    if (!m) result=ENOENT;
    else if ((size_t)snprintf(out,size,"%s%s",m->host,buffer+best)>=size) result=ENAMETOOLONG;
    host_unlock(&lock);
    if (result==ENOENT) fprintf(stderr,"Runtime: no mount for guest path %s\n",guest);
    return result;
}
static int host_flags(int flags) {
    int r;
    switch (flags&3) { case 0: r=O_RDONLY; break; case 1: r=O_WRONLY; break; default: r=O_RDWR; }
    if (flags&0x4) r|=O_NONBLOCK;
    if (flags&0x8) r|=O_APPEND;
    if (flags&0x80) r|=O_SYNC;
    if (flags&0x200) r|=O_CREAT;
    if (flags&0x400) r|=O_TRUNC;
    if (flags&0x800) r|=O_EXCL;
    if (flags&0x20000) r|=O_DIRECTORY;
#ifdef _WIN32
    r|=O_BINARY;
#endif
    return r|O_CLOEXEC;
}
static void free_listing(Listing *l) { if (l) { free(l->names); free(l->offsets); free(l->types); free(l); } }
static Listing *list_directory(const char *path) {
    DIR *d=opendir(path);
    if (!d) return NULL;
    Listing *l=calloc(1,sizeof(*l));
    size_t capacity=0,bytes=0,cap_names=0;
    struct dirent *e;
    while (l && (e=readdir(d))) {
        if (temp_name(e->d_name)) continue;
        size_t n=strlen(e->d_name)+1;
        if (l->count==capacity) {
            capacity=capacity ? capacity*2 : 64;
            l->offsets=realloc(l->offsets,capacity*sizeof(size_t));
            l->types=realloc(l->types,capacity);
        }
        if (bytes+n>cap_names) { cap_names=(bytes+n)*2; l->names=realloc(l->names,cap_names); }
        if (!l->offsets || !l->types || !l->names) { fputs("Out of memory listing directory\n",stderr); exit(1); }
        memcpy(l->names+bytes,e->d_name,n);
        l->offsets[l->count]=bytes;
#ifdef _WIN32
        /* MinGW's dirent has no entry types. */
        char child[1300]; HostStat entry;
        snprintf(child,sizeof(child),"%s/%s",path,e->d_name);
        l->types[l->count]=host_stat(child,&entry) ? 0 : S_ISDIR(entry.st_mode) ? 4 : S_ISREG(entry.st_mode) ? 8 : 0;
#else
        unsigned char type=e->d_type;
        if (type==DT_LNK || type==DT_UNKNOWN) {
            struct stat entry;
            if (!fstatat(dirfd(d),e->d_name,&entry,0))
                type=S_ISDIR(entry.st_mode) ? DT_DIR : S_ISREG(entry.st_mode) ? DT_REG : type;
        }
        l->types[l->count]=type==DT_DIR ? 4 : type==DT_REG ? 8 : type==DT_LNK ? 10 : 0;
#endif
        ++l->count; bytes+=n;
    }
    closedir(d);
    return l;
}
static void convert_stat(const HostStat *s,GuestStat *g) {
    memset(g,0,sizeof(*g));
    g->dev=(uint32_t)s->st_dev; g->ino=(uint32_t)s->st_ino;
    g->nlink=(uint16_t)s->st_nlink;
    g->size=s->st_size;
#ifdef _WIN32
    /* FreeBSD type bits match POSIX; Windows reports owner bits only. */
    g->mode=(uint16_t)((S_ISDIR(s->st_mode) ? 0040000 : 0100000) | ((s->st_mode & 0700) * 0111 & 0777));
    g->blocks=(s->st_size+511)/512; g->blksize=65536;
    g->atime=(GuestTimespec){s->st_atime,0};
    g->mtime=(GuestTimespec){s->st_mtime,0};
    g->ctime=(GuestTimespec){s->st_ctime,0};
#else
    g->mode=(uint16_t)s->st_mode;
    g->blocks=s->st_blocks; g->blksize=(uint32_t)s->st_blksize;
    g->atime=(GuestTimespec){s->st_atim.tv_sec,s->st_atim.tv_nsec};
    g->mtime=(GuestTimespec){s->st_mtim.tv_sec,s->st_mtim.tv_nsec};
    g->ctime=(GuestTimespec){s->st_ctim.tv_sec,s->st_ctim.tv_nsec};
#endif
    g->birthtime=g->ctime;
}
static File *get(int fd) {
    if (fd<3 || fd>=MAX_FILES || !files[fd].used) return NULL;
    return &files[fd];
}
/* BB_AUDIO_TRACE=1: sound file opens and failed reads (missing game sounds). */
static int audio_trace(void) { static int v=-1; if (v<0) { const char *e=getenv("BB_AUDIO_TRACE"); v=e && e[0]=='1'; } return v; }
/* BB_SAVE_TRACE=1: guest operations that participate in a transactional save. */
static int save_trace(void) { static int v=-1; if (v<0) { const char *e=getenv("BB_SAVE_TRACE"); v=e && e[0]=='1'; } return v; }
/* Game mounts (including linked mod overlays) are read-only. Saves use other mounts. */
static int game_path(const char *p) {
    if (!p || !*p) return 0;
    if (*p!='/') return 1;
    return (!strncmp(p,"/app0",5) && (!p[5] || p[5]=='/')) ||
           (!strncmp(p,"/hostapp",8) && (!p[8] || p[8]=='/'));
}
static unsigned temp_serial;
static int copy_contents(int from,int to) {
    char buffer[65536];
    for (;;) {
        ssize_t n=read(from,buffer,sizeof(buffer));
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return n<0 ? -1 : 0;
        for (ssize_t done=0;done<n;) {
            ssize_t written=write(to,buffer+done,(size_t)(n-done));
            if (written<0 && errno==EINTR) continue;
            if (written<=0) return -1;
            done+=written;
        }
    }
}
/* Redirect path operations to an open save's private copy. A path write also marks the copy
 * dirty so close commits it. */
static void current_copy(char *path,size_t size,int write_access) {
    host_lock(&lock);
    for (int i=3;i<MAX_FILES;++i) {
        if (files[i].used && files[i].commit && !strcmp(files[i].commit,path)) {
            snprintf(path,size,"%s",files[i].temp);
            files[i].dirty|=write_access;
            break;
        }
    }
    host_unlock(&lock);
}
/* Open a copy of a save for writing. The original is not touched until commit_file(). */
static int open_for_commit(const char *path,const char *source,int flags,int mode,
                           char **commit,char **temp,int *dirty) {
    int hf=host_flags(flags);
    HostStat s;
    int exists=!host_stat(source,&s);
    if (exists && !S_ISREG(s.st_mode)) {
        int h=open(path,hf,mode ? mode : 0644);
        return h<0 ? -errno : h;
    }
    if (exists && (hf&O_CREAT) && (hf&O_EXCL)) return -EEXIST;
    if (!exists && !(hf&O_CREAT)) return -ENOENT;
    size_t n=strlen(path)+32;
    char *copy=malloc(n), *final=strdup(path);
    if (!copy || !final) { free(copy); free(final); return -ENOMEM; }
    snprintf(copy,n,"%s.%u.bbtmp",path,__atomic_add_fetch(&temp_serial,1,__ATOMIC_RELAXED));
    int out=open(copy,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC
#ifdef _WIN32
                 |O_BINARY
#endif
                 ,exists ? (int)(s.st_mode&07777) : mode ? mode : 0644);
    int error=out<0 ? errno : 0;
    if (!error && exists && !(hf&O_TRUNC)) {
        int in=open(source,O_RDONLY|O_CLOEXEC
#ifdef _WIN32
                    |O_BINARY
#endif
                    );
        if (in<0 || copy_contents(in,out)) error=errno ? errno : EIO;
        if (in>=0) close(in);
    }
    if (out>=0) close(out);
    int host=error ? -1 : open(copy,hf&~(O_CREAT|O_EXCL|O_TRUNC));
    if (host<0) {
        if (!error) error=errno;
        unlink(copy); free(copy); free(final);
        return -error;
    }
    *commit=final;
    *temp=copy;
    /* O_TRUNC changes an existing file, and O_CREAT creates a missing empty file, even if the
     * guest never follows the open with write(). Both therefore have to commit on close. */
    *dirty=!exists || (hf&O_TRUNC);
    return host;
}
static void sync_parent_directory(const char *file) {
#ifndef _WIN32
    char dir[1024];
    snprintf(dir,sizeof(dir),"%s",file);
    char *slash=strrchr(dir,'/');
    if (!slash) return;
    *slash=0;
    int descriptor=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if (descriptor>=0) { fsync(descriptor); close(descriptor); }
#else
    (void)file; /* MoveFileEx in win32_compat.c uses MOVEFILE_WRITE_THROUGH. */
#endif
}
static int commit_file(File *file) {
    if (!file->dirty) {
        close(file->host);
        unlink(file->temp);
        return 0;
    }
    int error=fsync(file->host) ? errno : 0;
    close(file->host);
    if (!error && rename(file->temp,file->commit)) error=errno;
    if (error) {
        fprintf(stderr,"Runtime: save %s was not replaced (%s); previous file kept\n",
                file->path,strerror(error));
        unlink(file->temp);
        return -error;
    }
    sync_parent_directory(file->commit);
    return 0;
}
/* All operations return >=0 or -(host errno); wrappers adapt the convention. */
static int64_t do_open(const char *guest,int flags,int mode) {
    if (game_path(guest) && (flags & (3|0x8|0x200|0x400|0x800))) return -EROFS;
    char path[1024];
    int e=translate(guest,path,sizeof(path));
    if (e) return -e;
    char current[1024], *commit=NULL, *temp=NULL;
    int dirty=0;
    snprintf(current,sizeof(current),"%s",path);
    if (save_path(guest)) current_copy(current,sizeof(current),0);
    HostStat s;
    Listing *dir=NULL;
    int host=-1;
#ifdef _WIN32
    /* Windows cannot open a directory as a descriptor: list it, keep no host descriptor. */
    if (!host_stat(current,&s) && S_ISDIR(s.st_mode)) {
        if (flags&3) return -EISDIR;
        if (!(dir=list_directory(current))) return -errno;
    } else
#endif
    {
    if (save_path(guest) && (flags&3)) {
        host=open_for_commit(path,current,flags,mode,&commit,&temp,&dirty);
        if (host<0) { errno=-host; host=-1; }
    } else {
        host=open(current,host_flags(flags),mode ? mode : 0644);
    }
    if (host<0) {
        e=errno;
        if (e==ENOENT) { ++missing; printf("Runtime: open(%s) -> not found\n",guest); }
        return -e;
    }
    if (!host_fstat(host,&s) && S_ISDIR(s.st_mode)) dir=list_directory(current);
    }
    if (save_trace() && save_path(guest))
        printf("Save trace: open(%s, flags 0x%x)%s%s\n",guest,flags,
               temp ? " -> copy " : "",temp ? temp : "");
    host_lock(&lock);
    int fd=-1;
    for (int i=3;i<MAX_FILES;++i) if (!files[i].used) { fd=i; break; }
    if (fd<0) {
        host_unlock(&lock);
        if (host>=0) close(host);
        free_listing(dir);
        if (temp) unlink(temp);
        free(commit); free(temp);
        return -EMFILE;
    }
    files[fd]=(File){.used=1,.host=host,.dirty=dirty,.dir=dir,.commit=commit,.temp=temp};
    snprintf(files[fd].path,sizeof(files[fd].path),"%s",guest);
    ++opens;
    host_unlock(&lock);
    if (audio_trace() && strstr(guest,"sound/")) printf("Audio trace: open(%s) -> fd %d, %lld bytes\n",guest,fd,(long long)s.st_size);
    const char *mod_trace=getenv("BB_MOD_TRACE"), *mod_root=getenv("BB_MODS_DIR");
    if (mod_trace && mod_trace[0]=='1' && mod_root) {
        char actual[PATH_MAX],root[PATH_MAX];
        static unsigned traced;
        if (realpath(path,actual) && realpath(mod_root,root)) {
            size_t n=strlen(root);
            if (!strncmp(actual,root,n) && (actual[n]=='/' || actual[n]=='\\') &&
                __atomic_fetch_add(&traced,1,__ATOMIC_RELAXED)<32)
                printf("Mods: open %s -> %s\n",guest,actual);
        }
    }
    return fd;
}
static int64_t do_close(int fd) {
    if (fd>=0 && fd<3) return 0;
    host_lock(&lock);
    File *f=get(fd);
    if (!f) { host_unlock(&lock); return -EBADF; }
    File closed=*f;
    *f=(File){0};
    host_unlock(&lock);
    free_listing(closed.dir);
    int result=0;
    if (closed.temp) result=commit_file(&closed);
    else if (closed.host>=0) close(closed.host);
    if (save_trace() && save_path(closed.path))
        printf("Save trace: close(fd %d, %s)%s -> %d\n",fd,closed.path,
               closed.temp ? closed.dirty ? " commit" : " discard unwritten copy" : "",result);
    free(closed.commit); free(closed.temp);
    return result;
}
static int host_fd(int fd) {
    if (fd>=0 && fd<3) return fd;
    File *f=get(fd);
    return f ? f->host : -1;
}
static int host_fd_written(int fd) {
    if (fd>=0 && fd<3) return fd;
    File *f=get(fd);
    if (!f) return -1;
    f->dirty=1;
    return f->host;
}
/* Pages of the destination may be write-protected for GPU tracking: the kernel's copy then
 * fails with EFAULT instead of faulting to our handler. A user-mode write to each page first
 * goes through the handler, which unprotects it (and records the upcoming write). */
static void touch_for_write(void *buffer,uint64_t size) {
    if (!size) return;
    uintptr_t p=(uintptr_t)buffer & ~(uintptr_t)4095, end=(uintptr_t)buffer+size;
    for (; p<end; p+=4096) {
        volatile unsigned char *b=(volatile unsigned char *)(p<(uintptr_t)buffer ? (uintptr_t)buffer : p);
        *b=*b;
    }
}
static int64_t do_read(int fd,void *buffer,uint64_t size) {
    int h=host_fd(fd);
    if (h<0) return get(fd) ? -EISDIR : -EBADF;
    touch_for_write(buffer,size);
    ssize_t n=read(h,buffer,size);
    if (n<0) { if (audio_trace()) printf("Audio trace: read(fd %d, %llu) failed, errno %d\n",fd,(unsigned long long)size,errno); return -errno; }
    __atomic_add_fetch(&reads,1,__ATOMIC_RELAXED); __atomic_add_fetch(&bytes_read,(uint64_t)n,__ATOMIC_RELAXED);
    return n;
}
static int64_t do_pread(int fd,void *buffer,uint64_t size,int64_t offset) {
    int h=host_fd(fd);
    if (h<0) return get(fd) ? -EISDIR : -EBADF;
    touch_for_write(buffer,size);
    ssize_t n=pread(h,buffer,size,offset);
    if (n<0) { if (audio_trace()) printf("Audio trace: pread(fd %d, %llu @%lld) failed, errno %d\n",fd,(unsigned long long)size,(long long)offset,errno); return -errno; }
    __atomic_add_fetch(&reads,1,__ATOMIC_RELAXED); __atomic_add_fetch(&bytes_read,(uint64_t)n,__ATOMIC_RELAXED);
    return n;
}
static int64_t do_write(int fd,const void *buffer,uint64_t size) {
    int h=host_fd_written(fd);
    if (h<0) return -EBADF;
    ssize_t n=write(h,buffer,size);
    if (n<0) return -errno;
    __atomic_add_fetch(&writes,1,__ATOMIC_RELAXED);
    return n;
}
static int64_t do_pwrite(int fd,const void *buffer,uint64_t size,int64_t offset) {
    int h=host_fd_written(fd);
    if (h<0) return -EBADF;
    ssize_t n=pwrite(h,buffer,size,offset);
    return n<0 ? -errno : n;
}
static int64_t do_lseek(int fd,int64_t offset,int whence) {
    File *f=get(fd);
    if (!f) return -EBADF;
    if (whence<0 || whence>2) return -EINVAL;
    if (f->dir) {
        /* Directory offsets are entry indices for getdirentries. */
        if (whence==0 && offset>=0) { f->position=(size_t)offset; return offset; }
        return -EINVAL;
    }
    int64_t r=lseek(f->host,offset,whence);
    return r<0 ? -errno : r;
}
static int64_t do_stat(const char *guest,GuestStat *out);
static int64_t do_fstat(int fd,GuestStat *out) {
    int h=host_fd(fd);
    if (h<0) {
        File *f=get(fd);
        return f && f->dir ? do_stat(f->path,out) : -EBADF; /* Windows directory descriptor */
    }
    if (!out) return -EFAULT;
    HostStat s;
    if (host_fstat(h,&s)) return -errno;
    convert_stat(&s,out); return 0;
}
static int64_t do_stat(const char *guest,GuestStat *out) {
    char path[1024]; HostStat s;
    int e=translate(guest,path,sizeof(path));
    if (e) return -e;
    if (save_path(guest)) current_copy(path,sizeof(path),0);
    if (!out) return -EFAULT;
    if (host_stat(path,&s)) return -errno;
    convert_stat(&s,out); return 0;
}
static int64_t do_getdents(int fd,char *buffer,uint64_t size,int64_t *basep) {
    host_lock(&lock);
    File *f=get(fd);
    int64_t result=0;
    if (!f) result=-EBADF;
    else if (!f->dir) result=-EINVAL;
    else if (!buffer) result=-EFAULT;
    else if (size<512) result=-EINVAL;
    else {
        if (basep) *basep=(int64_t)f->position;
        uint64_t written=0;
        while (f->position<f->dir->count) {
            const char *name=f->dir->names+f->dir->offsets[f->position];
            size_t n=strlen(name); if (n>255) n=255;
            uint16_t reclen=(uint16_t)((8+n+1+7)&~(size_t)7);
            if (written+reclen>size) break;
            char *p=buffer+written;
            memset(p,0,reclen);
            uint32_t ino=(uint32_t)f->position+1;
            memcpy(p,&ino,4); memcpy(p+4,&reclen,2);
            p[6]=(char)f->dir->types[f->position]; p[7]=(char)n;
            memcpy(p+8,name,n);
            written+=reclen; ++f->position;
        }
        result=(int64_t)written;
    }
    host_unlock(&lock);
    return result;
}
static int64_t path_op(const char *guest,int op,int mode) {
    if (game_path(guest)) return -EROFS;
    char path[1024];
    int e=translate(guest,path,sizeof(path));
    if (e) return -e;
    int r= op==0 ? mkdir(path,mode ? mode : 0755) : op==1 ? rmdir(path) : unlink(path);
    return r ? -errno : 0;
}
static int64_t do_rename(const char *from,const char *to) {
    if (game_path(from) || game_path(to)) return -EROFS;
    char a[1024],b[1024];
    int e=translate(from,a,sizeof(a));
    if (!e) e=translate(to,b,sizeof(b));
    if (e) return -e;
    return rename(a,b) ? -errno : 0;
}
static int64_t do_ftruncate(int fd,int64_t length) {
    int h=host_fd_written(fd);
    if (h<0) return -EBADF;
    return ftruncate(h,length) ? -errno : 0;
}
static int64_t do_truncate(const char *guest,int64_t length) {
    if (game_path(guest)) return -EROFS;
    char path[1024];
    int e=translate(guest,path,sizeof(path));
    if (e) return -e;
    if (save_path(guest)) current_copy(path,sizeof(path),1);
    return truncate(path,length) ? -errno : 0;
}
static int64_t do_fsync(int fd) { int h=host_fd(fd); if (h<0) return -EBADF; return fsync(h) ? -errno : 0; }
static int64_t do_access(const char *guest,int mode) {
    char path[1024];
    int e=translate(guest,path,sizeof(path));
    if (e) return -e;
    if (save_path(guest)) current_copy(path,sizeof(path),0);
#ifdef _WIN32
    return access(path,mode&6) ? -errno : 0; /* no execute bit to test */
#else
    return access(path,mode&7) ? -errno : 0;
#endif
}

/* Convention adapters: sceKernel* -> Orbis error codes, POSIX -> -1 + errno. */
static int64_t sce(int64_t r) { return r<0 ? ERR(runtime_guest_errno((int)-r)) : r; }
static int64_t posix(int64_t r) { if (r<0) { *runtime_errno()=runtime_guest_errno((int)-r); return -1; } return r; }
#define PAIR(name,params,args) \
    static ABI int64_t sce_##name params { return sce(do_##name args); } \
    static ABI int64_t posix_##name params { return posix(do_##name args); }
PAIR(open,(const char *p,int f,int m),(p,f,m))
PAIR(close,(int fd),(fd))
PAIR(read,(int fd,void *b,uint64_t n),(fd,b,n))
PAIR(pread,(int fd,void *b,uint64_t n,int64_t o),(fd,b,n,o))
PAIR(write,(int fd,const void *b,uint64_t n),(fd,b,n))
PAIR(pwrite,(int fd,const void *b,uint64_t n,int64_t o),(fd,b,n,o))
PAIR(lseek,(int fd,int64_t o,int w),(fd,o,w))
PAIR(fstat,(int fd,GuestStat *s),(fd,s))
PAIR(stat,(const char *p,GuestStat *s),(p,s))
PAIR(getdents,(int fd,char *b,uint64_t n,int64_t *base),(fd,b,n,base))
PAIR(rename,(const char *a,const char *b),(a,b))
PAIR(ftruncate,(int fd,int64_t l),(fd,l))
PAIR(truncate,(const char *p,int64_t l),(p,l))
PAIR(fsync,(int fd),(fd))
static ABI int64_t posix_access(const char *p,int m) { return posix(do_access(p,m)); }
static ABI int64_t sce_mkdir(const char *p,int m) { return sce(path_op(p,0,m)); }
static ABI int64_t posix_mkdir(const char *p,int m) { return posix(path_op(p,0,m)); }
static ABI int64_t sce_rmdir(const char *p) { return sce(path_op(p,1,0)); }
static ABI int64_t posix_rmdir(const char *p) { return posix(path_op(p,1,0)); }
static ABI int64_t sce_unlink(const char *p) { return sce(path_op(p,2,0)); }
static ABI int64_t posix_unlink(const char *p) { return posix(path_op(p,2,0)); }
static ABI int32_t sce_check_reachability(const char *p) {
    GuestStat s; return (int32_t)sce(do_stat(p,&s));
}

static const RuntimeExport exports[]={
    {"sceKernelOpen",sce_open}, {"open",posix_open}, {"_open",posix_open},
    {"sceKernelClose",sce_close}, {"close",posix_close}, {"_close",posix_close},
    {"sceKernelRead",sce_read}, {"read",posix_read}, {"_read",posix_read},
    {"sceKernelPread",sce_pread}, {"pread",posix_pread},
    {"sceKernelWrite",sce_write}, {"write",posix_write}, {"_write",posix_write},
    {"sceKernelPwrite",sce_pwrite}, {"pwrite",posix_pwrite},
    {"sceKernelLseek",sce_lseek}, {"lseek",posix_lseek},
    {"sceKernelFstat",sce_fstat}, {"fstat",posix_fstat},
    {"sceKernelStat",sce_stat}, {"stat",posix_stat},
    {"sceKernelGetdirentries",sce_getdents}, {"getdirentries",posix_getdents},
    {"sceKernelRename",sce_rename}, {"rename",posix_rename},
    {"sceKernelFtruncate",sce_ftruncate}, {"ftruncate",posix_ftruncate},
    {"sceKernelTruncate",sce_truncate}, {"truncate",posix_truncate},
    {"sceKernelFsync",sce_fsync}, {"fsync",posix_fsync},
    {"access",posix_access}, {"sceKernelCheckReachability",sce_check_reachability},
    {"sceKernelMkdir",sce_mkdir}, {"mkdir",posix_mkdir},
    {"sceKernelRmdir",sce_rmdir}, {"rmdir",posix_rmdir},
    {"sceKernelUnlink",sce_unlink}, {"unlink",posix_unlink},
};
uintptr_t runtime_file_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
/* Host-side helpers for other modules (e.g. SaveData, Fios). */
int64_t runtime_file_open(const char *p,int f,int m) { return do_open(p,f,m); }
int64_t runtime_file_close(int fd) { return do_close(fd); }
int64_t runtime_file_read(int fd,void *b,uint64_t n) { return do_read(fd,b,n); }
int64_t runtime_file_pread(int fd,void *b,uint64_t n,int64_t o) { return do_pread(fd,b,n,o); }
int64_t runtime_file_write(int fd,const void *b,uint64_t n) { return do_write(fd,b,n); }
int64_t runtime_file_lseek(int fd,int64_t o,int w) { return do_lseek(fd,o,w); }
int64_t runtime_file_stat(const char *p,void *s) { return do_stat(p,s); }
int64_t runtime_file_fstat(int fd,void *s) { return do_fstat(fd,s); }
int64_t runtime_file_getdents(int fd,char *b,uint64_t n,int64_t *base) { return do_getdents(fd,b,n,base); }
int runtime_file_translate(const char *guest,char *out,size_t size) { return translate(guest,out,size); }
void runtime_file_report(void) {
    printf("Runtime: files opened=%zu, reads=%zu (%llu bytes), writes=%zu, not found=%zu\n",
           opens,reads,(unsigned long long)bytes_read,writes,missing);
}
