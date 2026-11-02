#include "pd_net.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static uint64_t net_now_ms(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) return 0;
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

static bool set_nonblocking_fd(int fd) {
#ifdef TCP_NODELAY
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#endif
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

#define PD_NET_CONNECT_TIMEOUT_MS 1500
#define PD_NET_IO_TIMEOUT_MS      1500

static int connect_tcp_blocking(const char *host, uint16_t port) {
    if (!host || port == 0) return -1;

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &saddr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(host);
        if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
            return -1;
        }
        memcpy(&saddr.sin_addr, he->h_addr_list[0], sizeof(saddr.sin_addr));
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    int rc = connect(fd, (struct sockaddr *)&saddr, sizeof(saddr));
    if (rc < 0) {
        if (errno != EINPROGRESS && errno != EALREADY && errno != EWOULDBLOCK) {
            close(fd);
            return -1;
        }
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        if (poll(&pfd, 1, PD_NET_CONNECT_TIMEOUT_MS) <= 0) {
            close(fd);
            return -1;
        }
        int so_err = 0;
        socklen_t slen = sizeof(so_err);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &slen) < 0 || so_err != 0) {
            close(fd);
            return -1;
        }
    }

    return fd;
}

static bool send_all_blocking(int fd, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n > 0) {
            sent += (size_t)n;
        } else if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            if (poll(&pfd, 1, 100) <= 0) return false;
        } else {
            return false;
        }
    }
    return true;
}

/* Simple HTTP request/response helper for REST endpoints */
static int http_request(const char *host, uint16_t port, const char *method,
                        const char *path, const char *content_type,
                        const char *cookie, const char *body,
                        char *resp_buf, size_t resp_buf_size,
                        char **out_body) {
    int fd = connect_tcp_blocking(host, port);
    if (fd < 0) return -1;

    size_t body_len = body ? strlen(body) : 0;
    char req[2048];
    int req_len = snprintf(
        req, sizeof(req),
        "%s %s HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Connection: close\r\n"
        "%s%s%s"
        "%s%s%s"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        method, path, host, (unsigned)port,
        content_type ? "Content-Type: " : "",
        content_type ? content_type : "",
        content_type ? "\r\n" : "",
        (cookie && cookie[0]) ? "Cookie: " : "",
        (cookie && cookie[0]) ? cookie : "",
        (cookie && cookie[0]) ? "\r\n" : "",
        body_len,
        body ? body : "");

    if (req_len <= 0 || (size_t)req_len >= sizeof(req)) {
        close(fd);
        return -1;
    }

    if (!send_all_blocking(fd, req, (size_t)req_len)) {
        close(fd);
        return -1;
    }

    uint64_t deadline_ms = net_now_ms() + PD_NET_IO_TIMEOUT_MS;
    size_t total = 0;
    char *hdr_end = NULL;
    bool has_content_length = false;
    size_t content_length = 0;

    while (total < resp_buf_size - 1) {
        ssize_t n = recv(fd, resp_buf + total, resp_buf_size - 1 - total, 0);
        if (n > 0) {
            total += (size_t)n;
            resp_buf[total] = '\0';
            if (!hdr_end) {
                hdr_end = strstr(resp_buf, "\r\n\r\n");
                if (hdr_end) {
                    const char *p = resp_buf;
                    while (p < hdr_end) {
                        const char *line_end = strstr(p, "\r\n");
                        if (!line_end || line_end > hdr_end) break;
                        if ((size_t)(line_end - p) >= 15 &&
                            strncasecmp(p, "Content-Length:", 15) == 0) {
                            const char *val = p + 15;
                            while (val < line_end && isspace((unsigned char)*val)) val++;
                            char *endp = NULL;
                            long cl = strtol(val, &endp, 10);
                            if (endp != val && cl >= 0) {
                                has_content_length = true;
                                content_length = (size_t)cl;
                            }
                            break;
                        }
                        p = line_end + 2;
                    }
                }
            }
            if (hdr_end && has_content_length) {
                size_t hdr_bytes = (size_t)((hdr_end + 4) - resp_buf);
                if (total >= hdr_bytes + content_length) {
                    break;
                }
            }
        } else if (n == 0) {
            break;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            uint64_t now = net_now_ms();
            if (now >= deadline_ms) {
                break;
            }
            int wait_ms = (int)(deadline_ms - now);
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            if (poll(&pfd, 1, wait_ms) <= 0) {
                break;
            }
        } else if (errno != EINTR) {
            break;
        }
        if (net_now_ms() >= deadline_ms) {
            break;
        }
    }
    close(fd);
    resp_buf[total] = '\0';

    int status_code = 0;
    if (strncmp(resp_buf, "HTTP/1.", 7) == 0) {
        const char *sp = strchr(resp_buf, ' ');
        if (sp) status_code = atoi(sp + 1);
    }

    if (!hdr_end) {
        hdr_end = strstr(resp_buf, "\r\n\r\n");
    }
    if (out_body) {
        *out_body = hdr_end ? (hdr_end + 4) : (resp_buf + total);
    }
    return status_code;
}

