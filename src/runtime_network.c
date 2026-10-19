/* Optional native LAN transport for the PS4 networking ABI.
 *
 * Offline remains the default.  BB_NETWORK_MODE=lan opts in and replaces the
 * deliberately disconnected exports in runtime_services.c.  The P2P datagram
 * framing is wire-compatible with Wozzardman/shadp2p commit 6e97b404: two
 * network-byte-order virtual ports followed by the guest payload.  Matching2,
 * shadNet authentication and signaling are separate layers and are intentionally
 * not claimed here.
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#define ORBIS_AF_INET 2
#define ORBIS_SOCK_STREAM 1
#define ORBIS_SOCK_DGRAM 2
#define ORBIS_SOCK_DGRAM_P2P 6
#define ORBIS_SOL_SOCKET 0xffff
#define ORBIS_SO_REUSEADDR 0x0004
#define ORBIS_SO_KEEPALIVE 0x0008
#define ORBIS_SO_BROADCAST 0x0020
#define ORBIS_SO_SNDBUF 0x1001
#define ORBIS_SO_RCVBUF 0x1002
#define ORBIS_SO_ERROR 0x1007
#define ORBIS_SO_TYPE 0x1008
#define ORBIS_SO_SNDTIMEO 0x1105
#define ORBIS_SO_RCVTIMEO 0x1106
#define ORBIS_SO_NBIO 0x1200
#define ORBIS_MSG_PEEK 0x0002
#define ORBIS_MSG_WAITALL 0x0040
#define ORBIS_MSG_DONTWAIT 0x0080
#define ORBIS_EPOLL_CTL_ADD 1
#define ORBIS_EPOLL_CTL_MOD 2
#define ORBIS_EPOLL_CTL_DEL 3
#define ORBIS_EPOLLIN 0x1
#define ORBIS_EPOLLOUT 0x2
#define ORBIS_EPOLLERR 0x8
#define ORBIS_EPOLLHUP 0x10
#define ORBIS_NET_EBADF 9
#define ORBIS_NET_EINVAL 22
#define ORBIS_NET_EWOULDBLOCK 35
#define ORBIS_NET_EINPROGRESS 36
#define ORBIS_NET_EPROTONOSUPPORT 43
#define ORBIS_NET_EOPNOTSUPP 45
#define ORBIS_NET_EAFNOSUPPORT 47
#define ORBIS_NET_EADDRINUSE 48
#define ORBIS_NET_EADDRNOTAVAIL 49
#define ORBIS_NET_ENETDOWN 50
#define ORBIS_NET_ENETUNREACH 51
#define ORBIS_NET_ECONNABORTED 53
#define ORBIS_NET_ECONNRESET 54
#define ORBIS_NET_ENOBUFS 55
#define ORBIS_NET_EISCONN 56
#define ORBIS_NET_ENOTCONN 57
#define ORBIS_NET_ETIMEDOUT 60
#define ORBIS_NET_ECONNREFUSED 61
#define ORBIS_NET_EHOSTUNREACH 65
#define ORBIS_NET_CTL_INVALID_ADDR ((int32_t)0x80412107)

#ifdef _WIN32
typedef SOCKET NativeSocket;
#define INVALID_NATIVE INVALID_SOCKET
#define CLOSE_NATIVE closesocket
#else
typedef int NativeSocket;
#define INVALID_NATIVE (-1)
#define CLOSE_NATIVE close
#endif

typedef struct {
    uint8_t len, family;
    uint16_t port;
    uint32_t address;
    uint16_t vport;
    uint8_t zero[6];
} OrbisSockaddrIn;

typedef union { void *ptr; uint32_t u32; int fd; uint64_t u64; } OrbisEpollData;
typedef struct { uint32_t events, pad; uint64_t ident; OrbisEpollData data; } OrbisEpollEvent;

typedef struct {
    int used, type, nonblocking, p2p_bound, connected;
    NativeSocket native;
    uint16_t p2p_vport;
    OrbisSockaddrIn peer;
} LanSocket;

#define SOCKET_SLOTS 128
#define SOCKET_ID_BASE 0x400
#define EPOLL_SLOTS 32
#define EPOLL_ID_BASE 0x600
#define EPOLL_ITEMS 64
typedef struct { int used, socket_id; OrbisEpollEvent event; } EpollItem;
typedef struct { int used; EpollItem items[EPOLL_ITEMS]; } LanEpoll;

static HostMutex network_lock=HOST_MUTEX_INIT;
static LanSocket sockets[SOCKET_SLOTS];
static LanEpoll epolls[EPOLL_SLOTS];
static int32_t network_errno;
static uint16_t next_vport=30000;
static int network_initialized;
/* Optional local tsnet sidecar. BBTS + peer IPv4 (network order) + port (network
 * order) + the original four-byte-vport P2P datagram. Never enabled by default. */
