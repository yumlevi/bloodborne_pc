#include "lan_match.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static void wait_a_moment(void){Sleep(1);}
#else
#include <time.h>
static void wait_a_moment(void){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}
#endif

static int wait_event(LanMatch *match,LanMatchEventType type,LanMatchEvent *event) {
    for(int attempt=0;attempt<2000;attempt++) {
        int result=lan_match_poll(match,event);assert(result>=0);
        if(result==1&&event->type==type)return 1;wait_a_moment();
    }
    return 0;
}

static int wait_found(LanMatch *host,LanMatch *client,LanMatchEvent *found) {
    for(int attempt=0;attempt<2000;attempt++) {
        LanMatchEvent host_event;int host_result=lan_match_poll(host,&host_event);assert(host_result>=0);
        int client_result=lan_match_poll(client,found);assert(client_result>=0);
        if(client_result==1&&found->type==LAN_MATCH_EVENT_ROOM_FOUND)return 1;wait_a_moment();
    }
    return 0;
}

int main(void) {
    LanMatch host={0},client={0};
    assert(lan_match_open(&host,0,"127.0.0.1",1)==0);
    assert(lan_match_open(&client,0,"127.0.0.1",lan_match_local_port(&host))==0);
    LanMatchRoom room={0};room.room_id=0x1122334455667788ull;room.world_id=1;
    room.flags=0x42;room.max_members=5;room.members=1;room.p2p_port=31316;
    memcpy(room.host_npid,"HostHunter",10);room.int_attr_count=1;room.int_attrs[0].id=0x4c;
    room.int_attrs[0].value=109;room.search_bin.id=0x54;room.search_bin.size=3;
    memcpy(room.search_bin.data,"DLC",3);assert(lan_match_host(&host,&room)==0);
    assert(lan_match_search(&client,1)==0);
    LanMatchEvent found,joined,accepted,left;
    assert(wait_found(&host,&client,&found));
    assert(found.room.room_id==room.room_id&&found.room.world_id==1);
    assert(found.room.max_members==5&&found.peer_p2p_port==31316);
    assert(found.room.int_attr_count==1&&found.room.int_attrs[0].value==109);
    assert(found.room.search_bin.id==0x54&&found.room.search_bin.size==3);
    assert(!strcmp(found.peer_npid,"HostHunter"));
    assert(lan_match_join(&client,&found,"GuestHunter",31317)==0);
    assert(wait_event(&host,LAN_MATCH_EVENT_PEER_JOINED,&joined));
    assert(joined.peer_p2p_port==31317&&!strcmp(joined.peer_npid,"GuestHunter"));
    assert(wait_event(&client,LAN_MATCH_EVENT_JOIN_ACCEPTED,&accepted));
    assert(accepted.room.room_id==room.room_id&&accepted.member_id==2);
    assert(accepted.peer_p2p_port==31316&&!strcmp(accepted.peer_npid,"HostHunter"));
    assert(lan_match_leave(&client,&found,"GuestHunter")==0);
    assert(wait_event(&host,LAN_MATCH_EVENT_PEER_LEFT,&left));
    assert(!strcmp(left.peer_npid,"GuestHunter"));
    lan_match_close(&client);lan_match_close(&host);
    puts("PASS: native LAN room discovery, join acknowledgement and leave");return 0;
}
