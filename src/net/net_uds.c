#include "pd_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void pd_net_handle_packet_line(pd_net_ctx_t *ctx, const char *line);

#ifdef __3DS__
#include <3ds.h>
#include <3ds/services/uds.h>

#define PD_UDS_WLAN_COMM_ID 0x50443344u /* "PD3D" */
#define PD_UDS_CHANNEL      1

typedef struct {
    char host_name[32];
    int32_t variant;
} pd_uds_appdata_t;

static bool s_uds_ready = false;
static udsBindContext s_bind_ctx;
static bool s_bound = false;

bool pd_net_uds_send_line(pd_net_ctx_t *ctx, const char *line);

bool pd_net_uds_available(void) {
    if (!s_uds_ready) {
        if (R_SUCCEEDED(udsInit(0x3000, NULL))) {
            s_uds_ready = true;
        }
    }
    return s_uds_ready;
}

void pd_net_uds_close(pd_net_ctx_t *ctx) {
    if (!s_uds_ready) return;
    if (s_bound) {
        Result rc = udsUnbind(&s_bind_ctx);
        if (R_SUCCEEDED(rc)) {
            s_bound = false;
        } else if (ctx) {
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "udsUnbind failed: 0x%08lx", (unsigned long)rc);
            return;
        }
    }
    udsDestroyNetwork();
    udsDisconnectNetwork();
}

bool pd_net_uds_host(pd_net_ctx_t *ctx, pd_variant_t variant) {
    if (!ctx || !pd_net_uds_available()) return false;
    pd_net_uds_close(ctx);

    udsNetworkStruct netstruct;
    static const char passphrase[] = "PieceDuel3DS_UDS";
    udsGenerateDefaultNetworkStruct(&netstruct, PD_UDS_WLAN_COMM_ID, 0, 2);

    pd_uds_appdata_t appdata;
    memset(&appdata, 0, sizeof(appdata));
    snprintf(appdata.host_name, sizeof(appdata.host_name), "%s", ctx->local_name);
    appdata.variant = (int32_t)variant;

    if (R_FAILED(udsCreateNetwork(&netstruct, passphrase, sizeof(passphrase),
                                  &s_bind_ctx, PD_UDS_CHANNEL, UDS_DEFAULT_RECVBUFSIZE))) {
        return false;
    }
    udsSetApplicationData(&appdata, sizeof(appdata));
    s_bound = true;

    ctx->mode = PD_NET_MODE_UDS;
    ctx->state = PD_NET_STATE_HOSTING;
    ctx->is_host = true;
    ctx->my_seat = 0;
    ctx->variant = variant;
    ctx->preview_cell = PD_NONE;
    ctx->preview_piece = PD_NONE;
    ctx->rematch_offered_by_me = false;
    ctx->rematch_offered_by_peer = false;
    ctx->draw_offered_by_me = false;
    ctx->draw_offered_by_peer = false;
    pd_create_game(&ctx->game_state, 0, variant);
    ctx->state_updated = true;
    return true;
}

bool pd_net_uds_scan(pd_net_ctx_t *ctx) {
    if (!ctx || !pd_net_uds_available()) return false;
    ctx->mode = PD_NET_MODE_UDS;
    ctx->state = PD_NET_STATE_SCANNING;
    ctx->beacon_count = 0;

    size_t tmp_size = 0x4000;
    void *tmp_buf = malloc(tmp_size);
    if (!tmp_buf) return false;

    udsNetworkScanInfo *networks = NULL;
    size_t total_networks = 0;
    Result rc = udsScanBeacons(tmp_buf, tmp_size, &networks, &total_networks,
                               PD_UDS_WLAN_COMM_ID, 0, NULL, false);
    free(tmp_buf);
    if (R_FAILED(rc) || !networks) return false;

    for (size_t i = 0; i < total_networks && ctx->beacon_count < PD_MAX_BEACONS; i++) {
        pd_net_beacon_t *b = &ctx->beacons[ctx->beacon_count++];
        memset(b, 0, sizeof(*b));
        b->is_uds = true;
        b->network_id = networks[i].network.networkID;
        pd_uds_appdata_t appdata;
        size_t actual = 0;
        if (R_SUCCEEDED(udsGetNetworkStructApplicationData(&networks[i].network,
                                                           &appdata, sizeof(appdata), &actual)) &&
            actual == sizeof(appdata)) {
            snprintf(b->host_name, sizeof(b->host_name), "%.31s", appdata.host_name);
            b->variant = (pd_variant_t)appdata.variant;
        } else {
            snprintf(b->host_name, sizeof(b->host_name), "3DS_Host");
            b->variant = PD_VARIANT_STANDARD;
        }
    }
    free(networks);
    return true;
}