#define TSNET_BRIDGE_HEADER 10
static uint16_t tsnet_bridge_port;
static uint32_t tsnet_node_ip;

static int lan_enabled(void) {
    const char *mode=getenv("BB_NETWORK_MODE");
    return mode && (!strcmp(mode,"lan") || !strcmp(mode,"shadnet"));
}

static int last_native_error(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static int guest_error(int error) {
#ifdef _WIN32
    switch (error) {
    case WSAEBADF: case WSAENOTSOCK: return ORBIS_NET_EBADF;
    case WSAEINVAL: return ORBIS_NET_EINVAL;
    case WSAEWOULDBLOCK: return ORBIS_NET_EWOULDBLOCK;
    case WSAEINPROGRESS: case WSAEALREADY: return ORBIS_NET_EINPROGRESS;
    case WSAEPROTONOSUPPORT: return ORBIS_NET_EPROTONOSUPPORT;
    case WSAEOPNOTSUPP: return ORBIS_NET_EOPNOTSUPP;
    case WSAEAFNOSUPPORT: return ORBIS_NET_EAFNOSUPPORT;
    case WSAEADDRINUSE: return ORBIS_NET_EADDRINUSE;
    case WSAEADDRNOTAVAIL: return ORBIS_NET_EADDRNOTAVAIL;
    case WSAENETDOWN: return ORBIS_NET_ENETDOWN;
    case WSAENETUNREACH: return ORBIS_NET_ENETUNREACH;
    case WSAECONNABORTED: return ORBIS_NET_ECONNABORTED;
    case WSAECONNRESET: return ORBIS_NET_ECONNRESET;
    case WSAENOBUFS: return ORBIS_NET_ENOBUFS;
    case WSAEISCONN: return ORBIS_NET_EISCONN;
    case WSAENOTCONN: return ORBIS_NET_ENOTCONN;
    case WSAETIMEDOUT: return ORBIS_NET_ETIMEDOUT;
    case WSAECONNREFUSED: return ORBIS_NET_ECONNREFUSED;
    case WSAEHOSTUNREACH: return ORBIS_NET_EHOSTUNREACH;
    default: return ORBIS_NET_EINVAL;
    }
#else
    return runtime_guest_errno(error);
#endif
}

static int fail_native(void) { network_errno=guest_error(last_native_error()); return -1; }
static int fail_guest(int error) { network_errno=error; return -1; }

static LanSocket *socket_at(int id) {
    int slot=id-SOCKET_ID_BASE;
    return slot>=0 && slot<SOCKET_SLOTS && sockets[slot].used ? &sockets[slot] : NULL;
}

static LanEpoll *epoll_at(int id) {
    int slot=id-EPOLL_ID_BASE;
    return slot>=0 && slot<EPOLL_SLOTS && epolls[slot].used ? &epolls[slot] : NULL;
}

static int alloc_socket(NativeSocket native,int type) {
    host_lock(&network_lock);
    for (int i=0;i<SOCKET_SLOTS;++i) if (!sockets[i].used) {
        memset(&sockets[i],0,sizeof(sockets[i]));
        sockets[i].used=1; sockets[i].type=type; sockets[i].native=native;
        host_unlock(&network_lock); return SOCKET_ID_BASE+i;
    }
    host_unlock(&network_lock); CLOSE_NATIVE(native); return fail_guest(ORBIS_NET_ENOBUFS);
}

static int native_flags(int flags) {
    int out=0;
    if (flags&ORBIS_MSG_PEEK) out|=MSG_PEEK;
#ifdef MSG_WAITALL
    if (flags&ORBIS_MSG_WAITALL) out|=MSG_WAITALL;
#endif
#ifndef _WIN32
    if (flags&ORBIS_MSG_DONTWAIT) out|=MSG_DONTWAIT;
#endif
    return out;
}

static int to_native_addr(const OrbisSockaddrIn *in,uint32_t size,struct sockaddr_in *out) {
    if (!in || size<sizeof(*in)) return fail_guest(ORBIS_NET_EINVAL);
    if (in->family!=ORBIS_AF_INET) return fail_guest(ORBIS_NET_EAFNOSUPPORT);
    memset(out,0,sizeof(*out)); out->sin_family=AF_INET;
    out->sin_port=in->port; out->sin_addr.s_addr=in->address; return 0;
}

static void from_native_addr(const struct sockaddr_in *in,uint16_t vport,OrbisSockaddrIn *out) {
    memset(out,0,sizeof(*out)); out->len=sizeof(*out); out->family=ORBIS_AF_INET;
    out->port=in->sin_port; out->address=in->sin_addr.s_addr; out->vport=vport;
}

static ABI int32_t net_init(void) {
    if (network_initialized) return 0;
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2,2),&data)) return fail_native();
#endif
    const char *bridge=getenv("BB_TSNET_BRIDGE_PORT");
    if (bridge&&*bridge) {
        char *end=NULL; long port=strtol(bridge,&end,10);
        const char *node=getenv("BB_TSNET_NODE_IP");
        struct in_addr parsed={0};
        if (end==bridge||*end||port<1||port>65535||!node||
            inet_pton(AF_INET,node,&parsed)!=1||parsed.s_addr==0)
            return fail_guest(ORBIS_NET_EINVAL);
        tsnet_bridge_port=(uint16_t)port;
        tsnet_node_ip=parsed.s_addr;
    }
    network_initialized=1;
    puts("Runtime: native LAN transport enabled (room matching/signaling still in development)");
    return 0;
}

