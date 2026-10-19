#include "lan_match.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET LanSocket;
#define LAN_INVALID_SOCKET INVALID_SOCKET
#define lan_close_socket closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int LanSocket;
#define LAN_INVALID_SOCKET (-1)
#define lan_close_socket close
#endif

#define LAN_MAGIC 0x42424c4eu
#define LAN_VERSION 1u
#define LAN_HEADER_SIZE 12u
#define LAN_MAX_PACKET 256u

enum {
    LAN_PACKET_QUERY = 1,
    LAN_PACKET_ANNOUNCE = 2,
    LAN_PACKET_JOIN = 3,
    LAN_PACKET_JOIN_ACK = 4,
    LAN_PACKET_LEAVE = 5
};

typedef struct {
    LanSocket socket;
    struct sockaddr_in target;
    LanMatchRoom hosted_room;
    uint16_t local_port;
    uint16_t next_member_id;
    int hosting;
} LanMatchImpl;

static void put_u16(uint8_t *out, uint16_t value) {
    out[0]=(uint8_t)(value>>8);out[1]=(uint8_t)value;
}
static void put_u32(uint8_t *out, uint32_t value) {
    out[0]=(uint8_t)(value>>24);out[1]=(uint8_t)(value>>16);
    out[2]=(uint8_t)(value>>8);out[3]=(uint8_t)value;
}
static void put_u64(uint8_t *out, uint64_t value) {
    put_u32(out,(uint32_t)(value>>32));put_u32(out+4,(uint32_t)value);
}
static uint16_t get_u16(const uint8_t *in) { return (uint16_t)((uint16_t)in[0]<<8|in[1]); }
static uint32_t get_u32(const uint8_t *in) {
    return (uint32_t)in[0]<<24|(uint32_t)in[1]<<16|(uint32_t)in[2]<<8|in[3];
}
static uint64_t get_u64(const uint8_t *in) { return (uint64_t)get_u32(in)<<32|get_u32(in+4); }

static size_t copy_npid(uint8_t *out,const char *npid) {
    size_t length=npid?strlen(npid):0;if(length>LAN_MATCH_NPID_SIZE)length=LAN_MATCH_NPID_SIZE;
    out[0]=(uint8_t)length;memset(out+1,0,LAN_MATCH_NPID_SIZE);
    if(length)memcpy(out+1,npid,length);return 1+LAN_MATCH_NPID_SIZE;
}
static int read_npid(const uint8_t *in,size_t size,char out[LAN_MATCH_NPID_SIZE+1]) {
    if(size<1+LAN_MATCH_NPID_SIZE||in[0]>LAN_MATCH_NPID_SIZE)return -1;
    memset(out,0,LAN_MATCH_NPID_SIZE+1);memcpy(out,in+1,in[0]);return 0;
}

static int socket_would_block(void) {
#ifdef _WIN32
    int error=WSAGetLastError();return error==WSAEWOULDBLOCK;
#else
    return errno==EAGAIN||errno==EWOULDBLOCK;
#endif
}

static int set_nonblocking(LanSocket socket) {
#ifdef _WIN32
    u_long enabled=1;return ioctlsocket(socket,FIONBIO,&enabled)==0?0:-1;
#else
    int flags=fcntl(socket,F_GETFL,0);return flags>=0&&fcntl(socket,F_SETFL,flags|O_NONBLOCK)==0?0:-1;
#endif
}

static int address_from_text(struct sockaddr_in *address,const char *ipv4,uint16_t port) {
    memset(address,0,sizeof(*address));address->sin_family=AF_INET;address->sin_port=htons(port);
    return inet_pton(AF_INET,ipv4,&address->sin_addr)==1?0:-1;
}

static int send_packet(LanMatchImpl *impl,const struct sockaddr_in *target,
                       uint8_t type,const uint8_t *payload,uint16_t payload_size) {
    uint8_t packet[LAN_MAX_PACKET];
    if(payload_size>sizeof(packet)-LAN_HEADER_SIZE)return -1;
    put_u32(packet,LAN_MAGIC);packet[4]=LAN_VERSION;packet[5]=type;
    put_u16(packet+6,payload_size);put_u32(packet+8,0);
    if(payload_size)memcpy(packet+LAN_HEADER_SIZE,payload,payload_size);
    int sent=sendto(impl->socket,(const char*)packet,(int)(LAN_HEADER_SIZE+payload_size),0,
                    (const struct sockaddr*)target,sizeof(*target));
    return sent==(int)(LAN_HEADER_SIZE+payload_size)?0:-1;
}

