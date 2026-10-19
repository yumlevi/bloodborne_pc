/* Native LAN identity and compact Matching2 implementation. The wire
 * transport is isolated in lan_match.c; this file translates it to the PS4
 * ABI subset used by Bloodborne. */
#include "runtime.h"
#include "lan_match.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NP_INVALID_ARGUMENT ((int32_t)0x80550003)
#define MATCHING_INVALID_ARGUMENT ((int32_t)0x80550c03)
#define MATCHING_NOT_INITIALIZED ((int32_t)0x80550c01)
#define MATCHING_INVALID_CONTEXT ((int32_t)0x80550c0a)
#define MATCHING_NETWORK_ERROR ((int32_t)0x80550c13)
#define MATCHING_CONTEXT_STARTED 0x6f02
#define MATCHING_CONTEXT_STOPPED 0x6f03
#define MATCHING_CAUSE_CONTEXT_ACTION 11
#define MATCHING_CREATE_JOIN 0x0101
#define MATCHING_JOIN_ROOM 0x0102
#define MATCHING_LEAVE_ROOM 0x0103
#define MATCHING_SEARCH_ROOM 0x0106
#define MATCHING_GET_LOBBIES 0x0006
#define MATCHING_JOIN_LOBBY 0x0201
#define MATCHING_LEAVE_LOBBY 0x0202
#define MATCHING_PING_INFO 0x0e01
#define MATCHING_MEMBER_JOINED 0x1101
#define MATCHING_MEMBER_LEFT 0x1102
#define MATCHING_SIGNAL_ESTABLISHED 0x5102

typedef struct { char data[16]; int8_t term; int8_t dummy[3]; } NpOnlineId;
typedef struct { NpOnlineId handle; uint8_t opt[8],reserved[8]; } NpId;
typedef void (ABI *NpStateCallback)(int32_t,uint32_t,NpId *,void *);
typedef void (ABI *NpSignalingCallback)(uint32_t,uint32_t,int32_t,int32_t,void *);
typedef void (ABI *MatchingContextCallback)(uint16_t,uint16_t,uint8_t,int32_t,void *);
typedef void (ABI *MatchingRequestCallback)(uint16_t,uint32_t,uint16_t,int32_t,const void *,void *);
typedef void (ABI *MatchingRoomCallback)(uint16_t,uint64_t,uint16_t,const void *,void *);
typedef void (ABI *MatchingSignalCallback)(uint16_t,uint64_t,uint16_t,uint16_t,int32_t,void *);

typedef struct { uint64_t pool_size,unknown; int32_t priority; uint8_t padding[4]; uint64_t stack_size,size,ssl_pool_size; } MatchingInit;
typedef struct { MatchingRequestCallback callback; void *arg; uint32_t timeout; uint16_t app_id; uint8_t padding[2]; } MatchingRequestOpt;
typedef struct MatchingWorld { struct MatchingWorld *next; uint32_t world_id,lobbies,max_lobby_members,lobby_members,rooms,room_members; uint8_t pad[3]; } MatchingWorld;
typedef struct { MatchingWorld *world; uint64_t count; } MatchingWorldResponse;
typedef struct { uint16_t id; uint8_t padding[2]; uint32_t num; } MatchingIntAttr;
typedef struct { uint16_t id; uint8_t padding[6]; uint8_t *data; uint64_t size; } MatchingBinAttr;
typedef struct {
    uint16_t max_slot; uint8_t team_id,pad[5]; uint32_t flags,world_id; uint64_t lobby_id;
    void *room_password,*password_mask,*group_config; uint64_t group_configs; void *join_group;
    NpOnlineId *allowed; uint64_t allowed_count; NpOnlineId *blocked; uint64_t blocked_count;
    MatchingBinAttr *internal_bin; uint64_t internal_bin_count; MatchingIntAttr *external_int; uint64_t external_int_count;
    MatchingBinAttr *external_search_bin; uint64_t external_search_bin_count; MatchingBinAttr *external_bin; uint64_t external_bin_count;
    MatchingBinAttr *member_internal_bin; uint64_t member_internal_bin_count; void *signaling;
} MatchingCreateRequest;
typedef struct {
    uint64_t room_id; void *password,*join_group; MatchingBinAttr *member_internal_bin; uint64_t member_internal_bin_count;
    uint8_t presence[16]; uint64_t presence_length; uint8_t team_id,pad[3]; uint32_t flags; NpOnlineId *blocked; uint64_t blocked_count;
} MatchingJoinRequest;
typedef struct { uint64_t room_id; uint8_t presence[16]; uint64_t presence_length; } MatchingLeaveRequest;
typedef struct {
    int32_t option; uint32_t world_id; uint64_t lobby_id; uint32_t range_start,range_max; uint32_t flags1,flags2;
    void *int_filter; uint64_t int_filters; void *bin_filter; uint64_t bin_filters; uint16_t *attrs; uint64_t attr_count;
} MatchingSearchRequest;
typedef struct MatchingMember {
    struct MatchingMember *next; uint64_t join_date; NpId np_id; uint8_t pad[4]; uint16_t member_id;
    uint8_t team_id,nat_type; uint32_t flags; void *room_group,*internal_bin; uint64_t internal_bin_count;
} MatchingMember;
typedef struct { MatchingMember *members; uint64_t count; MatchingMember *me,*owner; } MatchingMemberList;
typedef struct {
    uint16_t public_slots,private_slots,open_public_slots,open_private_slots,max_slot,server_id; uint32_t world_id;
    uint64_t lobby_id,room_id,password_mask,joined_slot_mask; void *room_group; uint64_t room_group_count;
    uint32_t flags; uint8_t pad[4]; void *internal_bin; uint64_t internal_bin_count;
} MatchingRoomInternal;
typedef struct { const MatchingRoomInternal *room; MatchingMemberList members; } MatchingCreateResponse;
typedef struct MatchingRoomExternal {
    struct MatchingRoomExternal *next; uint16_t max_slot,current_members; uint32_t flags; uint16_t server_id; uint8_t pad[2];
    uint32_t world_id; uint64_t lobby_id,room_id,password_mask,joined_slot_mask;
    uint16_t public_slots,private_slots,open_public_slots,open_private_slots; NpId *owner; void *room_group;
    uint64_t room_group_count; MatchingIntAttr *external_int; uint64_t external_int_count;
    MatchingBinAttr *external_search_bin; uint64_t external_search_bin_count; MatchingBinAttr *external_bin; uint64_t external_bin_count;
} MatchingRoomExternal;
typedef struct { uint32_t start,total,results; uint8_t pad[4]; MatchingRoomExternal *room; } MatchingSearchResponse;
typedef struct { uint64_t room_id; } MatchingLeaveResponse;
typedef struct { uint32_t start,total,results,pad; void *lobby; } MatchingLobbyListResponse;
typedef struct { uint16_t server_id,pad; uint32_t world_id; uint64_t room_id; uint32_t rtt; uint8_t reserved[20]; } MatchingPingResponse;