static ABI int32_t net_term(void) { return 0; }
static ABI int32_t *net_errno_loc(void) { return &network_errno; }

static ABI int32_t net_socket(const char *name,int family,int type,int protocol) {
    (void)name;
    if (!network_initialized) net_init();
    if (family!=ORBIS_AF_INET) return fail_guest(ORBIS_NET_EAFNOSUPPORT);
    int native_type=type==ORBIS_SOCK_STREAM ? SOCK_STREAM :
                    (type==ORBIS_SOCK_DGRAM || type==ORBIS_SOCK_DGRAM_P2P) ? SOCK_DGRAM : 0;
    if (!native_type) return fail_guest(ORBIS_NET_EPROTONOSUPPORT);
    NativeSocket s=socket(AF_INET,native_type,protocol);
    if (s==INVALID_NATIVE) return fail_native();
    return alloc_socket(s,type);
}

static ABI int32_t net_close(int id) {
    host_lock(&network_lock); LanSocket *s=socket_at(id);
    if (!s) { host_unlock(&network_lock); return fail_guest(ORBIS_NET_EBADF); }
    NativeSocket native=s->native; memset(s,0,sizeof(*s)); s->native=INVALID_NATIVE;
    host_unlock(&network_lock);
    return CLOSE_NATIVE(native)==0 ? 0 : fail_native();
}

static int p2p_ensure_bound(LanSocket *s,uint16_t requested) {
    if (s->p2p_bound) return 0;
    struct sockaddr_in local; memset(&local,0,sizeof(local));
    local.sin_family=AF_INET; local.sin_addr.s_addr=htonl(INADDR_ANY); local.sin_port=0;
    const char *port=getenv("BB_P2P_PORT");
    if (port && *port) { long value=strtol(port,NULL,10); if (value>0 && value<65536) local.sin_port=htons((uint16_t)value); }
    if (bind(s->native,(struct sockaddr *)&local,sizeof(local))) return fail_native();
    s->p2p_vport=requested ? requested : htons(next_vport++);
    if (s->p2p_vport==0xffff) s->p2p_vport=htons(next_vport++);
    s->p2p_bound=1; return 0;
}