static bool extract_json_string(const char *json, const char *key, char *out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) return false;
    char pat[64];
    int plen = snprintf(pat, sizeof(pat), "\"%s\"", key);
    if (plen <= 0 || (size_t)plen >= sizeof(pat)) return false;

    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        p += plen;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != ':') continue;
        p++;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != '"') return false;
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i < out_size - 1) {
            if (*p == '\\' && *(p + 1) != '\0') p++;
            out[i++] = *p++;
        }
        out[i] = '\0';
        return true;
    }
    return false;
}

static bool extract_json_int(const char *json, const char *key, int *out_val, bool *is_null) {
    if (!json || !key) return false;
    char pat[64];
    int plen = snprintf(pat, sizeof(pat), "\"%s\"", key);
    if (plen <= 0 || (size_t)plen >= sizeof(pat)) return false;

    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        p += plen;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != ':') continue;
        p++;
        while (*p && isspace((unsigned char)*p)) p++;
        if (strncmp(p, "null", 4) == 0) {
            if (is_null) *is_null = true;
            if (out_val) *out_val = PD_NONE;
            return true;
        }
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p) return false;
        if (is_null) *is_null = false;
        if (out_val) *out_val = (int)v;
        return true;
    }
    return false;
}

static bool extract_cookie_token(const char *headers, const char *cookie_name,
                                 char *out, size_t out_size) {
    if (!headers || !cookie_name || !out || out_size == 0) return false;
    const char *p = headers;
    size_t nlen = strlen(cookie_name);
    while ((p = strstr(p, cookie_name)) != NULL) {
        if (p[nlen] == '=') {
            const char *end = strpbrk(p, ";\r\n");
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (len >= out_size) len = out_size - 1;
            memcpy(out, p, len);
            out[len] = '\0';
            return true;
        }
        p += nlen;
    }
    return false;
}

static void url_encode(const char *src, char *dst, size_t dst_size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; src && src[i] && j + 4 < dst_size; i++) {
        unsigned char c = (unsigned char)src[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = (char)c;
        } else {
            dst[j++] = '%';
            dst[j++] = hex[(c >> 4) & 0xF];
            dst[j++] = hex[c & 0xF];
        }
    }
    dst[j] = '\0';
}

void pd_net_web_close(pd_net_ctx_t *ctx) {
    if (!ctx) return;
    if (ctx->peer_fd >= 0 && ctx->mode == PD_NET_MODE_WEB) {
        close(ctx->peer_fd);
        ctx->peer_fd = -1;
    }
    ctx->ws_handshake_done = false;
    ctx->sio_connected = false;
}

void pd_net_web_configure(pd_net_ctx_t *ctx, const char *host, uint16_t port, const char *cookie) {
    if (!ctx) return;
    ctx->mode = PD_NET_MODE_WEB;
    if (host && host[0] != '\0') {
        if (host != ctx->server_host) {
            snprintf(ctx->server_host, sizeof(ctx->server_host), "%s", host);
        }
    } else if (ctx->server_host[0] == '\0') {
        snprintf(ctx->server_host, sizeof(ctx->server_host), "127.0.0.1");
    }
    ctx->server_port = (port > 0) ? port : PD_WEB_DEFAULT_PORT;
    if (cookie && cookie != ctx->auth_cookie) {
        snprintf(ctx->auth_cookie, sizeof(ctx->auth_cookie), "%s", cookie);
    }
}