static uint16_t write_room(uint8_t *out,const LanMatchRoom *room) {
    put_u64(out,room->room_id);put_u32(out+8,room->world_id);put_u32(out+12,room->flags);
    put_u16(out+16,room->max_members);put_u16(out+18,room->members);put_u16(out+20,room->p2p_port);
    uint16_t offset=(uint16_t)(22+copy_npid(out+22,room->host_npid));
    uint8_t count=room->int_attr_count;if(count>LAN_MATCH_MAX_INT_ATTRS)count=LAN_MATCH_MAX_INT_ATTRS;
    out[offset++]=count;
    for(uint8_t i=0;i<count;i++) {put_u16(out+offset,room->int_attrs[i].id);put_u32(out+offset+2,room->int_attrs[i].value);offset+=6;}
    uint16_t bin_size=room->search_bin.size;if(bin_size>LAN_MATCH_MAX_BIN_SIZE)bin_size=LAN_MATCH_MAX_BIN_SIZE;
    put_u16(out+offset,room->search_bin.id);put_u16(out+offset+2,bin_size);offset+=4;
    if(bin_size)memcpy(out+offset,room->search_bin.data,bin_size);return (uint16_t)(offset+bin_size);
}

static int read_room(const uint8_t *in,size_t size,LanMatchRoom *room) {
    if(size<44)return -1;
    memset(room,0,sizeof(*room));room->room_id=get_u64(in);room->world_id=get_u32(in+8);
    room->flags=get_u32(in+12);room->max_members=get_u16(in+16);room->members=get_u16(in+18);
    room->p2p_port=get_u16(in+20);if(read_npid(in+22,size-22,room->host_npid)!=0)return -1;
    size_t offset=39;room->int_attr_count=in[offset++];
    if(room->int_attr_count>LAN_MATCH_MAX_INT_ATTRS||size<offset+(size_t)room->int_attr_count*6+4)return -1;
    for(uint8_t i=0;i<room->int_attr_count;i++) {room->int_attrs[i].id=get_u16(in+offset);room->int_attrs[i].value=get_u32(in+offset+2);offset+=6;}
    room->search_bin.id=get_u16(in+offset);room->search_bin.size=get_u16(in+offset+2);offset+=4;
    if(room->search_bin.size>LAN_MATCH_MAX_BIN_SIZE||size!=offset+room->search_bin.size)return -1;
    if(room->search_bin.size)memcpy(room->search_bin.data,in+offset,room->search_bin.size);return 0;
}

int lan_match_open(LanMatch *match,uint16_t listen_port,const char *target_ipv4,uint16_t target_port) {
    if(!match||match->impl||!target_ipv4)return -1;
#ifdef _WIN32
    WSADATA data;if(WSAStartup(MAKEWORD(2,2),&data)!=0)return -1;
#endif
    LanMatchImpl *impl=(LanMatchImpl*)calloc(1,sizeof(*impl));
    if(!impl)goto fail_startup;
    impl->socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(impl->socket==LAN_INVALID_SOCKET)goto fail_impl;
    int enabled=1;
    setsockopt(impl->socket,SOL_SOCKET,SO_REUSEADDR,(const char*)&enabled,sizeof(enabled));
    setsockopt(impl->socket,SOL_SOCKET,SO_BROADCAST,(const char*)&enabled,sizeof(enabled));
    struct sockaddr_in local;memset(&local,0,sizeof(local));local.sin_family=AF_INET;
    local.sin_addr.s_addr=htonl(INADDR_ANY);local.sin_port=htons(listen_port);
    if(bind(impl->socket,(struct sockaddr*)&local,sizeof(local))!=0||set_nonblocking(impl->socket)!=0)goto fail_socket;
    socklen_t local_size=(socklen_t)sizeof(local);
    if(getsockname(impl->socket,(struct sockaddr*)&local,&local_size)!=0)goto fail_socket;
    impl->local_port=ntohs(local.sin_port);impl->next_member_id=2;
    if(address_from_text(&impl->target,target_ipv4,target_port)!=0)goto fail_socket;
    match->impl=impl;return 0;
fail_socket:
    lan_close_socket(impl->socket);
fail_impl:
    free(impl);
fail_startup:
#ifdef _WIN32
    WSACleanup();
#endif
    return -1;
}

void lan_match_close(LanMatch *match) {
    if(!match||!match->impl)return;LanMatchImpl *impl=(LanMatchImpl*)match->impl;
    lan_close_socket(impl->socket);free(impl);match->impl=NULL;
#ifdef _WIN32
    WSACleanup();
#endif
}

uint16_t lan_match_local_port(const LanMatch *match) {
    return match&&match->impl?((const LanMatchImpl*)match->impl)->local_port:0;
}

int lan_match_set_target(LanMatch *match,const char *target_ipv4,uint16_t target_port) {
    if(!match||!match->impl||!target_ipv4)return -1;
    return address_from_text(&((LanMatchImpl*)match->impl)->target,target_ipv4,target_port);
}

int lan_match_host(LanMatch *match,const LanMatchRoom *room) {
    if(!match||!match->impl||!room||!room->room_id||!room->max_members)return -1;
    LanMatchImpl *impl=(LanMatchImpl*)match->impl;impl->hosted_room=*room;impl->hosting=1;return 0;
}

int lan_match_search(LanMatch *match,uint32_t world_id) {
    if(!match||!match->impl)return -1;uint8_t payload[4];put_u32(payload,world_id);
    return send_packet((LanMatchImpl*)match->impl,&((LanMatchImpl*)match->impl)->target,
                       LAN_PACKET_QUERY,payload,sizeof(payload));
}

