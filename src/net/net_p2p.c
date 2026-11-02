#include "pd_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

extern void pd_net_handle_packet_line(pd_net_ctx_t *ctx, const char *line);

static bool set_nonblocking(int fd) {
#ifdef TCP_NODELAY
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void pd_net_p2p_close(pd_net_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->udp_fd > 0) {
        close(ctx->udp_fd);
        ctx->udp_fd = -1;
    }
    if (ctx->listen_fd > 0) {
        close(ctx->listen_fd);
        ctx->listen_fd = -1;
    }
    if (ctx->peer_fd > 0 && ctx->mode == PD_NET_MODE_LAN_P2P) {
        close(ctx->peer_fd);
        ctx->peer_fd = -1;
    }
}

bool pd_net_p2p_send_line(pd_net_ctx_t *ctx, const char *line) {
    if (!ctx || ctx->peer_fd <= 0 || !line) return false;
    size_t len = strlen(line);
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(ctx->peer_fd, line + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            struct pollfd pfd;
            pfd.fd = ctx->peer_fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            if (poll(&pfd, 1, 50) <= 0) return false;
        } else {
            return false;
        }
    }
    return true;
}

bool pd_net_p2p_host(pd_net_ctx_t *ctx, uint16_t udp_port, uint16_t tcp_port, pd_variant_t variant) {
    if (!ctx) return false;
    pd_net_p2p_close(ctx);

    int ufd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ufd < 0) return false;
    int yes = 1;
    setsockopt(ufd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(ufd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    set_nonblocking(ufd);

    struct sockaddr_in uaddr;
    memset(&uaddr, 0, sizeof(uaddr));
    uaddr.sin_family = AF_INET;
    uaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    uaddr.sin_port = htons(0); /* Ephemeral sender port so scanner can bind udp_port on same host */
    if (bind(ufd, (struct sockaddr *)&uaddr, sizeof(uaddr)) < 0) {
        close(ufd);
        return false;
    }

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        close(ufd);
        return false;
    }
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    set_nonblocking(lfd);

    struct sockaddr_in taddr;
    memset(&taddr, 0, sizeof(taddr));
    taddr.sin_family = AF_INET;
    taddr.sin_addr.s_addr = htonl(INADDR_ANY);
    taddr.sin_port = htons(tcp_port);
    if (bind(lfd, (struct sockaddr *)&taddr, sizeof(taddr)) < 0) {
        close(ufd);
        close(lfd);
        return false;
    }
    if (listen(lfd, 2) < 0) {
        close(ufd);
        close(lfd);
        return false;
    }

    socklen_t tlen = sizeof(taddr);
    if (getsockname(lfd, (struct sockaddr *)&taddr, &tlen) == 0) {
        ctx->bound_tcp_port = ntohs(taddr.sin_port);
    } else {
        ctx->bound_tcp_port = tcp_port;
    }

    ctx->udp_fd = ufd;
    ctx->listen_fd = lfd;
    ctx->bound_udp_port = udp_port ? udp_port : PD_P2P_DEFAULT_UDP_PORT;
    ctx->mode = PD_NET_MODE_LAN_P2P;
    ctx->state = PD_NET_STATE_HOSTING;
    ctx->is_host = true;
    ctx->my_seat = 0;
    ctx->variant = variant;
    ctx->rx_len = 0;
    ctx->rematch_offered_by_me = false;
    ctx->rematch_offered_by_peer = false;
    ctx->draw_offered_by_me = false;
    ctx->draw_offered_by_peer = false;
    ctx->preview_cell = PD_NONE;
    ctx->preview_piece = PD_NONE;
    pd_create_game(&ctx->game_state, 0, variant);
    ctx->state_updated = true;
    return true;
}