bool pd_net_web_login(pd_net_ctx_t *ctx, const char *identifier, const char *password) {
    if (!ctx || !identifier || !password) return false;
    ctx->mode = PD_NET_MODE_WEB;

    char resp[4096];
    char *body = NULL;
    int status = http_request(ctx->server_host, ctx->server_port, "GET",
                              "/api/auth/csrf", NULL, NULL, NULL,
                              resp, sizeof(resp), &body);
    if (status < 200 || status >= 400 || !body) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "CSRF request failed");
        return false;
    }

    char csrf_token[128] = {0};
    if (!extract_json_string(body, "csrfToken", csrf_token, sizeof(csrf_token))) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Missing csrfToken");
        return false;
    }

    char csrf_cookie[256] = {0};
    if (!extract_cookie_token(resp, "__Host-authjs.csrf-token", csrf_cookie, sizeof(csrf_cookie))) {
        extract_cookie_token(resp, "authjs.csrf-token", csrf_cookie, sizeof(csrf_cookie));
    }

    char enc_csrf[256], enc_id[256], enc_pw[256];
    url_encode(csrf_token, enc_csrf, sizeof(enc_csrf));
    url_encode(identifier, enc_id, sizeof(enc_id));
    url_encode(password, enc_pw, sizeof(enc_pw));

    char form_body[1024];
    snprintf(form_body, sizeof(form_body),
             "csrfToken=%s&identifier=%s&password=%s",
             enc_csrf, enc_id, enc_pw);

    status = http_request(ctx->server_host, ctx->server_port, "POST",
                          "/api/auth/callback/credentials",
                          "application/x-www-form-urlencoded",
                          csrf_cookie, form_body,
                          resp, sizeof(resp), &body);
    if (status < 200 || status >= 400) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Login failed (%d)", status);
        return false;
    }

    char session_cookie[512] = {0};
    if (!extract_cookie_token(resp, "__Secure-authjs.session-token",
                              session_cookie, sizeof(session_cookie))) {
        extract_cookie_token(resp, "authjs.session-token",
                             session_cookie, sizeof(session_cookie));
    }
    if (session_cookie[0] == '\0') {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "No session cookie received");
        return false;
    }

    snprintf(ctx->auth_cookie, sizeof(ctx->auth_cookie), "%s", session_cookie);
    if (identifier[0] != '\0' && strchr(identifier, '@') == NULL) {
        snprintf(ctx->local_name, sizeof(ctx->local_name), "%s", identifier);
    }
    return true;
}

bool pd_net_web_create_game(pd_net_ctx_t *ctx, const char *mode, const char *ai_level) {
    if (!ctx) return false;
    ctx->mode = PD_NET_MODE_WEB;

    char req_body[256];
    const char *m = (mode && mode[0]) ? mode : "pvp";
    if (strcmp(m, "ai") == 0 && ai_level && ai_level[0]) {
        snprintf(req_body, sizeof(req_body), "{\"mode\":\"ai\",\"aiLevel\":\"%s\"}", ai_level);
    } else {
        snprintf(req_body, sizeof(req_body), "{\"mode\":\"%s\"}", m);
    }

    char resp[4096];
    char *body = NULL;
    int status = http_request(ctx->server_host, ctx->server_port, "POST",
                              "/api/games", "application/json",
                              ctx->auth_cookie, req_body,
                              resp, sizeof(resp), &body);
    if (status < 200 || status >= 300 || !body) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Create game failed (%d)", status);
        return false;
    }

    ctx->game_id[0] = '\0';
    ctx->invite_code[0] = '\0';
    extract_json_string(body, "id", ctx->game_id, sizeof(ctx->game_id));
    extract_json_string(body, "inviteCode", ctx->invite_code, sizeof(ctx->invite_code));
    if (ctx->game_id[0] == '\0') return false;

    ctx->is_host = true;
    ctx->my_seat = 0;
    ctx->state = PD_NET_STATE_IN_LOBBY;
    return true;
}

bool pd_net_web_join_game(pd_net_ctx_t *ctx, const char *id_or_code) {
    if (!ctx || !id_or_code || id_or_code[0] == '\0') return false;
    ctx->mode = PD_NET_MODE_WEB;

    char req_body[256];
    snprintf(req_body, sizeof(req_body), "{\"idOrCode\":\"%s\"}", id_or_code);

    char resp[4096];
    char *body = NULL;
    int status = http_request(ctx->server_host, ctx->server_port, "POST",
                              "/api/games/join", "application/json",
                              ctx->auth_cookie, req_body,
                              resp, sizeof(resp), &body);
    if (status < 200 || status >= 300 || !body) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Join game failed (%d)", status);
        return false;
    }

    ctx->game_id[0] = '\0';
    extract_json_string(body, "id", ctx->game_id, sizeof(ctx->game_id));
    extract_json_string(body, "inviteCode", ctx->invite_code, sizeof(ctx->invite_code));
    if (ctx->game_id[0] == '\0') return false;

    ctx->is_host = false;
    ctx->my_seat = 1;
    ctx->state = PD_NET_STATE_ACTIVE;
    return true;
}

bool pd_net_web_matchmake(pd_net_ctx_t *ctx) {
    if (!ctx) return false;
    ctx->mode = PD_NET_MODE_WEB;

    char resp[4096];
    char *body = NULL;
    int status = http_request(ctx->server_host, ctx->server_port, "POST",
                              "/api/matchmake", "application/json",
                              ctx->auth_cookie, "{}",
                              resp, sizeof(resp), &body);
    if (status < 200 || status >= 300 || !body) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Matchmake failed (%d)", status);
        return false;
    }

    ctx->game_id[0] = '\0';
    if (!extract_json_string(body, "gameId", ctx->game_id, sizeof(ctx->game_id))) {
        extract_json_string(body, "id", ctx->game_id, sizeof(ctx->game_id));
    }
    char mm_status[32] = {0};
    if (extract_json_string(body, "status", mm_status, sizeof(mm_status))) {
        if (strcmp(mm_status, "waiting") == 0) {
            ctx->state = PD_NET_STATE_IN_LOBBY;
        } else if (strcmp(mm_status, "matched") == 0) {
            ctx->state = PD_NET_STATE_ACTIVE;
        }
    }
    return ctx->game_id[0] != '\0';
}