_Static_assert(sizeof(MatchingCreateRequest)==184,"Matching2 create request ABI");
_Static_assert(sizeof(MatchingJoinRequest)==0x58,"Matching2 join request ABI");
_Static_assert(sizeof(MatchingMember)==0x58,"Matching2 member ABI");

static NpStateCallback state_callback; static void *state_callback_arg; static int state_callback_pending;
static int matching_initialized; static uint16_t matching_context; static MatchingContextCallback context_callback;
static void *context_callback_arg; static uint16_t pending_context_event; static MatchingRequestCallback default_request_callback;
static void *default_request_arg; static MatchingRequestCallback pending_request_callback; static void *pending_request_arg;
static uint32_t pending_request_id; static uint16_t pending_request_event; static const void *pending_request_data;
static uint32_t next_request=1; static MatchingRoomCallback room_callback; static void *room_callback_arg;
static MatchingSignalCallback signal_callback; static void *signal_callback_arg; static LanMatch lan_match;
static LanMatchEvent found_room; static uint64_t current_room_id; static uint32_t peer_ipv4_be;
static uint16_t peer_p2p_port,peer_member_id,local_member_id; static char peer_npid[LAN_MATCH_NPID_SIZE+1];
static int pending_network_operation; static uint64_t search_deadline_ms;
static int np_state_logged;
static int legacy_signaling_initialized,legacy_signaling_context,legacy_signaling_connection;
static NpSignalingCallback legacy_signaling_callback; static void *legacy_signaling_callback_arg;
static int legacy_signaling_event_pending; static int32_t next_lookup_context=1,next_lookup_request=1;

static MatchingWorld world={NULL,1,0,0,0,0,0,{0}}; static MatchingWorldResponse world_response={&world,1};
static MatchingRoomInternal internal_room; static MatchingMember local_member,remote_member;
static MatchingCreateResponse create_response; static MatchingRoomExternal external_room;
static MatchingSearchResponse search_response; static MatchingLeaveResponse leave_response; static NpId external_owner;
static MatchingLobbyListResponse lobby_list_response; static MatchingPingResponse ping_response;
static MatchingIntAttr external_int[LAN_MATCH_MAX_INT_ATTRS]; static MatchingBinAttr external_search_bin;
static uint8_t external_search_bin_data[LAN_MATCH_MAX_BIN_SIZE];

