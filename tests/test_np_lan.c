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
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name){for(size_t i=0;i<count;++i)if(!strcmp(table[i].name,name))return(uintptr_t)table[i].function;return 0;}

typedef struct{char data[16];int8_t term,dummy[3];}OnlineId;
typedef struct{OnlineId handle;uint8_t rest[16];}NpId;
typedef void(ABI *StateCallback)(int32_t,uint32_t,NpId*,void*);
typedef void(ABI *ContextCallback)(uint16_t,uint16_t,uint8_t,int32_t,void*);
typedef int32_t(ABI *GetState)(int32_t,uint32_t*);
typedef int32_t(ABI *GetOnline)(int32_t,OnlineId*);
typedef int32_t(ABI *RegisterState)(StateCallback,void*);
typedef int32_t(ABI *CheckCallbacks)(void);
typedef int32_t(ABI *MatchingInit)(const void*);
typedef int32_t(ABI *CreateContext)(const void*,uint16_t*);
typedef int32_t(ABI *RegisterContext)(ContextCallback,void*);
typedef int32_t(ABI *StartContext)(uint16_t,uint64_t);
typedef void(ABI *RequestCallback)(uint16_t,uint32_t,uint16_t,int32_t,const void*,void*);
typedef struct{RequestCallback callback;void*arg;uint32_t timeout;uint16_t app_id;uint8_t padding[2];}RequestOpt;
typedef int32_t(ABI *SetDefaultRequest)(uint16_t,const RequestOpt*);
typedef int32_t(ABI *GetWorlds)(uint16_t,const void*,const RequestOpt*,uint32_t*);
typedef int32_t(ABI *CreateJoinRoom)(uint16_t,const void*,const RequestOpt*,uint32_t*);
typedef int32_t(ABI *RegisterRoom)(uint16_t,void(ABI *)(uint16_t,uint64_t,uint16_t,const void*,void*),void*);
typedef int32_t(ABI *RegisterSignal)(uint16_t,void(ABI *)(uint16_t,uint64_t,uint16_t,uint16_t,int32_t,void*),void*);
typedef int32_t(ABI *SignalStatus)(uint16_t,uint64_t,uint16_t,int32_t*,void*,uint16_t*);
typedef int32_t(ABI *Terminate)(void);
typedef int32_t(ABI *RegisterPlus)(void*,void*);
typedef int32_t(ABI *LookupCreateTitle)(const void*);
typedef int32_t(ABI *LookupCreateRequest)(int32_t,const void*);
typedef int32_t(ABI *LookupNpId)(int32_t,const OnlineId*,NpId*,const void*);
typedef int32_t(ABI *LookupPoll)(int32_t,int32_t*);
typedef int32_t(ABI *SignalingInitialize)(int64_t,int32_t,int32_t,int64_t);
typedef int32_t(ABI *SignalingCreate)(const NpId*,void*,void*,int32_t*);
typedef int32_t(ABI *SignalingActivate)(int32_t,const NpId*,int32_t*);
typedef int32_t(ABI *LegacySignalStatus)(int32_t,int32_t,uint32_t*,uint32_t*,uint16_t*);
typedef struct{uint16_t id;uint8_t padding[2];uint32_t num;}IntAttr;
typedef struct{uint16_t id;uint8_t padding[6];uint8_t*data;uint64_t size;}BinAttr;
typedef struct{
    uint16_t max_slot;uint8_t team_id,pad[5];uint32_t flags,world_id;uint64_t lobby_id;
    void*room_password,*password_mask,*group_config;uint64_t group_configs;void*join_group;
    OnlineId*allowed;uint64_t allowed_count;OnlineId*blocked;uint64_t blocked_count;
    BinAttr*internal_bin;uint64_t internal_bin_count;IntAttr*external_int;uint64_t external_int_count;
    BinAttr*external_search_bin;uint64_t external_search_bin_count;BinAttr*external_bin;uint64_t external_bin_count;
    BinAttr*member_internal_bin;uint64_t member_internal_bin_count;void*signaling;
}CreateRequest;

static int state_called,context_called,worlds_called,create_called,room_joined,room_left,signal_called;
static uint64_t created_room;
static ABI void state_cb(int32_t user,uint32_t state,NpId*id,void*arg){assert(user==1&&state==2&&id&&arg==(void*)1);assert(!strncmp(id->handle.data,"LanHunter",9));state_called++;}
static ABI void context_cb(uint16_t context,uint16_t event,uint8_t cause,int32_t error,void*arg){assert(context==1&&event==0x6f02&&cause==11&&error==0&&arg==(void*)2);context_called++;}
static ABI void worlds_cb(uint16_t context,uint32_t request,uint16_t event,int32_t error,const void*response,void*arg){
    const uint64_t*words=(const uint64_t*)response;assert(context==1&&request&&event==2&&error==0&&response&&arg==(void*)3);assert(words[0]&&words[1]==1);worlds_called++;
}
static ABI void create_cb(uint16_t context,uint32_t request,uint16_t event,int32_t error,const void*response,void*arg){
    const uint64_t*words=(const uint64_t*)response;assert(context==1&&request&&event==0x101&&error==0&&response&&arg==(void*)4);
    assert(words[0]);created_room=*(const uint64_t*)((const uint8_t*)words[0]+24);assert(created_room);create_called++;
}
static ABI void room_cb(uint16_t context,uint64_t room,uint16_t event,const void*data,void*arg){
    assert(context==1&&room==created_room&&data&&arg==(void*)5);if(event==0x1101)room_joined++;else if(event==0x1102)room_left++;else assert(0);
}
static ABI void signal_cb(uint16_t context,uint64_t room,uint16_t member,uint16_t event,int32_t error,void*arg){
    assert(context==1&&room==created_room&&member==2&&event==0x5102&&error==0&&arg==(void*)6);signal_called++;
}