/* RFC 6455 masked client frame sender */
static bool ws_send_frame(pd_net_ctx_t *ctx, uint8_t opcode, const void *payload, size_t len) {
    if (!ctx || ctx->peer_fd < 0 || !ctx->ws_handshake_done) return false;
    uint8_t frame[2048];
    if (len + 14 > sizeof(frame)) return false;

    size_t idx = 0;
    frame[idx++] = (uint8_t)(0x80 | (opcode & 0x0F)); /* FIN=1 */
    if (len < 126) {
        frame[idx++] = (uint8_t)(0x80 | len);
    } else {
        frame[idx++] = (uint8_t)(0x80 | 126);
        frame[idx++] = (uint8_t)((len >> 8) & 0xFF);
        frame[idx++] = (uint8_t)(len & 0xFF);
    }

    const uint8_t mask[4] = {0x37, 0xFA, 0x21, 0x3D};
    frame[idx++] = mask[0];
    frame[idx++] = mask[1];
    frame[idx++] = mask[2];
    frame[idx++] = mask[3];

    const uint8_t *p = (const uint8_t *)payload;
    for (size_t i = 0; i < len; i++) {
        frame[idx++] = p[i] ^ mask[i & 3];
    }

    return send_all_blocking(ctx->peer_fd, frame, idx);
}

/* RFC 6455 masked client text frame sender */
static bool ws_send_text_frame(pd_net_ctx_t *ctx, const char *payload) {
    if (!payload) return false;
    return ws_send_frame(ctx, 0x1, payload, strlen(payload));
}

/* Find `"key":` at top level (brace/bracket depth == 1) of a JSON object */
static const char *find_top_level_json_value(const char *json, const char *key) {
    if (!json || !key) return NULL;
    size_t klen = strlen(key);
    const char *p = json;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '{') return NULL;

    int depth = 0;
    while (*p) {
        if (*p == '"') {
            const char *str_start = p + 1;
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && *(p + 1) != '\0') p += 2;
                else p++;
            }
            const char *str_end = p;
            if (*p == '"') p++;
            if (depth == 1 && (size_t)(str_end - str_start) == klen &&
                strncmp(str_start, key, klen) == 0) {
                const char *q = p;
                while (*q && isspace((unsigned char)*q)) q++;
                if (*q == ':') {
                    q++;
                    while (*q && isspace((unsigned char)*q)) q++;
                    return q;
                }
            }
            continue;
        }
        if (*p == '{' || *p == '[') {
            depth++;
        } else if (*p == '}' || *p == ']') {
            depth--;
            if (depth <= 0) break;
        }
        p++;
    }
    return NULL;
}

static bool extract_top_level_json_string(const char *json, const char *key, char *out, size_t out_size) {
    if (!out || out_size == 0) return false;
    const char *p = find_top_level_json_value(json, key);
    if (!p || *p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p + 1) != '\0') p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

static bool extract_top_level_json_int(const char *json, const char *key, int *out_val, bool *is_null) {
    const char *p = find_top_level_json_value(json, key);
    if (!p) return false;
    if (strncmp(p, "null", 4) == 0) {
        if (is_null) *is_null = true;
        if (out_val) *out_val = PD_NONE;
        return true;
    }
    char *end = NULL;
    long v = strtol(p, &end, 10);
    if (end == p) return false;
    if (is_null) *is_null = false;
    if (out_val) *out_val = (int)v;
    return true;
}

/* Extract a balanced `{...}` object starting after `"key":` */
static bool extract_json_object(const char *json, const char *key, char *out, size_t out_size) {
    if (!json || !key || !out || out_size == 0) return false;
    char pat[64];
    int plen = snprintf(pat, sizeof(pat), "\"%s\"", key);
    if (plen <= 0 || (size_t)plen >= sizeof(pat)) return false;

    const char *p = json;
    while ((p = strstr(p, pat)) != NULL) {
        p += plen;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != ':') continue;
        p++;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != '{') return false;

        const char *start = p;
        int depth = 0;
        bool in_str = false;
        while (*p) {
            if (*p == '"' && (p == start || *(p - 1) != '\\')) {
                in_str = !in_str;
            } else if (!in_str) {
                if (*p == '{') depth++;
                else if (*p == '}') {
                    depth--;
                    if (depth == 0) {
                        size_t len = (size_t)(p - start + 1);
                        if (len >= out_size) return false;
                        memcpy(out, start, len);
                        out[len] = '\0';
                        return true;
                    }
                }
            }
            p++;
        }
        return false;
    }
    return false;
}