static int lan_enabled(void) { const char *mode=getenv("BB_NETWORK_MODE"); return mode&&!strcmp(mode,"lan"); }
static int trace_enabled(void) { const char *value=getenv("BB_NETWORK_TRACE");return value&&*value&&strcmp(value,"0"); }
static void trace_np(const char *message) { if(trace_enabled())fprintf(stderr,"Native NP: %s\n",message); }
static uint16_t configured_port(const char *name,uint16_t fallback) { const char *text=getenv(name);if(!text||!*text)return fallback;long value=strtol(text,NULL,10);return value>0&&value<65536?(uint16_t)value:fallback; }
static uint64_t now_ms(void) { struct timespec value;if(timespec_get(&value,TIME_UTC)!=TIME_UTC)return 0;return (uint64_t)value.tv_sec*1000u+(uint64_t)value.tv_nsec/1000000u; }
static void fill_online_id(NpOnlineId *out) { const char *name=getenv("BB_LAN_NPID");if(!name||!*name)name="Hunter01";memset(out,0,sizeof(*out));size_t length=strlen(name);if(length>sizeof(out->data))length=sizeof(out->data);memcpy(out->data,name,length); }
static void fill_np_id(NpId *out,const char *name) { memset(out,0,sizeof(*out));if(!name){fill_online_id(&out->handle);return;}size_t length=strlen(name);if(length>sizeof(out->handle.data))length=sizeof(out->handle.data);memcpy(out->handle.data,name,length); }
static void local_name(char out[LAN_MATCH_NPID_SIZE+1]) { NpOnlineId id;fill_online_id(&id);memset(out,0,LAN_MATCH_NPID_SIZE+1);memcpy(out,id.data,LAN_MATCH_NPID_SIZE); }
static void queue_request(MatchingRequestCallback callback,void *arg,uint32_t id,uint16_t event,const void *data) { pending_request_callback=callback;pending_request_arg=arg;pending_request_id=id;pending_request_event=event;pending_request_data=data; }
static MatchingRequestCallback choose_callback(const MatchingRequestOpt *opt,void **arg) { if(opt&&opt->callback){*arg=opt->arg;return opt->callback;}*arg=default_request_arg;return default_request_callback; }
static uint64_t make_room_id(void) { char name[LAN_MATCH_NPID_SIZE+1];local_name(name);uint64_t hash=1469598103934665603ull;for(size_t i=0;name[i];i++){hash^=(uint8_t)name[i];hash*=1099511628211ull;}return ((uint64_t)time(NULL)<<32)^hash^((uint64_t)next_request<<16); }
static void fill_member(MatchingMember *member,const char *name,uint16_t id,uint32_t flags) { memset(member,0,sizeof(*member));member->join_date=(uint64_t)time(NULL)*1000000u;fill_np_id(&member->np_id,name);member->member_id=id;member->nat_type=1;member->flags=flags; }
static void fill_internal_room(const LanMatchRoom *room) { memset(&internal_room,0,sizeof(internal_room));internal_room.public_slots=room->max_members;internal_room.open_public_slots=room->max_members>room->members?room->max_members-room->members:0;internal_room.max_slot=room->max_members;internal_room.server_id=1;internal_room.world_id=room->world_id;internal_room.room_id=room->room_id;internal_room.flags=room->flags; }
static void fill_create_response(const LanMatchRoom *room,int local_is_owner) {
    char name[LAN_MATCH_NPID_SIZE+1];local_name(name);fill_internal_room(room);fill_member(&local_member,name,local_member_id,local_is_owner?0x80000000u:0);
    memset(&create_response,0,sizeof(create_response));create_response.room=&internal_room;
    if(local_is_owner){local_member.next=peer_member_id?&remote_member:NULL;create_response.members.members=&local_member;create_response.members.owner=&local_member;create_response.members.me=&local_member;create_response.members.count=peer_member_id?2:1;}
    else{fill_member(&remote_member,peer_npid,peer_member_id,0x80000000u);remote_member.next=&local_member;create_response.members.members=&remote_member;create_response.members.owner=&remote_member;create_response.members.me=&local_member;create_response.members.count=2;}
}
static void fill_search_response(const LanMatchEvent *event) {
    const LanMatchRoom *room=&event->room;memset(&external_room,0,sizeof(external_room));fill_np_id(&external_owner,room->host_npid);
    external_room.max_slot=room->max_members;external_room.current_members=room->members;external_room.flags=room->flags;external_room.server_id=1;
    external_room.world_id=room->world_id;external_room.room_id=room->room_id;external_room.public_slots=room->max_members;
    external_room.open_public_slots=room->max_members>room->members?room->max_members-room->members:0;external_room.owner=&external_owner;
    external_room.external_int_count=room->int_attr_count;for(uint8_t i=0;i<room->int_attr_count;i++){external_int[i].id=room->int_attrs[i].id;external_int[i].num=room->int_attrs[i].value;}
    external_room.external_int=room->int_attr_count?external_int:NULL;memset(&external_search_bin,0,sizeof(external_search_bin));
    if(room->search_bin.size){external_search_bin.id=room->search_bin.id;external_search_bin.size=room->search_bin.size;memcpy(external_search_bin_data,room->search_bin.data,room->search_bin.size);external_search_bin.data=external_search_bin_data;external_room.external_search_bin=&external_search_bin;external_room.external_search_bin_count=1;}
    memset(&search_response,0,sizeof(search_response));search_response.total=1;search_response.results=1;search_response.room=&external_room;
}
static void finish_empty_search(void) { MatchingRequestCallback callback=pending_request_callback;void *arg=pending_request_arg;uint32_t id=pending_request_id;memset(&search_response,0,sizeof(search_response));pending_network_operation=0;queue_request(callback,arg,id,MATCHING_SEARCH_ROOM,&search_response); }
static void pump_lan_events(void) {
    if(!matching_initialized||!lan_match.impl)return;
    for(int i=0;i<16;i++){LanMatchEvent event;int result=lan_match_poll(&lan_match,&event);if(result<=0)break;
        if(event.type==LAN_MATCH_EVENT_ROOM_FOUND&&pending_network_operation==MATCHING_SEARCH_ROOM){MatchingRequestCallback callback=pending_request_callback;void *arg=pending_request_arg;uint32_t id=pending_request_id;found_room=event;fill_search_response(&event);pending_network_operation=0;queue_request(callback,arg,id,MATCHING_SEARCH_ROOM,&search_response);}
        else if(event.type==LAN_MATCH_EVENT_PEER_JOINED){current_room_id=event.room.room_id;peer_ipv4_be=event.peer_ipv4_be;peer_p2p_port=event.peer_p2p_port;peer_member_id=event.member_id;memcpy(peer_npid,event.peer_npid,sizeof(peer_npid));fill_member(&remote_member,peer_npid,peer_member_id,0);fill_create_response(&event.room,1);if(legacy_signaling_connection)legacy_signaling_event_pending=1;if(room_callback)room_callback(matching_context,current_room_id,MATCHING_MEMBER_JOINED,&remote_member,room_callback_arg);if(signal_callback)signal_callback(matching_context,current_room_id,peer_member_id,MATCHING_SIGNAL_ESTABLISHED,0,signal_callback_arg);}
        else if(event.type==LAN_MATCH_EVENT_JOIN_ACCEPTED&&pending_network_operation==MATCHING_JOIN_ROOM){MatchingRequestCallback callback=pending_request_callback;void *arg=pending_request_arg;uint32_t id=pending_request_id;current_room_id=event.room.room_id;peer_ipv4_be=event.peer_ipv4_be;peer_p2p_port=event.peer_p2p_port;peer_member_id=1;local_member_id=event.member_id;memcpy(peer_npid,event.peer_npid,sizeof(peer_npid));LanMatchRoom joined=found_room.room;joined.members++;fill_create_response(&joined,0);pending_network_operation=0;queue_request(callback,arg,id,MATCHING_JOIN_ROOM,&create_response);if(legacy_signaling_connection)legacy_signaling_event_pending=1;if(signal_callback)signal_callback(matching_context,current_room_id,peer_member_id,MATCHING_SIGNAL_ESTABLISHED,0,signal_callback_arg);}
        else if(event.type==LAN_MATCH_EVENT_PEER_LEFT){if(room_callback)room_callback(matching_context,event.room.room_id,MATCHING_MEMBER_LEFT,&remote_member,room_callback_arg);peer_member_id=0;peer_ipv4_be=0;peer_p2p_port=0;peer_npid[0]=0;}
    }
    if(pending_network_operation==MATCHING_SEARCH_ROOM&&now_ms()>=search_deadline_ms)finish_empty_search();
}