static ABI int32_t net_bind(int id,const OrbisSockaddrIn *address,uint32_t size) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
    if (s->type==ORBIS_SOCK_DGRAM_P2P) return p2p_ensure_bound(s,address ? address->vport : 0);
    struct sockaddr_in native; if (to_native_addr(address,size,&native)) return -1;
    return bind(s->native,(struct sockaddr *)&native,sizeof(native)) ? fail_native() : 0;
}

static ABI int32_t net_connect(int id,const OrbisSockaddrIn *address,uint32_t size) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
    if (s->type==ORBIS_SOCK_DGRAM_P2P) {
        if (!address || size<sizeof(*address) || address->family!=ORBIS_AF_INET) return fail_guest(ORBIS_NET_EINVAL);
        if (p2p_ensure_bound(s,0)) return -1;
        s->peer=*address; s->connected=1; return 0;
    }
    struct sockaddr_in native; if (to_native_addr(address,size,&native)) return -1;
    return connect(s->native,(struct sockaddr *)&native,sizeof(native)) ? fail_native() : 0;
}

static ABI int32_t net_listen(int id,int backlog) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
    if (s->type==ORBIS_SOCK_DGRAM_P2P) return fail_guest(ORBIS_NET_EOPNOTSUPP);
    return listen(s->native,backlog) ? fail_native() : 0;
}

static ABI int32_t net_accept(int id,OrbisSockaddrIn *address,uint32_t *size) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
    struct sockaddr_in native; socklen_t native_size=sizeof(native);
    NativeSocket accepted=accept(s->native,(struct sockaddr *)&native,&native_size);
    if (accepted==INVALID_NATIVE) return fail_native();
    int result=alloc_socket(accepted,ORBIS_SOCK_STREAM);
    if (result>=0 && address && size && *size>=sizeof(*address)) { from_native_addr(&native,0,address); *size=sizeof(*address); }
    return result;
}

static int p2p_send(LanSocket *s,const void *data,uint64_t length,int flags,const OrbisSockaddrIn *to,uint32_t size) {
    (void)flags;
    if (length>65503-(tsnet_bridge_port?TSNET_BRIDGE_HEADER:0)) return fail_guest(40);
    if (p2p_ensure_bound(s,0)) return -1;
    const OrbisSockaddrIn *target=to;
    if (!target) { if (!s->connected) return fail_guest(ORBIS_NET_ENOTCONN); target=&s->peer; }
    if (size && size<sizeof(*target)) return fail_guest(ORBIS_NET_EINVAL);
    struct sockaddr_in native; if (to_native_addr(target,sizeof(*target),&native)) return -1;
    size_t offset=tsnet_bridge_port?TSNET_BRIDGE_HEADER:0;
    unsigned char *packet=malloc((size_t)length+4+offset); if (!packet) return fail_guest(ORBIS_NET_ENOBUFS);
    if (offset) {
        memcpy(packet,"BBTS",4);
        memcpy(packet+4,&native.sin_addr.s_addr,4);
        memcpy(packet+8,&native.sin_port,2);
        native.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        native.sin_port=htons(tsnet_bridge_port);
    }
    memcpy(packet+offset,&s->p2p_vport,2); memcpy(packet+offset+2,&target->vport,2);
    if (length) memcpy(packet+offset+4,data,(size_t)length);
    int sent=(int)sendto(s->native,(const char *)packet,(int)length+4+(int)offset,0,(struct sockaddr *)&native,sizeof(native));
    free(packet); if (sent<0) return fail_native(); return sent-4-(int)offset;
}

static ABI int32_t net_sendto(int id,const void *data,uint64_t length,int flags,const OrbisSockaddrIn *to,uint32_t size) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
    if (s->type==ORBIS_SOCK_DGRAM_P2P) return p2p_send(s,data,length,flags,to,size);
    struct sockaddr_in native; if (to_native_addr(to,size,&native)) return -1;
    if(getenv("BB_NETWORK_TRACE"))fprintf(stderr,"LAN sendto socket=%llu address=%08x port=%u bytes=%llu\n",(unsigned long long)s->native,(unsigned)ntohl(native.sin_addr.s_addr),(unsigned)ntohs(native.sin_port),(unsigned long long)length);
    int result=(int)sendto(s->native,(const char *)data,(int)length,native_flags(flags),(struct sockaddr *)&native,sizeof(native));
    return result<0 ? fail_native() : result;
}