static void parse_players_array(pd_net_ctx_t *ctx, const char *json) {
    const char *p = strstr(json, "\"players\"");
    if (!p) return;
    p = strchr(p, '[');
    if (!p) return;
    const char *end = strchr(p, ']');
    if (!end) return;

    const char *cur = p;
    while ((cur = strchr(cur, '{')) != NULL && cur < end) {
        const char *obj_end = strchr(cur, '}');
        if (!obj_end || obj_end > end) break;
        char obj[256];
        size_t len = (size_t)(obj_end - cur + 1);
        if (len >= sizeof(obj)) len = sizeof(obj) - 1;
        memcpy(obj, cur, len);
        obj[len] = '\0';

        int seat = -1;
        bool seat_null = false;
        char uname[32] = {0};
        if (extract_json_int(obj, "seat", &seat, &seat_null) && !seat_null &&
            extract_json_string(obj, "username", uname, sizeof(uname))) {
            if (ctx->local_name[0] != '\0' && strcmp(uname, ctx->local_name) == 0) {
                ctx->my_seat = seat;
            } else {
                snprintf(ctx->opponent_name, sizeof(ctx->opponent_name), "%s", uname);
            }
        }
        cur = obj_end + 1;
    }
}

static void handle_sio_frame(pd_net_ctx_t *ctx, const char *msg) {
    if (!ctx || !msg || msg[0] == '\0') return;

    /* Engine.IO OPEN */
    if (msg[0] == '0') {
        ws_send_text_frame(ctx, "40");
        return;
    }

    /* Engine.IO PING -> PONG */
    if (msg[0] == '2') {
        char pong[64];
        snprintf(pong, sizeof(pong), "3%.50s", msg + 1);
        ws_send_text_frame(ctx, pong);
        return;
    }

    /* Socket.IO CONNECT ACK */
    if (strncmp(msg, "40", 2) == 0) {
        ctx->sio_connected = true;
        if (ctx->game_id[0] != '\0') {
            char join_pkt[160];
            snprintf(join_pkt, sizeof(join_pkt), "42[\"game:join\",\"%s\"]", ctx->game_id);
            ws_send_text_frame(ctx, join_pkt);
        }
        return;
    }

    /* Socket.IO EVENT: 42["event", payload] */
    if (strncmp(msg, "42[", 3) == 0) {
        const char *q1 = strchr(msg + 3, '"');
        if (!q1) return;
        const char *q2 = strchr(q1 + 1, '"');
        if (!q2) return;

        char event[64];
        size_t elen = (size_t)(q2 - (q1 + 1));
        if (elen >= sizeof(event)) elen = sizeof(event) - 1;
        memcpy(event, q1 + 1, elen);
        event[elen] = '\0';

        const char *payload = q2 + 1;
        while (*payload && (*payload == ',' || isspace((unsigned char)*payload))) payload++;

        if (strcmp(event, "game:state") == 0) {
            if (!extract_top_level_json_string(payload, "id", ctx->game_id, sizeof(ctx->game_id))) {
                extract_json_string(payload, "id", ctx->game_id, sizeof(ctx->game_id));
            }
            if (!extract_top_level_json_string(payload, "inviteCode", ctx->invite_code, sizeof(ctx->invite_code))) {
                extract_json_string(payload, "inviteCode", ctx->invite_code, sizeof(ctx->invite_code));
            }
            parse_players_array(ctx, payload);

            char state_obj[2048];
            if (extract_json_object(payload, "state", state_obj, sizeof(state_obj))) {
                if (pd_net_parse_state_json(state_obj, &ctx->game_state)) {
                    ctx->variant = ctx->game_state.variant;
                }
            }

            /* Parse room status outside nested "state" object */
            char room_status[32] = {0};
            if (!extract_top_level_json_string(payload, "status", room_status, sizeof(room_status))) {
                extract_json_string(payload, "status", room_status, sizeof(room_status));
            }
            if (strcmp(room_status, "lobby") == 0) {
                ctx->state = PD_NET_STATE_IN_LOBBY;
            } else if (strcmp(room_status, "active") == 0) {
                ctx->state = PD_NET_STATE_ACTIVE;
            } else if (strcmp(room_status, "finished") == 0) {
                ctx->state = PD_NET_STATE_FINISHED;
                if (ctx->game_state.status == PD_STATUS_PLAYING) {
                    int wseat = PD_NONE;
                    bool wnull = true;
                    bool has_wseat = extract_top_level_json_int(payload, "winnerSeat", &wseat, &wnull);
                    if (!has_wseat) {
                        has_wseat = extract_json_int(payload, "winnerSeat", &wseat, &wnull);
                    }
                    if (has_wseat && !wnull && (wseat == 0 || wseat == 1)) {
                        ctx->game_state.status = PD_STATUS_WIN;
                        ctx->game_state.winner = wseat;
                    } else {
                        ctx->game_state.status = PD_STATUS_DRAW;
                        ctx->game_state.winner = PD_NONE;
                    }
                    ctx->game_state.piece_in_hand = PD_NONE;
                }
            }

            ctx->preview_cell = PD_NONE;
            ctx->preview_piece = PD_NONE;
            ctx->state_updated = true;
            return;
        }

        if (strcmp(event, "game:preview") == 0) {
            int cell = PD_NONE;
            bool cell_null = false;
            int piece = PD_NONE;
            bool piece_null = false;
            extract_json_int(payload, "cell", &cell, &cell_null);
            extract_json_int(payload, "piece", &piece, &piece_null);
            ctx->preview_cell = cell_null ? PD_NONE : cell;
            ctx->preview_piece = piece_null ? PD_NONE : piece;
            return;
        }

        if (strcmp(event, "game:rematch-offer") == 0) {
            int seat = -1;
            bool snull = false;
            if (extract_json_int(payload, "seat", &seat, &snull) && !snull) {
                if (seat == ctx->my_seat) ctx->rematch_offered_by_me = true;
                else ctx->rematch_offered_by_peer = true;
            }
            return;
        }

        if (strcmp(event, "game:rematch-ready") == 0) {
            char next_id[64] = {0};
            if (extract_json_string(payload, "gameId", next_id, sizeof(next_id)) && next_id[0]) {
                snprintf(ctx->game_id, sizeof(ctx->game_id), "%s", next_id);
                ctx->rematch_offered_by_me = false;
                ctx->rematch_offered_by_peer = false;
                ctx->draw_offered_by_me = false;
                ctx->draw_offered_by_peer = false;
                ctx->preview_cell = PD_NONE;
                ctx->preview_piece = PD_NONE;
                char join_pkt[160];
                snprintf(join_pkt, sizeof(join_pkt), "42[\"game:join\",\"%s\"]", ctx->game_id);
                ws_send_text_frame(ctx, join_pkt);
            }
            return;
        }

        if (strcmp(event, "game:draw-offered") == 0) {
            int seat = -1;
            bool snull = false;
            if (extract_json_int(payload, "seat", &seat, &snull) && !snull) {
                if (seat == ctx->my_seat) ctx->draw_offered_by_me = true;
                else ctx->draw_offered_by_peer = true;
            }
            return;
        }

        if (strcmp(event, "game:draw-declined") == 0) {
            ctx->draw_offered_by_me = false;
            ctx->draw_offered_by_peer = false;
            return;
        }

        if (strcmp(event, "chat:message") == 0) {
            extract_json_string(payload, "username", ctx->last_chat_sender, sizeof(ctx->last_chat_sender));
            extract_json_string(payload, "text", ctx->last_chat, sizeof(ctx->last_chat));
            return;
        }

        if (strcmp(event, "game:error") == 0) {
            if (!extract_json_string(payload, "message", ctx->error_msg, sizeof(ctx->error_msg))) {
                extract_json_string(payload, "code", ctx->error_msg, sizeof(ctx->error_msg));
            }
            return;
        }
    }
}