static ABI int32_t np_get_state(int32_t user,uint32_t *state){(void)user;if(!state)return NP_INVALID_ARGUMENT;*state=2;if(!np_state_logged){trace_np("reported SignedIn");np_state_logged=1;}return 0;}
static ABI int32_t np_get_online_id(int32_t user,NpOnlineId *id){(void)user;if(!id)return NP_INVALID_ARGUMENT;fill_online_id(id);return 0;}
static ABI int32_t np_get_np_id(int32_t user,NpId *id){(void)user;if(!id)return NP_INVALID_ARGUMENT;fill_np_id(id,NULL);return 0;}
static ABI int32_t np_presence(const NpOnlineId *id,int32_t *status){if(!id||!status)return NP_INVALID_ARGUMENT;*status=1;return 0;}
static ABI int32_t np_register_state(NpStateCallback callback,void *arg){if(!callback)return NP_INVALID_ARGUMENT;state_callback=callback;state_callback_arg=arg;state_callback_pending=1;return 0;}
static ABI int32_t np_unregister_state(void){state_callback=NULL;state_callback_arg=NULL;state_callback_pending=0;return 0;}
static ABI int32_t np_check_callback(void){pump_lan_events();if(state_callback&&state_callback_pending){NpId id;fill_np_id(&id,NULL);state_callback_pending=0;state_callback(1,2,&id,state_callback_arg);}if(legacy_signaling_callback&&legacy_signaling_event_pending){legacy_signaling_event_pending=0;legacy_signaling_callback((uint32_t)legacy_signaling_context,(uint32_t)legacy_signaling_connection,1,0,legacy_signaling_callback_arg);}if(context_callback&&pending_context_event){uint16_t event=pending_context_event;pending_context_event=0;context_callback(matching_context,event,MATCHING_CAUSE_CONTEXT_ACTION,0,context_callback_arg);}if(pending_request_event){MatchingRequestCallback callback=pending_request_callback;void *arg=pending_request_arg;uint32_t id=pending_request_id;uint16_t event=pending_request_event;const void *data=pending_request_data;pending_request_event=0;pending_request_callback=NULL;if(callback)callback(matching_context,id,event,0,data,arg);}return 0;}
static ABI int32_t np_create_async(const void *param){return param?(int32_t)(0x20000000u+next_request++):NP_INVALID_ARGUMENT;}
static ABI int32_t np_delete_request(int32_t id){return id>0?0:NP_INVALID_ARGUMENT;}
static ABI int32_t np_poll_async(int32_t id,int32_t *result){if(id<=0||!result)return NP_INVALID_ARGUMENT;*result=0;return 0;}
static ABI int32_t np_available(int32_t id,const NpOnlineId *online){return id>0&&online?0:NP_INVALID_ARGUMENT;}
static ABI int32_t np_check_plus(int32_t id,const void *param,uint8_t *result){if(id<=0||!param||!result)return NP_INVALID_ARGUMENT;*result=1;return 0;}
static ABI int32_t np_parental(int32_t id,const NpOnlineId *online,int8_t *age,void *info){if(id<=0||!online||!age||!info)return NP_INVALID_ARGUMENT;*age=30;memset(info,0,32);return 0;}
static ABI int32_t np_success(void){return 0;}
static ABI int32_t np_register_success(void *callback,void *arg){(void)arg;return callback?0:NP_INVALID_ARGUMENT;}