static ABI int32_t net_send(int id,const void *data,uint64_t length,int flags) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
    if (s->type==ORBIS_SOCK_DGRAM_P2P) return p2p_send(s,data,length,flags,NULL,0);
    int result=(int)send(s->native,(const char *)data,(int)length,native_flags(flags));
    return result<0 ? fail_native() : result;
}

static int p2p_recv(LanSocket *s,void *data,uint64_t length,int flags,OrbisSockaddrIn *from,uint32_t *size) {
    if (p2p_ensure_bound(s,0)) return -1;
    if (length>65503) return fail_guest(ORBIS_NET_EINVAL);
    size_t offset=tsnet_bridge_port?TSNET_BRIDGE_HEADER:0;
    unsigned char *packet=malloc((size_t)length+4+offset); if (!packet) return fail_guest(ORBIS_NET_ENOBUFS);
    struct sockaddr_in native; socklen_t native_size=sizeof(native);
    int received=(int)recvfrom(s->native,(char *)packet,(int)length+4+(int)offset,native_flags(flags),(struct sockaddr *)&native,&native_size);
    if (received<0) { free(packet); return fail_native(); }
    if (offset) {
        if (native.sin_addr.s_addr!=htonl(INADDR_LOOPBACK)||native.sin_port!=htons(tsnet_bridge_port)||
            received<TSNET_BRIDGE_HEADER||memcmp(packet,"BBTS",4)) {
            free(packet); return fail_guest(ORBIS_NET_EWOULDBLOCK);
        }
        memcpy(&native.sin_addr.s_addr,packet+4,4);
        memcpy(&native.sin_port,packet+8,2);
        if (!native.sin_addr.s_addr||!native.sin_port) {
            free(packet); return fail_guest(ORBIS_NET_EWOULDBLOCK);
        }
    }
    if (received<(int)(4+offset)) { free(packet); return fail_guest(ORBIS_NET_EINVAL); }
    uint16_t source_vport,dest_vport; memcpy(&source_vport,packet+offset,2); memcpy(&dest_vport,packet+offset+2,2);
    if (dest_vport!=s->p2p_vport && dest_vport!=0xffff) { free(packet); return fail_guest(ORBIS_NET_EWOULDBLOCK); }
    int payload=received-4-(int)offset; if (payload>(int)length) payload=(int)length;
    if (payload) memcpy(data,packet+offset+4,(size_t)payload); free(packet);
    if (from && size && *size>=sizeof(*from)) { from_native_addr(&native,source_vport,from); *size=sizeof(*from); }
    return payload;
}

static ABI int32_t net_recvfrom(int id,void *data,uint64_t length,int flags,OrbisSockaddrIn *from,uint32_t *size) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
    if (s->type==ORBIS_SOCK_DGRAM_P2P) return p2p_recv(s,data,length,flags,from,size);
    struct sockaddr_in native; socklen_t native_size=sizeof(native);
    int result=(int)recvfrom(s->native,(char *)data,(int)length,native_flags(flags),(struct sockaddr *)&native,&native_size);
    if (result<0) return fail_native();
    if (from && size && *size>=sizeof(*from)) { from_native_addr(&native,0,from); *size=sizeof(*from); }
    return result;
}

static ABI int32_t net_recv(int id,void *data,uint64_t length,int flags) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
    if (s->type==ORBIS_SOCK_DGRAM_P2P) return p2p_recv(s,data,length,flags,NULL,NULL);
    int result=(int)recv(s->native,(char *)data,(int)length,native_flags(flags));
    return result<0 ? fail_native() : result;
}

static int set_nonblocking(LanSocket *s,int enabled) {
#ifdef _WIN32
    u_long value=enabled!=0; if (ioctlsocket(s->native,FIONBIO,&value)) return fail_native();
#else
    int flags=fcntl(s->native,F_GETFL,0); if (flags<0 || fcntl(s->native,F_SETFL,enabled ? flags|O_NONBLOCK : flags&~O_NONBLOCK)<0) return fail_native();
#endif
    s->nonblocking=enabled!=0; return 0;
}