bool pd_net_p2p_send_beacon(pd_net_ctx_t *ctx, const char *dest_ip, uint16_t udp_port) {
    if (!ctx || ctx->udp_fd < 0) return false;
    uint16_t target_port = udp_port ? udp_port : (ctx->bound_udp_port ? ctx->bound_udp_port : PD_P2P_DEFAULT_UDP_PORT);
    const char *ip = (dest_ip && dest_ip[0] != '\0') ? dest_ip : "127.0.0.1";

    struct sockaddr_in daddr;
    memset(&daddr, 0, sizeof(daddr));
    daddr.sin_family = AF_INET;
    daddr.sin_port = htons(target_port);
    if (inet_pton(AF_INET, ip, &daddr.sin_addr) <= 0) {
        return false;
    }

    char pkt[128];
    int len = snprintf(pkt, sizeof(pkt), "PDP2P_BEACON|%s|%u|%d\n",
                       ctx->local_name, (unsigned)ctx->bound_tcp_port, (int)ctx->variant);
    if (len <= 0 || (size_t)len >= sizeof(pkt)) return false;

    ssize_t sent = sendto(ctx->udp_fd, pkt, (size_t)len, 0,
                          (struct sockaddr *)&daddr, sizeof(daddr));
    return sent == (ssize_t)len;
}