bool pd_net_uds_connect(pd_net_ctx_t *ctx, int beacon_index) {
    if (!ctx || !pd_net_uds_available()) return false;
    if (beacon_index < 0 || beacon_index >= ctx->beacon_count) return false;

    size_t tmp_size = 0x4000;
    void *tmp_buf = malloc(tmp_size);
    if (!tmp_buf) return false;

    udsNetworkScanInfo *networks = NULL;
    size_t total_networks = 0;
    Result rc = udsScanBeacons(tmp_buf, tmp_size, &networks, &total_networks,
                               PD_UDS_WLAN_COMM_ID, 0, NULL, false);
    free(tmp_buf);
    if (R_FAILED(rc) || !networks || (size_t)beacon_index >= total_networks) {
        free(networks);
        return false;
    }

    static const char passphrase[] = "PieceDuel3DS_UDS";
    rc = udsConnectNetwork(&networks[beacon_index].network, passphrase, sizeof(passphrase),
                           &s_bind_ctx, UDS_BROADCAST_NETWORKNODEID,
                           UDSCONTYPE_Client, PD_UDS_CHANNEL, UDS_DEFAULT_RECVBUFSIZE);
    free(networks);
    if (R_FAILED(rc)) return false;

    s_bound = true;
    ctx->mode = PD_NET_MODE_UDS;
    ctx->is_host = false;
    ctx->my_seat = 1;
    ctx->state = PD_NET_STATE_ACTIVE;

    char hello[128];
    snprintf(hello, sizeof(hello), "HELLO|%s\n", ctx->local_name);
    pd_net_uds_send_line(ctx, hello);
    return true;
}

bool pd_net_uds_send_line(pd_net_ctx_t *ctx, const char *line) {
    (void)ctx;
    if (!s_uds_ready || !s_bound || !line) return false;
    size_t len = strlen(line);
    Result rc = udsSendTo(UDS_BROADCAST_NETWORKNODEID, PD_UDS_CHANNEL,
                          UDS_SENDFLAG_Default, line, len);
    return R_SUCCEEDED(rc);
}

bool pd_net_uds_poll(pd_net_ctx_t *ctx) {
    if (!ctx || !s_uds_ready || !s_bound) return false;
    bool activity = false;
    char pkt_buf[1400];
    size_t actual_size = 0;
    u16 src_node = 0;

    while (R_SUCCEEDED(udsPullPacket(&s_bind_ctx, pkt_buf, sizeof(pkt_buf) - 1,
                                     &actual_size, &src_node)) &&
           actual_size > 0) {
        pkt_buf[actual_size] = '\0';
        char *nl = strchr(pkt_buf, '\n');
        if (nl) *nl = '\0';
        pd_net_handle_packet_line(ctx, pkt_buf);
        activity = true;
        actual_size = 0;
    }
    return activity;
}

#else /* Host build */

extern void pd_net_p2p_close(pd_net_ctx_t *ctx);
extern bool pd_net_p2p_poll(pd_net_ctx_t *ctx);
extern bool pd_net_p2p_send_line(pd_net_ctx_t *ctx, const char *line);

bool pd_net_uds_available(void) {
    const char *sim = getenv("PD_SIMULATE_UDS");
    return sim != NULL && sim[0] != '\0' && strcmp(sim, "0") != 0;
}

void pd_net_uds_close(pd_net_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->mode == PD_NET_MODE_UDS) {
        pd_net_p2p_close(ctx);
    }
}

bool pd_net_uds_host(pd_net_ctx_t *ctx, pd_variant_t variant) {
    if (!ctx || !pd_net_uds_available()) return false;
    if (!pd_net_p2p_host(ctx, PD_P2P_DEFAULT_UDP_PORT, 0, variant)) {
        return false;
    }
    ctx->mode = PD_NET_MODE_UDS;
    pd_net_p2p_send_beacon(ctx, "127.0.0.1", PD_P2P_DEFAULT_UDP_PORT);
    return true;
}

bool pd_net_uds_scan(pd_net_ctx_t *ctx) {
    if (!ctx || !pd_net_uds_available()) return false;
    if (ctx->udp_fd < 0) {
        if (!pd_net_p2p_start_scan(ctx, PD_P2P_DEFAULT_UDP_PORT)) {
            return false;
        }
    }
    ctx->mode = PD_NET_MODE_UDS;
    pd_net_p2p_poll(ctx);
    for (int i = 0; i < ctx->beacon_count; i++) {
        ctx->beacons[i].is_uds = true;
    }
    return true;
}

bool pd_net_uds_connect(pd_net_ctx_t *ctx, int beacon_index) {
    if (!ctx || !pd_net_uds_available()) return false;
    if (beacon_index < 0 || beacon_index >= ctx->beacon_count) return false;
    pd_net_beacon_t b = ctx->beacons[beacon_index];
    if (!pd_net_p2p_connect(ctx, b.ip_addr, b.tcp_port)) {
        return false;
    }
    ctx->mode = PD_NET_MODE_UDS;
    return true;
}

bool pd_net_uds_send_line(pd_net_ctx_t *ctx, const char *line) {
    if (!ctx || !pd_net_uds_available()) return false;
    return pd_net_p2p_send_line(ctx, line);
}

bool pd_net_uds_poll(pd_net_ctx_t *ctx) {
    if (!ctx || !pd_net_uds_available()) return false;
    return pd_net_p2p_poll(ctx);
}

#endif
