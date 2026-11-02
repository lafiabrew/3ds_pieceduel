#ifndef PD_NET_H
#define PD_NET_H

#include "pd_engine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PD_P2P_DEFAULT_UDP_PORT 43210
#define PD_P2P_DEFAULT_TCP_PORT 43211
#define PD_WEB_DEFAULT_PORT 4321
#define PD_MAX_BEACONS 8

typedef enum {
    PD_NET_MODE_NONE = 0,
    PD_NET_MODE_UDS = 1,     /* 3DS-to-3DS Local Wireless via libctru uds */
    PD_NET_MODE_LAN_P2P = 2, /* Wi-Fi LAN UDP beacon + TCP lockstep */
    PD_NET_MODE_WEB = 3      /* pieceduel Web Server HTTP + Socket.IO v4 */
} pd_net_mode_t;

typedef enum {
    PD_NET_STATE_IDLE = 0,
    PD_NET_STATE_HOSTING = 1,
    PD_NET_STATE_SCANNING = 2,
    PD_NET_STATE_CONNECTING = 3,
    PD_NET_STATE_IN_LOBBY = 4,
    PD_NET_STATE_ACTIVE = 5,
    PD_NET_STATE_FINISHED = 6,
    PD_NET_STATE_ERROR = 7
} pd_net_state_t;

typedef enum {
    PD_PKT_HELLO = 1,
    PD_PKT_STATE = 2,
    PD_PKT_MOVE = 3,
    PD_PKT_PREVIEW = 4,
    PD_PKT_REMATCH = 5,
    PD_PKT_RESIGN = 6,
    PD_PKT_DRAW_OFFER = 7,
    PD_PKT_DRAW_RESPOND = 8,
    PD_PKT_CHAT = 9,
    PD_PKT_PING = 10,
    PD_PKT_PONG = 11
} pd_pkt_type_t;

typedef struct {
    char host_name[32];
    char ip_addr[64];
    uint16_t tcp_port;
    pd_variant_t variant;
    uint32_t network_id; /* UDS network ID or LAN token */
    bool is_uds;
} pd_net_beacon_t;

typedef struct {
    pd_net_mode_t mode;
    pd_net_state_t state;
    bool is_host;
    int my_seat; /* 0 or 1 */
    pd_variant_t variant;

    /* Match & Room info */
    char game_id[64];
    char invite_code[16];
    char local_name[32];
    char opponent_name[32];
    int preview_cell;  /* 0..15 or PD_NONE */
    int preview_piece; /* 0..15 or PD_NONE */
    bool rematch_offered_by_me;
    bool rematch_offered_by_peer;
    bool draw_offered_by_me;
    bool draw_offered_by_peer;
    char last_chat[204];
    char last_chat_sender[32];
    char error_msg[128];

    /* Discovered local rooms (UDS or LAN UDP) */
    pd_net_beacon_t beacons[PD_MAX_BEACONS];
    int beacon_count;

    /* Socket descriptors & internal buffers */
    int udp_fd;
    int listen_fd;
    int peer_fd;
    uint16_t bound_udp_port;
    uint16_t bound_tcp_port;
    char rx_buf[8192];
    size_t rx_len;

    /* Web server state */
    char server_host[128];
    uint16_t server_port;
    char auth_cookie[512];
    bool ws_handshake_done;
    bool sio_connected;

    /* Authoritative or synchronized game state */
    pd_game_state_t game_state;
    bool state_updated;
} pd_net_ctx_t;

/* Lifecycle */
void pd_net_init(pd_net_ctx_t *ctx, const char *local_name);
void pd_net_close(pd_net_ctx_t *ctx);
bool pd_net_poll(pd_net_ctx_t *ctx);

/* Serialization helpers (net_common.c) */
int pd_net_format_move_json(pd_move_t move, char *buf, size_t buf_size);
bool pd_net_parse_move_json(const char *json, pd_move_t *out_move);
int pd_net_format_state_json(const pd_game_state_t *state, char *buf, size_t buf_size);
bool pd_net_parse_state_json(const char *json, pd_game_state_t *out_state);

/* LAN P2P (net_p2p.c) */
bool pd_net_p2p_host(pd_net_ctx_t *ctx, uint16_t udp_port, uint16_t tcp_port, pd_variant_t variant);
bool pd_net_p2p_start_scan(pd_net_ctx_t *ctx, uint16_t udp_port);
bool pd_net_p2p_send_beacon(pd_net_ctx_t *ctx, const char *dest_ip, uint16_t udp_port);
bool pd_net_p2p_connect(pd_net_ctx_t *ctx, const char *host_ip, uint16_t tcp_port);

/* 3DS Local Wireless UDS (net_uds.c) */
bool pd_net_uds_available(void);
bool pd_net_uds_host(pd_net_ctx_t *ctx, pd_variant_t variant);
bool pd_net_uds_scan(pd_net_ctx_t *ctx);
bool pd_net_uds_connect(pd_net_ctx_t *ctx, int beacon_index);

/* Web Server Cross-Play (net_web.c) */
void pd_net_web_configure(pd_net_ctx_t *ctx, const char *host, uint16_t port, const char *cookie);
bool pd_net_web_login(pd_net_ctx_t *ctx, const char *identifier, const char *password);
bool pd_net_web_create_game(pd_net_ctx_t *ctx, const char *mode, const char *ai_level);
bool pd_net_web_join_game(pd_net_ctx_t *ctx, const char *id_or_code);
bool pd_net_web_matchmake(pd_net_ctx_t *ctx);
bool pd_net_web_connect_socket(pd_net_ctx_t *ctx, const char *game_id);

/* In-game actions across all active network modes (UDS, LAN P2P, Web) */
bool pd_net_send_move(pd_net_ctx_t *ctx, pd_move_t move);
bool pd_net_send_preview(pd_net_ctx_t *ctx, int cell);
bool pd_net_send_rematch(pd_net_ctx_t *ctx);
bool pd_net_send_resign(pd_net_ctx_t *ctx);
bool pd_net_send_draw_offer(pd_net_ctx_t *ctx);
bool pd_net_send_draw_respond(pd_net_ctx_t *ctx, bool accept);
bool pd_net_send_chat(pd_net_ctx_t *ctx, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* PD_NET_H */
