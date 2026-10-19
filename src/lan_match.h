#ifndef BB_LAN_MATCH_H
#define BB_LAN_MATCH_H

#include <stdint.h>

#define LAN_MATCH_NPID_SIZE 16
#define LAN_MATCH_MAX_INT_ATTRS 8
#define LAN_MATCH_MAX_BIN_SIZE 64

typedef struct { void *impl; } LanMatch;

typedef struct {
    uint16_t id;
    uint32_t value;
} LanMatchIntAttr;

typedef struct {
    uint16_t id;
    uint16_t size;
    uint8_t data[LAN_MATCH_MAX_BIN_SIZE];
} LanMatchBinAttr;

typedef struct {
    uint64_t room_id;
    uint32_t world_id;
    uint32_t flags;
    uint16_t max_members;
    uint16_t members;
    uint16_t p2p_port;
    char host_npid[LAN_MATCH_NPID_SIZE + 1];
    uint8_t int_attr_count;
    LanMatchIntAttr int_attrs[LAN_MATCH_MAX_INT_ATTRS];
    LanMatchBinAttr search_bin;
} LanMatchRoom;

typedef enum {
    LAN_MATCH_EVENT_NONE = 0,
    LAN_MATCH_EVENT_ROOM_FOUND,
    LAN_MATCH_EVENT_PEER_JOINED,
    LAN_MATCH_EVENT_JOIN_ACCEPTED,
    LAN_MATCH_EVENT_PEER_LEFT
} LanMatchEventType;

typedef struct {
    LanMatchEventType type;
    LanMatchRoom room;
    uint32_t peer_ipv4_be;
    uint16_t peer_discovery_port;
    uint16_t peer_p2p_port;
    uint16_t member_id;
    char peer_npid[LAN_MATCH_NPID_SIZE + 1];
} LanMatchEvent;

/* listen_port may be zero to ask the OS for a free port.  target_ipv4 is
 * normally 255.255.255.255, while tests can use 127.0.0.1. */
int lan_match_open(LanMatch *match, uint16_t listen_port,
                   const char *target_ipv4, uint16_t target_port);
void lan_match_close(LanMatch *match);
uint16_t lan_match_local_port(const LanMatch *match);
int lan_match_set_target(LanMatch *match, const char *target_ipv4,
                         uint16_t target_port);
int lan_match_host(LanMatch *match, const LanMatchRoom *room);
int lan_match_search(LanMatch *match, uint32_t world_id);
int lan_match_join(LanMatch *match, const LanMatchEvent *room,
                   const char *npid, uint16_t p2p_port);
int lan_match_leave(LanMatch *match, const LanMatchEvent *room,
                    const char *npid);
/* Nonblocking. Returns 1 for an event, 0 when no packet/event is ready, -1
 * for a socket or malformed-state error. */
int lan_match_poll(LanMatch *match, LanMatchEvent *event);

#endif