static int native_option(int option) {
    switch(option) {
    case ORBIS_SO_REUSEADDR: return SO_REUSEADDR; case ORBIS_SO_KEEPALIVE: return SO_KEEPALIVE;
    case ORBIS_SO_BROADCAST: return SO_BROADCAST; case ORBIS_SO_SNDBUF: return SO_SNDBUF;
    case ORBIS_SO_RCVBUF: return SO_RCVBUF; case ORBIS_SO_ERROR: return SO_ERROR;
    case ORBIS_SO_TYPE: return SO_TYPE; default: return -1;
    }
}

static ABI int32_t net_setsockopt(int id,int level,int option,const void *value,uint32_t length) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
    if (level==ORBIS_SOL_SOCKET && option==ORBIS_SO_NBIO) return length>=4 ? set_nonblocking(s,*(const int *)value) : fail_guest(ORBIS_NET_EINVAL);
    int native=native_option(option); if (level!=ORBIS_SOL_SOCKET || native<0) return 0; /* tolerated game hints */
    return setsockopt(s->native,SOL_SOCKET,native,(const char *)value,(socklen_t)length) ? fail_native() : 0;
}

static ABI int32_t net_getsockopt(int id,int level,int option,void *value,uint32_t *length) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
    if (!value || !length) return fail_guest(ORBIS_NET_EINVAL);
    if (level==ORBIS_SOL_SOCKET && option==ORBIS_SO_NBIO) { if (*length<4) return fail_guest(ORBIS_NET_EINVAL); *(int *)value=s->nonblocking; *length=4; return 0; }
    int native=native_option(option); if (level!=ORBIS_SOL_SOCKET || native<0) return fail_guest(ORBIS_NET_EOPNOTSUPP);
    socklen_t native_length=(socklen_t)*length;
    if (getsockopt(s->native,SOL_SOCKET,native,(char *)value,&native_length)) return fail_native();
    *length=(uint32_t)native_length; return 0;
}

static ABI int32_t net_getsockname(int id,OrbisSockaddrIn *address,uint32_t *size) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
    if (!address || !size || *size<sizeof(*address)) return fail_guest(ORBIS_NET_EINVAL);
    if (s->type==ORBIS_SOCK_DGRAM_P2P && p2p_ensure_bound(s,0)) return -1;
    struct sockaddr_in native; socklen_t native_size=sizeof(native);
    if (getsockname(s->native,(struct sockaddr *)&native,&native_size)) return fail_native();
    if(getenv("BB_NETWORK_TRACE"))fprintf(stderr,"LAN getsockname socket=%llu address=%08x port=%u\n",(unsigned long long)s->native,(unsigned)ntohl(native.sin_addr.s_addr),(unsigned)ntohs(native.sin_port));
    from_native_addr(&native,s->p2p_vport,address); *size=sizeof(*address); return 0;
}

static ABI int32_t net_shutdown(int id,int how) {
    LanSocket *s=socket_at(id); if (!s) return fail_guest(ORBIS_NET_EBADF);
#ifdef _WIN32
    int native_how=how==0 ? SD_RECEIVE : how==1 ? SD_SEND : SD_BOTH;
#else
    int native_how=how;
#endif
    return shutdown(s->native,native_how) ? fail_native() : 0;
}

static ABI int32_t resolver_create(const char *name,int pool,int flags) { (void)name;(void)pool;(void)flags; return 1; }
static ABI int32_t resolver_destroy(int id) { (void)id; return 0; }
static ABI int32_t resolver_ntoa(int id,const char *host,uint32_t *address,int timeout,int retry,int flags) {
    (void)id;(void)timeout;(void)retry;(void)flags;
    if (!host || !address) return fail_guest(ORBIS_NET_EINVAL);
    struct addrinfo hints,*results=NULL; memset(&hints,0,sizeof(hints)); hints.ai_family=AF_INET; hints.ai_socktype=SOCK_STREAM;
    int error=getaddrinfo(host,NULL,&hints,&results); if (error || !results) return fail_guest(ORBIS_NET_EHOSTUNREACH);
    *address=((struct sockaddr_in *)results->ai_addr)->sin_addr.s_addr; freeaddrinfo(results); return 0;
}