static bool process_ws_rx_buffer(pd_net_ctx_t *ctx) {
    bool handled = false;
    while (ctx->rx_len >= 2) {
        const uint8_t *buf = (const uint8_t *)ctx->rx_buf;
        uint8_t opcode = buf[0] & 0x0F;
        bool masked = (buf[1] & 0x80) != 0;
        size_t payload_len = buf[1] & 0x7F;
        size_t hdr_len = 2;

        if (payload_len == 126) {
            if (ctx->rx_len < 4) break;
            payload_len = ((size_t)buf[2] << 8) | (size_t)buf[3];
            hdr_len = 4;
        } else if (payload_len == 127) {
            if (ctx->rx_len < 10) break;
            if (buf[2] != 0 || buf[3] != 0 || buf[4] != 0 || buf[5] != 0) {
                ctx->rx_len = 0;
                snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Oversized WS frame");
                break;
            }
            payload_len = ((size_t)buf[6] << 24) | ((size_t)buf[7] << 16) |
                          ((size_t)buf[8] << 8) | (size_t)buf[9];
            hdr_len = 10;
        }

        size_t full_hdr_len = hdr_len + (masked ? 4u : 0u);
        if (payload_len > sizeof(ctx->rx_buf) - 16 || full_hdr_len + payload_len > sizeof(ctx->rx_buf)) {
            ctx->rx_len = 0;
            snprintf(ctx->error_msg, sizeof(ctx->error_msg), "Oversized WS frame");
            break;
        }

        uint8_t mask_key[4] = {0, 0, 0, 0};
        if (masked) {
            if (ctx->rx_len < hdr_len + 4) break;
            memcpy(mask_key, buf + hdr_len, 4);
            hdr_len += 4;
        }

        if (ctx->rx_len < hdr_len + payload_len) break;

        char msg[8192];
        size_t copy_len = (payload_len < sizeof(msg) - 1) ? payload_len : (sizeof(msg) - 1);
        for (size_t i = 0; i < copy_len; i++) {
            uint8_t b = buf[hdr_len + i];
            msg[i] = (char)(masked ? (b ^ mask_key[i & 3]) : b);
        }
        msg[copy_len] = '\0';

        size_t frame_total = hdr_len + payload_len;
        size_t rem = ctx->rx_len - frame_total;
        if (rem > 0) {
            memmove(ctx->rx_buf, ctx->rx_buf + frame_total, rem);
        }
        ctx->rx_len = rem;

        if (opcode == 0x1) { /* Text frame */
            handle_sio_frame(ctx, msg);
            handled = true;
        } else if (opcode == 0x9) { /* Ping frame -> reply with masked Pong (0x8A) */
            ws_send_frame(ctx, 0xA, msg, copy_len);
            handled = true;
        } else if (opcode == 0x8) { /* Close frame */
            close(ctx->peer_fd);
            ctx->peer_fd = -1;
            ctx->ws_handshake_done = false;
            ctx->sio_connected = false;
            break;
        }
    }
    return handled;
}