bool pd_net_p2p_start_scan(pd_net_ctx_t *ctx, uint16_t udp_port) {
    if (!ctx) return false;
    pd_net_p2p_close(ctx);

    int ufd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ufd < 0) return false;
    int yes = 1;
    setsockopt(ufd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(ufd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
    set_nonblocking(ufd);

    struct sockaddr_in uaddr;
    memset(&uaddr, 0, sizeof(uaddr));
    uaddr.sin_family = AF_INET;
    uaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    uaddr.sin_port = htons(udp_port);
    if (bind(ufd, (struct sockaddr *)&uaddr, sizeof(uaddr)) < 0) {
        close(ufd);
        return false;
    }

    socklen_t ulen = sizeof(uaddr);
    if (getsockname(ufd, (struct sockaddr *)&uaddr, &ulen) == 0) {
        ctx->bound_udp_port = ntohs(uaddr.sin_port);
    } else {
        ctx->bound_udp_port = udp_port;
    }

    ctx->udp_fd = ufd;
    ctx->beacon_count = 0;
    ctx->mode = PD_NET_MODE_LAN_P2P;
    ctx->state = PD_NET_STATE_SCANNING;
    ctx->is_host = false;
    ctx->rx_len = 0;
    return true;
}

bool pd_net_p2p_connect(pd_net_ctx_t *ctx, const char *host_ip, uint16_t tcp_port) {
    if (!ctx || !host_ip || tcp_port == 0) return false;
    if (ctx->peer_fd >= 0) {
        close(ctx->peer_fd);
        ctx->peer_fd = -1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(tcp_port);
    if (inet_pton(AF_INET, host_ip, &saddr.sin_addr) <= 0) {
        close(fd);
        return false;
    }

    if (connect(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        close(fd);
        return false;
    }

    set_nonblocking(fd);
    ctx->peer_fd = fd;
    ctx->mode = PD_NET_MODE_LAN_P2P;
    ctx->is_host = false;
    ctx->my_seat = 1;
    ctx->state = PD_NET_STATE_ACTIVE;
    ctx->rx_len = 0;
    ctx->rematch_offered_by_me = false;
    ctx->rematch_offered_by_peer = false;
    ctx->draw_offered_by_me = false;
    ctx->draw_offered_by_peer = false;
    ctx->preview_cell = PD_NONE;
    ctx->preview_piece = PD_NONE;

    char hello[128];
    snprintf(hello, sizeof(hello), "HELLO|%s\n", ctx->local_name);
    pd_net_p2p_send_line(ctx, hello);
    return true;
}

static void record_beacon(pd_net_ctx_t *ctx, const char *ip, const char *host_name,
                          uint16_t tcp_port, pd_variant_t variant) {
    for (int i = 0; i < ctx->beacon_count; i++) {
        if (strcmp(ctx->beacons[i].ip_addr, ip) == 0 && ctx->beacons[i].tcp_port == tcp_port) {
            snprintf(ctx->beacons[i].host_name, sizeof(ctx->beacons[i].host_name), "%.31s", host_name);
            ctx->beacons[i].variant = variant;
            return;
        }
    }
    if (ctx->beacon_count >= PD_MAX_BEACONS) return;
    pd_net_beacon_t *b = &ctx->beacons[ctx->beacon_count++];
    memset(b, 0, sizeof(*b));
    snprintf(b->host_name, sizeof(b->host_name), "%.31s", host_name);
    snprintf(b->ip_addr, sizeof(b->ip_addr), "%.63s", ip);
    b->tcp_port = tcp_port;
    b->variant = variant;
    b->network_id = (uint32_t)tcp_port;
    b->is_uds = false;
}

bool pd_net_p2p_poll(pd_net_ctx_t *ctx) {
    if (!ctx) return false;
    bool activity = false;

    /* 1. Check incoming UDP beacons when scanning */
    if (ctx->state == PD_NET_STATE_SCANNING && ctx->udp_fd >= 0) {
        char ubuf[256];
        struct sockaddr_in src;
        socklen_t slen = sizeof(src);
        ssize_t n;
        while ((n = recvfrom(ctx->udp_fd, ubuf, sizeof(ubuf) - 1, 0,
                             (struct sockaddr *)&src, &slen)) > 0) {
            ubuf[n] = '\0';
            char *nl = strchr(ubuf, '\n');
            if (nl) *nl = '\0';
            if (strncmp(ubuf, "PDP2P_BEACON|", 13) == 0) {
                char *p1 = ubuf + 13;
                char *p2 = strchr(p1, '|');
                if (p2) {
                    *p2++ = '\0';
                    char *p3 = strchr(p2, '|');
                    if (p3) {
                        *p3++ = '\0';
                        uint16_t port = (uint16_t)atoi(p2);
                        pd_variant_t var = (pd_variant_t)atoi(p3);
                        char ip_str[64];
                        if (inet_ntop(AF_INET, &src.sin_addr, ip_str, sizeof(ip_str))) {
                            record_beacon(ctx, ip_str, p1, port, var);
                            activity = true;
                        }
                    }
                }
            }
            slen = sizeof(src);
        }
    }

    /* 2. Check incoming TCP connection when hosting */
    if (ctx->is_host && ctx->listen_fd >= 0 && ctx->peer_fd < 0) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(ctx->listen_fd, (struct sockaddr *)&caddr, &clen);
        if (cfd >= 0) {
            set_nonblocking(cfd);
            ctx->peer_fd = cfd;
            ctx->rx_len = 0;
            activity = true;
        }
    }

    /* 3. Read line-delimited TCP packets from peer_fd */
    if (ctx->peer_fd >= 0) {
        while (ctx->rx_len < sizeof(ctx->rx_buf) - 1) {
            ssize_t n = recv(ctx->peer_fd, ctx->rx_buf + ctx->rx_len,
                             sizeof(ctx->rx_buf) - 1 - ctx->rx_len, 0);
            if (n > 0) {
                ctx->rx_len += (size_t)n;
                ctx->rx_buf[ctx->rx_len] = '\0';
                activity = true;
            } else if (n == 0) {
                close(ctx->peer_fd);
                ctx->peer_fd = -1;
                if (ctx->state != PD_NET_STATE_FINISHED) {
                    ctx->state = PD_NET_STATE_ERROR;
                    snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Peer disconnected");
                }
                break;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    break;
                }
                close(ctx->peer_fd);
                ctx->peer_fd = -1;
                ctx->state = PD_NET_STATE_ERROR;
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Socket read error");
                break;
            }
        }

        char *start = ctx->rx_buf;
        char *nl = NULL;
        while ((nl = memchr(start, '\n', ctx->rx_len - (size_t)(start - ctx->rx_buf))) != NULL) {
            *nl = '\0';
            if (nl > start && *(nl - 1) == '\r') {
                *(nl - 1) = '\0';
            }
            pd_net_handle_packet_line(ctx, start);
            start = nl + 1;
        }
        size_t consumed = (size_t)(start - ctx->rx_buf);
        if (consumed > 0) {
            size_t remaining = ctx->rx_len - consumed;
            if (remaining > 0) {
                memmove(ctx->rx_buf, start, remaining);
            }
            ctx->rx_len = remaining;
            ctx->rx_buf[ctx->rx_len] = '\0';
        }
    }

    return activity;
}