static ABI int32_t epoll_create(const char *name,int flags) {
    (void)name;(void)flags; host_lock(&network_lock);
    for (int i=0;i<EPOLL_SLOTS;++i) if (!epolls[i].used) { memset(&epolls[i],0,sizeof(epolls[i])); epolls[i].used=1; host_unlock(&network_lock); return EPOLL_ID_BASE+i; }
    host_unlock(&network_lock); return fail_guest(ORBIS_NET_ENOBUFS);
}
static ABI int32_t epoll_destroy(int id) { LanEpoll *e=epoll_at(id); if (!e) return fail_guest(ORBIS_NET_EBADF); memset(e,0,sizeof(*e)); return 0; }
static ABI int32_t epoll_control(int id,int operation,int socket_id,const OrbisEpollEvent *event) {
    LanEpoll *e=epoll_at(id); if (!e || !socket_at(socket_id)) return fail_guest(ORBIS_NET_EBADF);
    EpollItem *empty=NULL,*found=NULL; for(int i=0;i<EPOLL_ITEMS;++i){ if(e->items[i].used&&e->items[i].socket_id==socket_id)found=&e->items[i]; if(!e->items[i].used&&!empty)empty=&e->items[i]; }
    if(operation==ORBIS_EPOLL_CTL_DEL){ if(found)memset(found,0,sizeof(*found)); return found?0:fail_guest(ORBIS_NET_EBADF); }
    EpollItem *item=found?found:empty; if(!item||!event)return fail_guest(ORBIS_NET_EINVAL);
    item->used=1; item->socket_id=socket_id; item->event=*event; return 0;
}
static ABI int32_t epoll_wait(int id,OrbisEpollEvent *events,int maximum,int timeout_us) {
    LanEpoll *e=epoll_at(id); if (!e || !events || maximum<=0) return fail_guest(ORBIS_NET_EINVAL);
#ifdef _WIN32
    WSAPOLLFD descriptors[EPOLL_ITEMS]; EpollItem *items[EPOLL_ITEMS]; ULONG descriptor_count=0;
    for(int i=0;i<EPOLL_ITEMS;++i)if(e->items[i].used){
        LanSocket*s=socket_at(e->items[i].socket_id);if(!s)continue;
        descriptors[descriptor_count].fd=s->native; descriptors[descriptor_count].events=0; descriptors[descriptor_count].revents=0;
        if(e->items[i].event.events&ORBIS_EPOLLIN)descriptors[descriptor_count].events|=POLLRDNORM;
        if(e->items[i].event.events&ORBIS_EPOLLOUT)descriptors[descriptor_count].events|=POLLWRNORM;
        items[descriptor_count]=&e->items[i]; ++descriptor_count;
    }
    if(!descriptor_count)return 0;
    int timeout_ms=timeout_us<0?-1:(timeout_us+999)/1000;
    int ready=WSAPoll(descriptors,descriptor_count,timeout_ms);
    if(getenv("BB_NETWORK_TRACE"))fprintf(stderr,"LAN epoll WSAPoll ready=%d sockets=%lu error=%d\n",ready,(unsigned long)descriptor_count,ready<0?last_native_error():0);
    if(ready<0)return fail_native(); if(!ready)return 0;
    int count=0;
    for(ULONG i=0;i<descriptor_count&&count<maximum;++i)if(descriptors[i].revents){
        uint32_t hit=0;
        if(descriptors[i].revents&(POLLRDNORM|POLLIN))hit|=ORBIS_EPOLLIN;
        if(descriptors[i].revents&(POLLWRNORM|POLLOUT))hit|=ORBIS_EPOLLOUT;
        if(descriptors[i].revents&POLLERR)hit|=ORBIS_EPOLLERR;
        if(descriptors[i].revents&POLLHUP)hit|=ORBIS_EPOLLHUP;
        if(hit){events[count]=items[i]->event;events[count].events=hit;events[count].ident=(uint64_t)items[i]->socket_id;++count;}
    }
    return count;
#else
    fd_set reads,writes,errors; FD_ZERO(&reads);FD_ZERO(&writes);FD_ZERO(&errors); NativeSocket highest=0;
    for(int i=0;i<EPOLL_ITEMS;++i)if(e->items[i].used){ LanSocket*s=socket_at(e->items[i].socket_id);if(!s)continue; if(e->items[i].event.events&ORBIS_EPOLLIN)FD_SET(s->native,&reads); if(e->items[i].event.events&ORBIS_EPOLLOUT)FD_SET(s->native,&writes); FD_SET(s->native,&errors);if(s->native>highest)highest=s->native; }
    struct timeval tv,*ptv=NULL; if(timeout_us>=0){tv.tv_sec=timeout_us/1000000;tv.tv_usec=timeout_us%1000000;ptv=&tv;}
    int ready=select((int)highest+1,&reads,&writes,&errors,ptv);
    if(getenv("BB_NETWORK_TRACE")) fprintf(stderr,"LAN epoll select ready=%d native=%llu error=%d\n",ready,(unsigned long long)highest,ready<0?last_native_error():0);
    if(ready<0)return fail_native(); if(!ready)return 0;
    int count=0; for(int i=0;i<EPOLL_ITEMS&&count<maximum;++i)if(e->items[i].used){LanSocket*s=socket_at(e->items[i].socket_id);if(!s)continue;uint32_t hit=0;if(FD_ISSET(s->native,&reads))hit|=ORBIS_EPOLLIN;if(FD_ISSET(s->native,&writes))hit|=ORBIS_EPOLLOUT;if(FD_ISSET(s->native,&errors))hit|=ORBIS_EPOLLERR;if(hit){events[count]=e->items[i].event;events[count].events=hit;events[count].ident=(uint64_t)e->items[i].socket_id;++count;}}
    return count;
#endif
}
static ABI int32_t epoll_abort(int id,int flags) { (void)id;(void)flags; return 0; }