/* Bloodborne uses the legacy NP lookup API before it creates its Matching2
 * context.  A signed-out result here disables all online inventory items even
 * when sceNpGetState already reported SignedIn. */
static ABI int32_t lookup_create_title(const void *owner){if(!owner)return NP_INVALID_ARGUMENT;int32_t id=0x30000000+next_lookup_context++;trace_np("lookup title context created");return id;}
static ABI int32_t lookup_delete_title(int32_t id){return id>0?0:NP_INVALID_ARGUMENT;}
static ABI int32_t lookup_create_request(int32_t context,const void *param){(void)param;if(context<=0)return NP_INVALID_ARGUMENT;int32_t id=0x31000000+next_lookup_request++;trace_np("lookup async request created");return id;}
static ABI int32_t lookup_delete_request(int32_t id){return id>0?0:NP_INVALID_ARGUMENT;}
static ABI int32_t lookup_poll(int32_t id,int32_t *result){if(id<=0||!result)return NP_INVALID_ARGUMENT;*result=0;return 0;}
static ABI int32_t lookup_npid(int32_t request,const NpOnlineId *online,NpId *out,const void *option){(void)option;if(request<=0||!online||!out)return NP_INVALID_ARGUMENT;memset(out,0,sizeof(*out));out->handle=*online;trace_np("lookup resolved an online ID");return 0;}

/* Legacy signaling is separate from Matching2 signaling.  The game creates
 * both, and treating this older layer as signed out prevents online startup. */
static ABI int32_t signaling_initialize(int64_t memory_size,int32_t priority,int32_t affinity,int64_t stack_size){(void)memory_size;(void)priority;(void)affinity;(void)stack_size;legacy_signaling_initialized=1;trace_np("legacy signaling initialized");return 0;}
static ABI int32_t signaling_terminate(void){legacy_signaling_initialized=0;legacy_signaling_context=0;legacy_signaling_connection=0;legacy_signaling_callback=NULL;legacy_signaling_event_pending=0;trace_np("legacy signaling terminated");return 0;}
static ABI int32_t signaling_create(const void *npid,NpSignalingCallback callback,void *arg,int32_t *out){if(!legacy_signaling_initialized||!npid||!out)return NP_INVALID_ARGUMENT;legacy_signaling_context=1;legacy_signaling_callback=callback;legacy_signaling_callback_arg=arg;*out=legacy_signaling_context;trace_np("legacy signaling context created");return 0;}
static ABI int32_t signaling_delete(int32_t context){if(context!=legacy_signaling_context||!context)return NP_INVALID_ARGUMENT;legacy_signaling_context=0;legacy_signaling_connection=0;legacy_signaling_callback=NULL;legacy_signaling_event_pending=0;return 0;}
static ABI int32_t signaling_activate(int32_t context,const NpId *peer,int32_t *out){if(context!=legacy_signaling_context||!context||!peer||!out)return NP_INVALID_ARGUMENT;legacy_signaling_connection=1;*out=1;if(peer_ipv4_be)legacy_signaling_event_pending=1;trace_np("legacy signaling connection activated");return 0;}
static ABI int32_t signaling_deactivate(int32_t context,int32_t connection){if(context!=legacy_signaling_context||connection!=legacy_signaling_connection||!connection)return NP_INVALID_ARGUMENT;legacy_signaling_connection=0;legacy_signaling_event_pending=0;return 0;}
static ABI int32_t signaling_status(int32_t context,int32_t connection,uint32_t *status,uint32_t *address,uint16_t *port){if(context!=legacy_signaling_context||connection!=legacy_signaling_connection||!status)return NP_INVALID_ARGUMENT;if(peer_ipv4_be){*status=2;if(address)*address=peer_ipv4_be;if(port)*port=(uint16_t)((peer_p2p_port<<8)|(peer_p2p_port>>8));}else{*status=1;if(address)*address=0;if(port)*port=0;}return 0;}

