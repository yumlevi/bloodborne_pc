#include "runtime.h"
#include "lan_match.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

const char *runtime_symbol(const char *name){return name;}
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name){for(size_t i=0;i<count;i++)if(!strcmp(table[i].name,name))return(uintptr_t)table[i].function;return 0;}

typedef void(ABI *RequestCallback)(uint16_t,uint32_t,uint16_t,int32_t,const void*,void*);
typedef void(ABI *SignalCallback)(uint16_t,uint64_t,uint16_t,uint16_t,int32_t,void*);
typedef struct{RequestCallback callback;void*arg;uint32_t timeout;uint16_t app_id;uint8_t padding[2];}RequestOpt;
typedef int32_t(ABI *Initialize)(const void*);typedef int32_t(ABI *Terminate)(void);
typedef int32_t(ABI *CreateContext)(const void*,uint16_t*);typedef int32_t(ABI *Check)(void);
typedef int32_t(ABI *RegisterSignal)(uint16_t,SignalCallback,void*);
typedef int32_t(ABI *SearchRoom)(uint16_t,const void*,const RequestOpt*,uint32_t*);
typedef int32_t(ABI *JoinRoom)(uint16_t,const void*,const RequestOpt*,uint32_t*);
typedef int32_t(ABI *LeaveRoom)(uint16_t,const void*,const RequestOpt*,uint32_t*);
typedef int32_t(ABI *SignalStatus)(uint16_t,uint64_t,uint16_t,int32_t*,void*,uint16_t*);
typedef struct{int32_t option;uint32_t world_id;uint64_t lobby_id;uint32_t start,max,flags1,flags2;void*int_filter;uint64_t int_filters;void*bin_filter;uint64_t bin_filters;uint16_t*attrs;uint64_t attr_count;}SearchRequest;
typedef struct{uint32_t start,total,results;uint8_t pad[4];void*room;}SearchResponse;
typedef struct{void*next;uint16_t max_slot,current_members;uint32_t flags;uint16_t server;uint8_t pad[2];uint32_t world;uint64_t lobby,room_id;}RoomExternalHead;
typedef struct{uint64_t room_id;void*password,*join_group,*member_bin;uint64_t member_bin_count;uint8_t presence[16];uint64_t presence_length;uint8_t team,pad[3];uint32_t flags;void*blocked;uint64_t blocked_count;}JoinRequest;
typedef struct{uint64_t room_id;uint8_t presence[16];uint64_t presence_length;}LeaveRequest;

static int search_called,join_called,leave_called,signal_called;static uint64_t found_room;
static ABI void request_cb(uint16_t context,uint32_t request,uint16_t event,int32_t error,const void*data,void*arg){
    assert(context==1&&request&&error==0&&data);
    if(event==0x106){const SearchResponse*response=(const SearchResponse*)data;assert(arg==(void*)1&&response->total==1&&response->results==1&&response->room);const RoomExternalHead*room=(const RoomExternalHead*)response->room;assert(room->world==1&&room->max_slot==5);found_room=room->room_id;search_called++;}
    else if(event==0x102){const uint64_t*words=(const uint64_t*)data;assert(arg==(void*)2&&words[0]);assert(*(const uint64_t*)((const uint8_t*)words[0]+24)==found_room);join_called++;}
    else if(event==0x103){assert(arg==(void*)3&&*(const uint64_t*)data==found_room);leave_called++;}
    else assert(0);
}
static ABI void signal_cb(uint16_t context,uint64_t room,uint16_t member,uint16_t event,int32_t error,void*arg){assert(context==1&&room==found_room&&member==1&&event==0x5102&&error==0&&arg==(void*)4);signal_called++;}
static void pause_ms(void){
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec delay={0,1000000};nanosleep(&delay,NULL);
#endif
}