static ABI int32_t netctl_state(int32_t *state) { if(!state)return ORBIS_NET_CTL_INVALID_ADDR;*state=3;return 0; }
static ABI int32_t netctl_info(int code,void *info) {
    if(!info)return ORBIS_NET_CTL_INVALID_ADDR; memset(info,0,256);
    if(code==1||code==3||code==4||code==11)*(uint32_t *)info=code==3?1500:1;
    else if(code==14&&tsnet_bridge_port) {
        struct in_addr ip={0}; ip.s_addr=tsnet_node_ip;
        inet_ntop(AF_INET,&ip,(char *)info,256);
    } else if(code>=14&&code<=18)strcpy((char *)info,code==14?"127.0.0.1":code==15?"255.0.0.0":code==16?"0.0.0.0":"1.1.1.1");
    return 0;
}
static ABI int32_t netctl_nat(uint32_t *info) { if(!info)return ORBIS_NET_CTL_INVALID_ADDR;info[1]=1;info[2]=2;info[3]=0;return 0; }

static const RuntimeExport exports[]={
    {"sceNetInit",net_init},{"sceNetTerm",net_term},{"sceNetErrnoLoc",net_errno_loc},
    {"sceNetSocket",net_socket},{"sceNetSocketClose",net_close},{"sceNetSocketAbort",net_close},
    {"sceNetBind",net_bind},{"sceNetConnect",net_connect},{"sceNetListen",net_listen},{"sceNetAccept",net_accept},
    {"sceNetSend",net_send},{"sceNetSendto",net_sendto},{"sceNetRecv",net_recv},{"sceNetRecvfrom",net_recvfrom},
    {"sceNetSetsockopt",net_setsockopt},{"sceNetGetsockopt",net_getsockopt},{"sceNetGetsockname",net_getsockname},{"sceNetShutdown",net_shutdown},
    {"sceNetResolverCreate",resolver_create},{"sceNetResolverDestroy",resolver_destroy},{"sceNetResolverStartNtoa",resolver_ntoa},
    {"sceNetEpollCreate",epoll_create},{"sceNetEpollDestroy",epoll_destroy},{"sceNetEpollControl",epoll_control},{"sceNetEpollWait",epoll_wait},{"sceNetEpollAbort",epoll_abort},
    {"sceNetCtlGetState",netctl_state},{"sceNetCtlGetInfo",netctl_info},{"sceNetCtlGetNatInfo",netctl_nat}
};

uintptr_t runtime_network_resolve(const char *name) {
    return lan_enabled() ? RUNTIME_LOOKUP(exports,name) : 0;
}