static ABI int32_t matching_initialize(const MatchingInit *param){if(!param||(param->size!=0x28&&param->size!=0x30))return MATCHING_INVALID_ARGUMENT;const char *target=getenv("BB_LAN_DISCOVERY_TARGET");if(!target||!*target)target="255.255.255.255";uint16_t port=configured_port("BB_LAN_DISCOVERY_PORT",31314);uint16_t target_port=configured_port("BB_LAN_DISCOVERY_TARGET_PORT",port);if(lan_match_open(&lan_match,port,target,target_port)!=0){fprintf(stderr,"Native LAN: cannot bind discovery UDP port %u\n",port);return MATCHING_NETWORK_ERROR;}matching_initialized=1;trace_np("Matching2 initialized and LAN discovery opened");return 0;}
static ABI int32_t matching_terminate(void){lan_match_close(&lan_match);matching_initialized=0;matching_context=0;context_callback=NULL;default_request_callback=NULL;pending_context_event=0;pending_request_callback=NULL;pending_request_event=0;room_callback=NULL;signal_callback=NULL;pending_network_operation=0;current_room_id=0;return 0;}
static ABI int32_t matching_create_context(const void *param,uint16_t *out){if(!matching_initialized)return MATCHING_NOT_INITIALIZED;if(!param||!out)return MATCHING_INVALID_ARGUMENT;matching_context=1;*out=1;return 0;}
static ABI int32_t matching_destroy_context(uint16_t context){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;matching_context=0;return 0;}
static ABI int32_t matching_register_context(MatchingContextCallback callback,void *arg){if(!matching_initialized)return MATCHING_NOT_INITIALIZED;context_callback=callback;context_callback_arg=arg;return 0;}
static ABI int32_t matching_register_room(uint16_t context,MatchingRoomCallback callback,void *arg){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;room_callback=callback;room_callback_arg=arg;return 0;}
static ABI int32_t matching_register_signal(uint16_t context,MatchingSignalCallback callback,void *arg){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;signal_callback=callback;signal_callback_arg=arg;return 0;}
static ABI int32_t matching_context_start(uint16_t context,uint64_t timeout){(void)timeout;if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;pending_context_event=MATCHING_CONTEXT_STARTED;return 0;}
static ABI int32_t matching_context_stop(uint16_t context){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;pending_context_event=MATCHING_CONTEXT_STOPPED;return 0;}
static ABI int32_t matching_default_request(uint16_t context,const MatchingRequestOpt *opt){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!opt)return MATCHING_INVALID_ARGUMENT;default_request_callback=opt->callback;default_request_arg=opt->arg;return 0;}
static ABI int32_t matching_server_id(uint16_t context,uint16_t *server){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!server)return MATCHING_INVALID_ARGUMENT;*server=1;return 0;}
static ABI int32_t matching_worlds(uint16_t context,const void *request,const MatchingRequestOpt *opt,uint32_t *request_id){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!request||!request_id)return MATCHING_INVALID_ARGUMENT;void *arg;MatchingRequestCallback callback=choose_callback(opt,&arg);*request_id=next_request++;queue_request(callback,arg,*request_id,0x0002,&world_response);return 0;}
static ABI int32_t matching_lobbies(uint16_t context,const void *request,const MatchingRequestOpt *opt,uint32_t *request_id){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!request||!request_id)return MATCHING_INVALID_ARGUMENT;memset(&lobby_list_response,0,sizeof(lobby_list_response));void *arg;MatchingRequestCallback callback=choose_callback(opt,&arg);*request_id=next_request++;queue_request(callback,arg,*request_id,MATCHING_GET_LOBBIES,&lobby_list_response);trace_np("Matching2 lobby list completed");return 0;}
static ABI int32_t matching_join_lobby(uint16_t context,const void *request,const MatchingRequestOpt *opt,uint32_t *request_id){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!request||!request_id)return MATCHING_INVALID_ARGUMENT;void *arg;MatchingRequestCallback callback=choose_callback(opt,&arg);*request_id=next_request++;queue_request(callback,arg,*request_id,MATCHING_JOIN_LOBBY,NULL);return 0;}
static ABI int32_t matching_leave_lobby(uint16_t context,const void *request,const MatchingRequestOpt *opt,uint32_t *request_id){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!request||!request_id)return MATCHING_INVALID_ARGUMENT;void *arg;MatchingRequestCallback callback=choose_callback(opt,&arg);*request_id=next_request++;queue_request(callback,arg,*request_id,MATCHING_LEAVE_LOBBY,NULL);return 0;}
static ABI int32_t matching_create_join(uint16_t context,const MatchingCreateRequest *request,const MatchingRequestOpt *opt,uint32_t *request_id){
    if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!request||!request_id||!request->max_slot)return MATCHING_INVALID_ARGUMENT;
    LanMatchRoom room;memset(&room,0,sizeof(room));room.room_id=make_room_id();room.world_id=request->world_id?request->world_id:1;room.flags=request->flags;room.max_members=request->max_slot;room.members=1;room.p2p_port=configured_port("BB_P2P_PORT",31316);local_name(room.host_npid);
    uint64_t count=request->external_int_count;if(count>LAN_MATCH_MAX_INT_ATTRS)count=LAN_MATCH_MAX_INT_ATTRS;room.int_attr_count=(uint8_t)count;for(uint8_t i=0;i<room.int_attr_count;i++){room.int_attrs[i].id=request->external_int[i].id;room.int_attrs[i].value=request->external_int[i].num;}
    if(request->external_search_bin_count&&request->external_search_bin){MatchingBinAttr *bin=&request->external_search_bin[0];room.search_bin.id=bin->id;uint64_t size=bin->size;if(size>LAN_MATCH_MAX_BIN_SIZE)size=LAN_MATCH_MAX_BIN_SIZE;room.search_bin.size=(uint16_t)size;if(size&&bin->data)memcpy(room.search_bin.data,bin->data,(size_t)size);}
    if(lan_match_host(&lan_match,&room)!=0)return MATCHING_NETWORK_ERROR;current_room_id=room.room_id;local_member_id=1;peer_member_id=0;fill_create_response(&room,1);void *arg;MatchingRequestCallback callback=choose_callback(opt,&arg);*request_id=next_request++;queue_request(callback,arg,*request_id,MATCHING_CREATE_JOIN,&create_response);return 0;
}
static ABI int32_t matching_search(uint16_t context,const MatchingSearchRequest *request,const MatchingRequestOpt *opt,uint32_t *request_id){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!request||!request_id)return MATCHING_INVALID_ARGUMENT;if(pending_network_operation)return MATCHING_INVALID_ARGUMENT;void *arg;MatchingRequestCallback callback=choose_callback(opt,&arg);*request_id=next_request++;pending_request_callback=callback;pending_request_arg=arg;pending_request_id=*request_id;pending_request_event=0;pending_network_operation=MATCHING_SEARCH_ROOM;search_deadline_ms=now_ms()+750;if(lan_match_search(&lan_match,request->world_id)!=0){pending_network_operation=0;return MATCHING_NETWORK_ERROR;}return 0;}
static ABI int32_t matching_join(uint16_t context,const MatchingJoinRequest *request,const MatchingRequestOpt *opt,uint32_t *request_id){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!request||!request_id||found_room.type!=LAN_MATCH_EVENT_ROOM_FOUND||request->room_id!=found_room.room.room_id)return MATCHING_INVALID_ARGUMENT;if(pending_network_operation)return MATCHING_INVALID_ARGUMENT;char name[LAN_MATCH_NPID_SIZE+1];local_name(name);if(lan_match_join(&lan_match,&found_room,name,configured_port("BB_P2P_PORT",31316))!=0)return MATCHING_NETWORK_ERROR;void *arg;MatchingRequestCallback callback=choose_callback(opt,&arg);*request_id=next_request++;pending_request_callback=callback;pending_request_arg=arg;pending_request_id=*request_id;pending_request_event=0;pending_network_operation=MATCHING_JOIN_ROOM;return 0;}
static ABI int32_t matching_leave(uint16_t context,const MatchingLeaveRequest *request,const MatchingRequestOpt *opt,uint32_t *request_id){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!request||!request_id||request->room_id!=current_room_id)return MATCHING_INVALID_ARGUMENT;char name[LAN_MATCH_NPID_SIZE+1];local_name(name);if(found_room.type==LAN_MATCH_EVENT_ROOM_FOUND)lan_match_leave(&lan_match,&found_room,name);leave_response.room_id=current_room_id;current_room_id=0;void *arg;MatchingRequestCallback callback=choose_callback(opt,&arg);*request_id=next_request++;queue_request(callback,arg,*request_id,MATCHING_LEAVE_ROOM,&leave_response);return 0;}
static ABI int32_t matching_signal_status(uint16_t context,uint64_t room,uint16_t member,int32_t *status,void *address,uint16_t *port){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!status)return MATCHING_INVALID_ARGUMENT;if(room==current_room_id&&member==peer_member_id&&peer_ipv4_be){*status=2;if(address)*(uint32_t*)address=peer_ipv4_be;if(port)*port=(uint16_t)((peer_p2p_port<<8)|(peer_p2p_port>>8));}else *status=0;return 0;}
static ABI int32_t matching_ping(uint16_t context,const uint64_t *request,const MatchingRequestOpt *opt,uint32_t *request_id){if(context!=matching_context||!context)return MATCHING_INVALID_CONTEXT;if(!request||!request_id)return MATCHING_INVALID_ARGUMENT;memset(&ping_response,0,sizeof(ping_response));ping_response.server_id=1;ping_response.world_id=1;ping_response.room_id=*request;ping_response.rtt=1;void *arg;MatchingRequestCallback callback=choose_callback(opt,&arg);*request_id=next_request++;queue_request(callback,arg,*request_id,MATCHING_PING_INFO,&ping_response);return 0;}