bool pd_net_web_poll(pd_net_ctx_t *ctx);

bool pd_net_web_connect_socket(pd_net_ctx_t *ctx, const char *game_id) {
    if (!ctx) return false;
    ctx->mode = PD_NET_MODE_WEB;
    if (game_id && game_id[0] != '\0' && game_id != ctx->game_id) {
        snprintf(ctx->game_id, sizeof(ctx->game_id), "%s", game_id);
    }
    pd_net_web_close(ctx);

    int fd = connect_tcp_blocking(ctx->server_host, ctx->server_port);
    if (fd < 0) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "WebSocket connect failed");
        return false;
    }

    char upgrade_req[1024];
    int req_len = snprintf(
        upgrade_req, sizeof(upgrade_req),
        "GET /socket.io/?EIO=4&transport=websocket HTTP/1.1\r\n"
        "Host: %s:%u\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "%s%s%s"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        ctx->server_host, (unsigned)ctx->server_port,
        (ctx->auth_cookie[0] != '\0') ? "Cookie: " : "",
        (ctx->auth_cookie[0] != '\0') ? ctx->auth_cookie : "",
        (ctx->auth_cookie[0] != '\0') ? "\r\n" : "");

    if (!send_all_blocking(fd, upgrade_req, (size_t)req_len)) {
        close(fd);
        return false;
    }

    char hdr_buf[2048];
    size_t hdr_len = 0;
    char *hdr_end = NULL;
    while (hdr_len < sizeof(hdr_buf) - 1) {
        ssize_t n = recv(fd, hdr_buf + hdr_len, sizeof(hdr_buf) - 1 - hdr_len, 0);
        if (n > 0) {
            hdr_len += (size_t)n;
            hdr_buf[hdr_len] = '\0';
            hdr_end = strstr(hdr_buf, "\r\n\r\n");
            if (hdr_end) break;
        } else if (n == 0) {
            break;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLIN;
            pfd.revents = 0;
            if (poll(&pfd, 1, PD_NET_IO_TIMEOUT_MS) <= 0) {
                break;
            }
        } else if (errno != EINTR) {
            break;
        }
    }

    if (!hdr_end || strncmp(hdr_buf, "HTTP/1.1 101", 12) != 0) {
        close(fd);
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "WebSocket upgrade rejected");
        return false;
    }

    set_nonblocking_fd(fd);
    ctx->peer_fd = fd;
    ctx->ws_handshake_done = true;
    ctx->sio_connected = false;
    ctx->rx_len = 0;

    /* Preserve any WebSocket frame bytes received immediately after \r\n\r\n */
    size_t consumed = (size_t)((hdr_end + 4) - hdr_buf);
    if (hdr_len > consumed) {
        size_t leftover = hdr_len - consumed;
        memcpy(ctx->rx_buf, hdr_end + 4, leftover);
        ctx->rx_len = leftover;
        process_ws_rx_buffer(ctx);
    }

    pd_net_web_poll(ctx);
    return true;
}

