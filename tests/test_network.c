#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#endif
#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* runtime_network.c is tested on its own so Windows does not need the pthread
 * runtime test binary.  The production resolver supplies these helpers. */
const char *runtime_symbol(const char *name) { return name; }
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for(size_t i=0;i<count;++i)if(!strcmp(table[i].name,name))return(uintptr_t)table[i].function;
    return 0;
}
int32_t runtime_guest_errno(int host_errno) { return host_errno; }

typedef struct {
    uint8_t len, family;
    uint16_t port;
    uint32_t address;
    uint16_t vport;
    uint8_t zero[6];
} TestNetAddress;
typedef union { void *ptr; uint32_t u32; int fd; uint64_t u64; } TestEpollData;
typedef struct { uint32_t events, pad; uint64_t ident; TestEpollData data; } TestEpollEvent;
typedef int32_t (ABI *NetInit)(void);
typedef int32_t (ABI *NetSocket)(const char *,int,int,int);
typedef int32_t (ABI *NetBind)(int,const TestNetAddress *,uint32_t);
typedef int32_t (ABI *NetAddressOp)(int,TestNetAddress *,uint32_t *);
typedef int32_t (ABI *NetSendTo)(int,const void *,uint64_t,int,const TestNetAddress *,uint32_t);
typedef int32_t (ABI *NetRecvFrom)(int,void *,uint64_t,int,TestNetAddress *,uint32_t *);
typedef int32_t (ABI *NetClose)(int);
typedef int32_t (ABI *NetCtlState)(int32_t *);
typedef int32_t (ABI *EpollCreate)(const char *,int);
typedef int32_t (ABI *EpollControl)(int,int,int,const TestEpollEvent *);
typedef int32_t (ABI *EpollWait)(int,TestEpollEvent *,int,int);
typedef int32_t (ABI *EpollDestroy)(int);

static void set_lan_mode(void) {
#ifdef _WIN32
    assert(_putenv_s("BB_NETWORK_MODE","lan")==0);
#else
    assert(setenv("BB_NETWORK_MODE","lan",1)==0);
#endif
}

int main(void) {
#ifdef _WIN32
    assert(_putenv_s("BB_NETWORK_MODE","")==0);
#else
    assert(unsetenv("BB_NETWORK_MODE")==0);
#endif
    assert(runtime_network_resolve("sceNetSocket")==0);
    set_lan_mode();
    NetInit init=(NetInit)runtime_network_resolve("sceNetInit");
    NetSocket create=(NetSocket)runtime_network_resolve("sceNetSocket");
    NetBind bind_guest=(NetBind)runtime_network_resolve("sceNetBind");
    NetAddressOp getsockname_guest=(NetAddressOp)runtime_network_resolve("sceNetGetsockname");
    NetSendTo sendto_guest=(NetSendTo)runtime_network_resolve("sceNetSendto");
    NetRecvFrom recvfrom_guest=(NetRecvFrom)runtime_network_resolve("sceNetRecvfrom");
    NetClose close_guest=(NetClose)runtime_network_resolve("sceNetSocketClose");
    NetCtlState state_guest=(NetCtlState)runtime_network_resolve("sceNetCtlGetState");
    EpollCreate epoll_create=(EpollCreate)runtime_network_resolve("sceNetEpollCreate");
    EpollControl epoll_control=(EpollControl)runtime_network_resolve("sceNetEpollControl");
    EpollWait epoll_wait=(EpollWait)runtime_network_resolve("sceNetEpollWait");
    EpollDestroy epoll_destroy=(EpollDestroy)runtime_network_resolve("sceNetEpollDestroy");
    assert(init&&create&&bind_guest&&getsockname_guest&&sendto_guest&&recvfrom_guest&&close_guest);
    assert(init()==0);
    int32_t state=0; assert(state_guest(&state)==0&&state==3);

    int receiver=create("test-rx",2,2,17),sender=create("test-tx",2,2,17);
    assert(receiver>=0&&sender>=0);
    TestNetAddress target={sizeof(TestNetAddress),2,0,htonl(INADDR_LOOPBACK),0,{0}};
    assert(bind_guest(receiver,&target,sizeof(target))==0);
    uint32_t size=sizeof(target); assert(getsockname_guest(receiver,&target,&size)==0&&target.port);
    int poll=epoll_create("test",0); assert(poll>=0);
    TestEpollEvent interest={1,0,0,{.u64=0xabc}};
    assert(epoll_control(poll,1,receiver,&interest)==0);
    const char hello[]="bbport-lan";
    assert(sendto_guest(sender,hello,sizeof(hello),0,&target,sizeof(target))==(int)sizeof(hello));
    TestEpollEvent ready={0}; assert(epoll_wait(poll,&ready,1,1000000)==1);
    assert((ready.events&1)&&ready.data.u64==0xabc);
    char buffer[32]={0}; TestNetAddress source={0}; size=sizeof(source);
    assert(recvfrom_guest(receiver,buffer,sizeof(buffer),0,&source,&size)==(int)sizeof(hello));
    assert(!strcmp(buffer,hello));
    assert(epoll_destroy(poll)==0&&close_guest(sender)==0&&close_guest(receiver)==0);

    receiver=create("p2p-rx",2,6,17); sender=create("p2p-tx",2,6,17);
    assert(receiver>=0&&sender>=0);
    target=(TestNetAddress){sizeof(TestNetAddress),2,0,htonl(INADDR_LOOPBACK),htons(32001),{0}};
    assert(bind_guest(receiver,&target,sizeof(target))==0);
    size=sizeof(target); assert(getsockname_guest(receiver,&target,&size)==0&&target.port&&target.vport==htons(32001));
    /* A P2P listener binds every local interface; use loopback as the remote
     * address for this same-machine wire-format test. */
    target.address=htonl(INADDR_LOOPBACK);
    assert(sendto_guest(sender,hello,sizeof(hello),0,&target,sizeof(target))==(int)sizeof(hello));
    source=(TestNetAddress){0}; size=sizeof(source); memset(buffer,0,sizeof(buffer));
    assert(recvfrom_guest(receiver,buffer,sizeof(buffer),0,&source,&size)==(int)sizeof(hello));
    assert(!strcmp(buffer,hello)&&source.vport!=0);
    assert(close_guest(sender)==0&&close_guest(receiver)==0);
    puts("PASS: native LAN UDP, epoll and shadp2p-compatible P2P framing");
    return 0;
}