static const RuntimeExport exports[]={
    {"sceNpGetState",np_get_state},{"sceNpGetOnlineId",np_get_online_id},{"sceNpGetNpId",np_get_np_id},{"sceNpGetGamePresenceStatus",np_presence},{"sceNpRegisterStateCallback",np_register_state},{"sceNpUnregisterStateCallback",np_unregister_state},{"sceNpRegisterPlusEventCallback",np_register_success},{"sceNpUnregisterPlusEventCallback",np_success},{"sceNpSetNpTitleId",np_success},{"sceNpNotifyPlusFeature",np_success},{"sceNpSetContentRestriction",np_success},{"sceNpCheckCallback",np_check_callback},{"sceNpCreateAsyncRequest",np_create_async},{"sceNpDeleteRequest",np_delete_request},{"sceNpAbortRequest",np_delete_request},{"sceNpPollAsync",np_poll_async},{"sceNpCheckNpAvailability",np_available},{"sceNpCheckPlus",np_check_plus},{"sceNpGetParentalControlInfo",np_parental},
    {"sceNpLookupCreateTitleCtx",lookup_create_title},{"sceNpLookupDeleteTitleCtx",lookup_delete_title},{"sceNpLookupCreateAsyncRequest",lookup_create_request},{"sceNpLookupDeleteRequest",lookup_delete_request},{"sceNpLookupAbortRequest",lookup_delete_request},{"sceNpLookupPollAsync",lookup_poll},{"sceNpLookupNpId",lookup_npid},
    {"sceNpSignalingInitialize",signaling_initialize},{"sceNpSignalingTerminate",signaling_terminate},{"sceNpSignalingCreateContext",signaling_create},{"sceNpSignalingDeleteContext",signaling_delete},{"sceNpSignalingActivateConnection",signaling_activate},{"sceNpSignalingDeactivateConnection",signaling_deactivate},{"sceNpSignalingGetConnectionStatus",signaling_status},
    {"sceNpMatching2Initialize",matching_initialize},{"sceNpMatching2Terminate",matching_terminate},{"sceNpMatching2CreateContext",matching_create_context},{"sceNpMatching2DestroyContext",matching_destroy_context},{"sceNpMatching2RegisterContextCallback",matching_register_context},{"sceNpMatching2RegisterRoomEventCallback",matching_register_room},{"sceNpMatching2RegisterSignalingCallback",matching_register_signal},{"sceNpMatching2ContextStart",matching_context_start},{"sceNpMatching2ContextStop",matching_context_stop},{"sceNpMatching2SetDefaultRequestOptParam",matching_default_request},{"sceNpMatching2GetServerId",matching_server_id},{"sceNpMatching2GetWorldInfoList",matching_worlds},{"sceNpMatching2GetLobbyInfoList",matching_lobbies},{"sceNpMatching2JoinLobby",matching_join_lobby},{"sceNpMatching2LeaveLobby",matching_leave_lobby},{"sceNpMatching2CreateJoinRoom",matching_create_join},{"sceNpMatching2SearchRoom",matching_search},{"sceNpMatching2JoinRoom",matching_join},{"sceNpMatching2LeaveRoom",matching_leave},{"sceNpMatching2SignalingGetConnectionStatus",matching_signal_status},{"sceNpMatching2SignalingGetPingInfo",matching_ping}
};
uintptr_t runtime_np_lan_resolve(const char *name){return lan_enabled()?RUNTIME_LOOKUP(exports,name):0;}