int main(void){
#ifdef _WIN32
    assert(_putenv_s("BB_NETWORK_MODE","")==0);assert(_putenv_s("BB_LAN_NPID","LanHunter")==0);
    assert(_putenv_s("BB_LAN_DISCOVERY_TARGET","127.0.0.1")==0);assert(_putenv_s("BB_LAN_DISCOVERY_PORT","42314")==0);
    assert(_putenv_s("BB_P2P_PORT","42316")==0);
#else
    assert(unsetenv("BB_NETWORK_MODE")==0);assert(setenv("BB_LAN_NPID","LanHunter",1)==0);
    assert(setenv("BB_LAN_DISCOVERY_TARGET","127.0.0.1",1)==0);assert(setenv("BB_LAN_DISCOVERY_PORT","42314",1)==0);
    assert(setenv("BB_P2P_PORT","42316",1)==0);
#endif
    assert(runtime_np_lan_resolve("sceNpGetState")==0);
#ifdef _WIN32
    assert(_putenv_s("BB_NETWORK_MODE","lan")==0);
#else
    assert(setenv("BB_NETWORK_MODE","lan",1)==0);
#endif
    GetState get_state=(GetState)runtime_np_lan_resolve("sceNpGetState");GetOnline get_online=(GetOnline)runtime_np_lan_resolve("sceNpGetOnlineId");
    RegisterState register_state=(RegisterState)runtime_np_lan_resolve("sceNpRegisterStateCallback");CheckCallbacks check=(CheckCallbacks)runtime_np_lan_resolve("sceNpCheckCallback");
    assert(get_state&&get_online&&register_state&&check);uint32_t state=0;OnlineId id={{0},0,{0}};
    assert(get_state(1,&state)==0&&state==2);assert(get_online(1,&id)==0&&!strncmp(id.data,"LanHunter",9));
    assert(register_state(state_cb,(void*)1)==0);assert(check()==0&&state_called==1);
    RegisterPlus register_plus=(RegisterPlus)runtime_np_lan_resolve("sceNpRegisterPlusEventCallback");
    LookupCreateTitle lookup_title=(LookupCreateTitle)runtime_np_lan_resolve("sceNpLookupCreateTitleCtx");
    LookupCreateRequest lookup_request=(LookupCreateRequest)runtime_np_lan_resolve("sceNpLookupCreateAsyncRequest");
    LookupNpId lookup_npid=(LookupNpId)runtime_np_lan_resolve("sceNpLookupNpId");LookupPoll lookup_poll=(LookupPoll)runtime_np_lan_resolve("sceNpLookupPollAsync");
    assert(register_plus&&register_plus((void*)1,(void*)2)==0);int32_t lookup_context=lookup_title(&id);assert(lookup_context>0);
    int32_t lookup_id=lookup_request(lookup_context,NULL),lookup_result=-1;NpId looked_up={0};assert(lookup_id>0);
    assert(lookup_npid(lookup_id,&id,&looked_up,NULL)==0&&!strncmp(looked_up.handle.data,"LanHunter",9));assert(lookup_poll(lookup_id,&lookup_result)==0&&lookup_result==0);
    SignalingInitialize signal_initialize=(SignalingInitialize)runtime_np_lan_resolve("sceNpSignalingInitialize");
    SignalingCreate signal_create=(SignalingCreate)runtime_np_lan_resolve("sceNpSignalingCreateContext");
    SignalingActivate signal_activate=(SignalingActivate)runtime_np_lan_resolve("sceNpSignalingActivateConnection");
    LegacySignalStatus legacy_status=(LegacySignalStatus)runtime_np_lan_resolve("sceNpSignalingGetConnectionStatus");
    assert(signal_initialize&&signal_create&&signal_activate&&legacy_status);assert(signal_initialize(0,0,0,0)==0);
    int32_t signal_context=0,signal_connection=0;assert(signal_create(&looked_up,NULL,NULL,&signal_context)==0&&signal_context==1);
    assert(signal_activate(signal_context,&looked_up,&signal_connection)==0&&signal_connection==1);uint32_t legacy_state=0,legacy_address=1;uint16_t legacy_port=1;
    assert(legacy_status(signal_context,signal_connection,&legacy_state,&legacy_address,&legacy_port)==0&&legacy_state==1&&legacy_address==0&&legacy_port==0);
    uint64_t init[6]={0};init[4]=0x30;MatchingInit initialize=(MatchingInit)runtime_np_lan_resolve("sceNpMatching2Initialize");
    CreateContext create=(CreateContext)runtime_np_lan_resolve("sceNpMatching2CreateContext");RegisterContext register_context=(RegisterContext)runtime_np_lan_resolve("sceNpMatching2RegisterContextCallback");StartContext start=(StartContext)runtime_np_lan_resolve("sceNpMatching2ContextStart");
    assert(initialize&&create&&register_context&&start);assert(initialize(init)==0);uint16_t context=0;uint64_t parameter[5]={0};
    assert(create(parameter,&context)==0&&context==1);assert(register_context(context_cb,(void*)2)==0);assert(start(context,1000000)==0);assert(check()==0&&context_called==1);
    SetDefaultRequest set_default=(SetDefaultRequest)runtime_np_lan_resolve("sceNpMatching2SetDefaultRequestOptParam");GetWorlds get_worlds=(GetWorlds)runtime_np_lan_resolve("sceNpMatching2GetWorldInfoList");
    RequestOpt opt={worlds_cb,(void*)3,0,0,{0}};uint16_t server=1;uint32_t request=0;assert(set_default(context,&opt)==0);assert(get_worlds(context,&server,NULL,&request)==0&&request&&worlds_called==0);assert(check()==0&&worlds_called==1);
    CreateJoinRoom create_room=(CreateJoinRoom)runtime_np_lan_resolve("sceNpMatching2CreateJoinRoom");
    RegisterRoom register_room=(RegisterRoom)runtime_np_lan_resolve("sceNpMatching2RegisterRoomEventCallback");
    RegisterSignal register_signal=(RegisterSignal)runtime_np_lan_resolve("sceNpMatching2RegisterSignalingCallback");
    SignalStatus signal_status=(SignalStatus)runtime_np_lan_resolve("sceNpMatching2SignalingGetConnectionStatus");
    assert(create_room&&register_room&&register_signal&&signal_status);assert(register_room(context,room_cb,(void*)5)==0);assert(register_signal(context,signal_cb,(void*)6)==0);
    IntAttr int_attr={0x4c,{0},109};uint8_t bin_data[3]={'D','L','C'};BinAttr bin_attr={0x54,{0},bin_data,3};
    CreateRequest create_request={0};create_request.max_slot=5;create_request.world_id=1;create_request.external_int=&int_attr;create_request.external_int_count=1;create_request.external_search_bin=&bin_attr;create_request.external_search_bin_count=1;
    RequestOpt create_opt={create_cb,(void*)4,0,0,{0}};assert(create_room(context,&create_request,&create_opt,&request)==0);assert(check()==0&&create_called==1);
    LanMatch client={0};assert(lan_match_open(&client,0,"127.0.0.1",42314)==0);assert(lan_match_search(&client,1)==0);
    LanMatchEvent found={0};for(int attempt=0;attempt<2000&&found.type==0;attempt++){LanMatchEvent event;assert(check()==0);int result=lan_match_poll(&client,&event);assert(result>=0);if(result==1)found=event;
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay={0,1000000};nanosleep(&delay,NULL);
#endif
    }
    assert(found.type==LAN_MATCH_EVENT_ROOM_FOUND&&found.room.room_id==created_room);assert(found.room.int_attrs[0].value==109&&found.room.search_bin.size==3);
    assert(lan_match_join(&client,&found,"GuestHunter",42317)==0);LanMatchEvent accepted={0};
    for(int attempt=0;attempt<2000&&accepted.type==0;attempt++){LanMatchEvent event;assert(check()==0);int result=lan_match_poll(&client,&event);assert(result>=0);if(result==1)accepted=event;
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay={0,1000000};nanosleep(&delay,NULL);
#endif
    }
    assert(accepted.type==LAN_MATCH_EVENT_JOIN_ACCEPTED&&room_joined==1&&signal_called==1);int32_t status=0;uint32_t address=0;uint16_t port=0;
    uint16_t expected_port=(uint16_t)((42317u<<8)|(42317u>>8));
    assert(signal_status(context,created_room,2,&status,&address,&port)==0&&status==2&&address!=0&&port==expected_port);
    assert(lan_match_leave(&client,&found,"GuestHunter")==0);for(int attempt=0;attempt<2000&&!room_left;attempt++){assert(check()==0);
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec delay={0,1000000};nanosleep(&delay,NULL);
#endif
    }
    assert(room_left==1);lan_match_close(&client);Terminate terminate=(Terminate)runtime_np_lan_resolve("sceNpMatching2Terminate");assert(terminate&&terminate()==0);
    puts("PASS: native LAN NP lifecycle, room discovery, join and signaling");return 0;
}