int main(void){
#ifdef _WIN32
    assert(_putenv_s("BB_NETWORK_MODE","lan")==0);assert(_putenv_s("BB_LAN_NPID","GuestHunter")==0);
    assert(_putenv_s("BB_LAN_DISCOVERY_TARGET","127.0.0.1")==0);assert(_putenv_s("BB_LAN_DISCOVERY_PORT","42415")==0);
    assert(_putenv_s("BB_LAN_DISCOVERY_TARGET_PORT","42414")==0);assert(_putenv_s("BB_P2P_PORT","42417")==0);
#else
    assert(setenv("BB_NETWORK_MODE","lan",1)==0);assert(setenv("BB_LAN_NPID","GuestHunter",1)==0);
    assert(setenv("BB_LAN_DISCOVERY_TARGET","127.0.0.1",1)==0);assert(setenv("BB_LAN_DISCOVERY_PORT","42415",1)==0);
    assert(setenv("BB_LAN_DISCOVERY_TARGET_PORT","42414",1)==0);assert(setenv("BB_P2P_PORT","42417",1)==0);
#endif
    LanMatch host={0};assert(lan_match_open(&host,42414,"127.0.0.1",42415)==0);LanMatchRoom room={0};
    room.room_id=0x8877665544332211ull;room.world_id=1;room.max_members=5;room.members=1;room.p2p_port=42416;memcpy(room.host_npid,"HostHunter",10);assert(lan_match_host(&host,&room)==0);
    Initialize initialize=(Initialize)runtime_np_lan_resolve("sceNpMatching2Initialize");Terminate terminate=(Terminate)runtime_np_lan_resolve("sceNpMatching2Terminate");
    CreateContext create=(CreateContext)runtime_np_lan_resolve("sceNpMatching2CreateContext");Check check=(Check)runtime_np_lan_resolve("sceNpCheckCallback");
    RegisterSignal register_signal=(RegisterSignal)runtime_np_lan_resolve("sceNpMatching2RegisterSignalingCallback");SearchRoom search=(SearchRoom)runtime_np_lan_resolve("sceNpMatching2SearchRoom");
    JoinRoom join=(JoinRoom)runtime_np_lan_resolve("sceNpMatching2JoinRoom");LeaveRoom leave=(LeaveRoom)runtime_np_lan_resolve("sceNpMatching2LeaveRoom");SignalStatus status_fn=(SignalStatus)runtime_np_lan_resolve("sceNpMatching2SignalingGetConnectionStatus");
    assert(initialize&&terminate&&create&&check&&register_signal&&search&&join&&leave&&status_fn);uint64_t init[6]={0};init[4]=0x30;assert(initialize(init)==0);uint64_t context_param[5]={0};uint16_t context=0;assert(create(context_param,&context)==0&&context==1);assert(register_signal(context,signal_cb,(void*)4)==0);
    SearchRequest search_request={0};search_request.world_id=1;RequestOpt search_opt={request_cb,(void*)1,0,0,{0}};uint32_t request=0;assert(search(context,&search_request,&search_opt,&request)==0);
    for(int attempt=0;attempt<2000&&!search_called;attempt++){LanMatchEvent event;int result=lan_match_poll(&host,&event);assert(result>=0);assert(check()==0);pause_ms();}
    assert(search_called==1&&found_room==room.room_id);JoinRequest join_request={0};join_request.room_id=found_room;RequestOpt join_opt={request_cb,(void*)2,0,0,{0}};assert(join(context,&join_request,&join_opt,&request)==0);
    LanMatchEvent host_join={0};for(int attempt=0;attempt<2000&&!join_called;attempt++){LanMatchEvent event;int result=lan_match_poll(&host,&event);assert(result>=0);if(result==1)host_join=event;assert(check()==0);pause_ms();}
    assert(host_join.type==LAN_MATCH_EVENT_PEER_JOINED&&join_called==1&&signal_called==1);int32_t connection=0;uint32_t address=0;uint16_t port=0,expected=(uint16_t)((42416u<<8)|(42416u>>8));
    assert(status_fn(context,found_room,1,&connection,&address,&port)==0&&connection==2&&address!=0&&port==expected);
    LeaveRequest leave_request={0};leave_request.room_id=found_room;RequestOpt leave_opt={request_cb,(void*)3,0,0,{0}};assert(leave(context,&leave_request,&leave_opt,&request)==0);assert(check()==0&&leave_called==1);
    LanMatchEvent host_leave={0};for(int attempt=0;attempt<2000&&host_leave.type==0;attempt++){int result=lan_match_poll(&host,&host_leave);assert(result>=0);pause_ms();}
    assert(host_leave.type==LAN_MATCH_EVENT_PEER_LEFT);assert(terminate()==0);lan_match_close(&host);puts("PASS: Matching2 client search, join, signaling and leave");return 0;
}