bool pd_net_web_poll(pd_net_ctx_t *ctx) {
    if (!ctx || ctx->peer_fd < 0 || !ctx->ws_handshake_done) return false;
    bool activity = false;

    while (ctx->rx_len < sizeof(ctx->rx_buf)) {
        ssize_t n = recv(ctx->peer_fd, ctx->rx_buf + ctx->rx_len,
                         sizeof(ctx->rx_buf) - ctx->rx_len, 0);
        if (n > 0) {
            ctx->rx_len += (size_t)n;
            activity = true;
        } else if (n == 0) {
            close(ctx->peer_fd);
            ctx->peer_fd = -1;
            ctx->ws_handshake_done = false;
            ctx->sio_connected = false;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) break;
            close(ctx->peer_fd);
            ctx->peer_fd = -1;
            ctx->ws_handshake_done = false;
            ctx->sio_connected = false;
            break;
        }
    }

    if (process_ws_rx_buffer(ctx)) {
        activity = true;
    }
    return activity;
}

bool pd_net_web_send_move(pd_net_ctx_t *ctx, pd_move_t move) {
    if (!ctx || !ctx->sio_connected || ctx->game_id[0] == '\0') return false;
    char move_json[128];
    if (pd_net_format_move_json(move, move_json, sizeof(move_json)) < 0) return false;
    char pkt[256];
    snprintf(pkt, sizeof(pkt), "42[\"game:move\",{\"gameId\":\"%s\",\"move\":%s}]",
             ctx->game_id, move_json);
    return ws_send_text_frame(ctx, pkt);
}

bool pd_net_web_send_preview(pd_net_ctx_t *ctx, int cell) {
    if (!ctx || !ctx->sio_connected || ctx->game_id[0] == '\0') return false;
    char pkt[256];
    if (cell >= 0 && cell < PD_BOARD_SIZE) {
        snprintf(pkt, sizeof(pkt), "42[\"game:preview\",{\"gameId\":\"%s\",\"cell\":%d}]",
                 ctx->game_id, cell);
    } else {
        snprintf(pkt, sizeof(pkt), "42[\"game:preview\",{\"gameId\":\"%s\",\"cell\":null}]",
                 ctx->game_id);
    }
    return ws_send_text_frame(ctx, pkt);
}

bool pd_net_web_send_rematch(pd_net_ctx_t *ctx) {
    if (!ctx || !ctx->sio_connected || ctx->game_id[0] == '\0') return false;
    ctx->rematch_offered_by_me = true;
    char pkt[160];
    snprintf(pkt, sizeof(pkt), "42[\"game:rematch\",\"%s\"]", ctx->game_id);
    return ws_send_text_frame(ctx, pkt);
}

bool pd_net_web_send_resign(pd_net_ctx_t *ctx) {
    if (!ctx || !ctx->sio_connected || ctx->game_id[0] == '\0') return false;
    char pkt[160];
    snprintf(pkt, sizeof(pkt), "42[\"game:resign\",\"%s\"]", ctx->game_id);
    return ws_send_text_frame(ctx, pkt);
}

bool pd_net_web_send_draw_offer(pd_net_ctx_t *ctx) {
    if (!ctx || !ctx->sio_connected || ctx->game_id[0] == '\0') return false;
    ctx->draw_offered_by_me = true;
    char pkt[160];
    snprintf(pkt, sizeof(pkt), "42[\"game:draw-offer\",\"%s\"]", ctx->game_id);
    return ws_send_text_frame(ctx, pkt);
}

bool pd_net_web_send_draw_respond(pd_net_ctx_t *ctx, bool accept) {
    if (!ctx || !ctx->sio_connected || ctx->game_id[0] == '\0') return false;
    ctx->draw_offered_by_me = false;
    ctx->draw_offered_by_peer = false;
    char pkt[200];
    snprintf(pkt, sizeof(pkt), "42[\"game:draw-respond\",{\"gameId\":\"%s\",\"accept\":%s}]",
             ctx->game_id, accept ? "true" : "false");
    return ws_send_text_frame(ctx, pkt);
}

bool pd_net_web_send_chat(pd_net_ctx_t *ctx, const char *text) {
    if (!ctx || !ctx->sio_connected || ctx->game_id[0] == '\0' || !text || !text[0]) return false;
    char escaped[256];
    size_t j = 0;
    for (size_t i = 0; text[i] && j + 3 < sizeof(escaped); i++) {
        if (text[i] == '"' || text[i] == '\\') {
            escaped[j++] = '\\';
        }
        escaped[j++] = text[i];
    }
    escaped[j] = '\0';

    char pkt[400];
    snprintf(pkt, sizeof(pkt), "42[\"chat:send\",{\"gameId\":\"%s\",\"text\":\"%s\"}]",
             ctx->game_id, escaped);
    return ws_send_text_frame(ctx, pkt);
}