int lan_match_join(LanMatch *match,const LanMatchEvent *room,const char *npid,uint16_t p2p_port) {
    if(!match||!match->impl||!room||room->type!=LAN_MATCH_EVENT_ROOM_FOUND||!room->room.room_id)return -1;
    uint8_t payload[27];put_u64(payload,room->room.room_id);put_u16(payload+8,p2p_port);
    copy_npid(payload+10,npid);
    struct sockaddr_in target;memset(&target,0,sizeof(target));target.sin_family=AF_INET;
    target.sin_addr.s_addr=room->peer_ipv4_be;target.sin_port=htons(room->peer_discovery_port);
    return send_packet((LanMatchImpl*)match->impl,&target,LAN_PACKET_JOIN,payload,sizeof(payload));
}

int lan_match_leave(LanMatch *match,const LanMatchEvent *room,const char *npid) {
    if(!match||!match->impl||!room||!room->room.room_id)return -1;
    uint8_t payload[25];put_u64(payload,room->room.room_id);copy_npid(payload+8,npid);
    struct sockaddr_in target;memset(&target,0,sizeof(target));target.sin_family=AF_INET;
    target.sin_addr.s_addr=room->peer_ipv4_be;target.sin_port=htons(room->peer_discovery_port);
    return send_packet((LanMatchImpl*)match->impl,&target,LAN_PACKET_LEAVE,payload,sizeof(payload));
}

int lan_match_poll(LanMatch *match,LanMatchEvent *event) {
    if(!match||!match->impl||!event)return -1;LanMatchImpl *impl=(LanMatchImpl*)match->impl;
    uint8_t packet[LAN_MAX_PACKET];struct sockaddr_in source;socklen_t source_size=(socklen_t)sizeof(source);
    int received=recvfrom(impl->socket,(char*)packet,sizeof(packet),0,(struct sockaddr*)&source,&source_size);
    if(received<0)return socket_would_block()?0:-1;
    if(received<(int)LAN_HEADER_SIZE||get_u32(packet)!=LAN_MAGIC||packet[4]!=LAN_VERSION)return 0;
    uint16_t payload_size=get_u16(packet+6);if((size_t)received!=LAN_HEADER_SIZE+payload_size)return 0;
    const uint8_t *payload=packet+LAN_HEADER_SIZE;memset(event,0,sizeof(*event));
    event->peer_ipv4_be=source.sin_addr.s_addr;event->peer_discovery_port=ntohs(source.sin_port);
    switch(packet[5]) {
    case LAN_PACKET_QUERY:
        if(payload_size!=4||!impl->hosting)return 0;
        if(get_u32(payload)!=0&&get_u32(payload)!=impl->hosted_room.world_id)return 0;
        {
            uint8_t response[64];uint16_t size=write_room(response,&impl->hosted_room);
            return send_packet(impl,&source,LAN_PACKET_ANNOUNCE,response,size)==0?0:-1;
        }
    case LAN_PACKET_ANNOUNCE:
        if(read_room(payload,payload_size,&event->room)!=0)return 0;
        event->type=LAN_MATCH_EVENT_ROOM_FOUND;event->peer_p2p_port=event->room.p2p_port;
        memcpy(event->peer_npid,event->room.host_npid,sizeof(event->peer_npid));return 1;
    case LAN_PACKET_JOIN:
        if(payload_size!=27||!impl->hosting||get_u64(payload)!=impl->hosted_room.room_id)return 0;
        if(read_npid(payload+10,payload_size-10,event->peer_npid)!=0)return 0;
        event->type=LAN_MATCH_EVENT_PEER_JOINED;event->room=impl->hosted_room;
        event->peer_p2p_port=get_u16(payload+8);event->member_id=impl->next_member_id++;
        if(impl->hosted_room.members<impl->hosted_room.max_members)impl->hosted_room.members++;
        {
            uint8_t response[30];put_u64(response,impl->hosted_room.room_id);response[8]=1;
            put_u16(response+9,event->member_id);put_u16(response+11,impl->hosted_room.p2p_port);
            copy_npid(response+13,impl->hosted_room.host_npid);
            if(send_packet(impl,&source,LAN_PACKET_JOIN_ACK,response,sizeof(response))!=0)return -1;
        }
        return 1;
    case LAN_PACKET_JOIN_ACK:
        if(payload_size!=30||payload[8]!=1||read_npid(payload+13,payload_size-13,event->peer_npid)!=0)return 0;
        event->type=LAN_MATCH_EVENT_JOIN_ACCEPTED;event->room.room_id=get_u64(payload);
        event->member_id=get_u16(payload+9);event->peer_p2p_port=get_u16(payload+11);return 1;
    case LAN_PACKET_LEAVE:
        if(payload_size!=25||!impl->hosting||get_u64(payload)!=impl->hosted_room.room_id)return 0;
        if(read_npid(payload+8,payload_size-8,event->peer_npid)!=0)return 0;
        event->type=LAN_MATCH_EVENT_PEER_LEFT;event->room=impl->hosted_room;
        if(impl->hosted_room.members>1)impl->hosted_room.members--;return 1;
    default:return 0;
    }
}
