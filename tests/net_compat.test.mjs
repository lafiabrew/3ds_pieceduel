#!/usr/bin/env node
/**
 * Automated Network Compatibility Test Suite for 3ds_pieceduel:
 * 1. JSON Move & GameState serialization/deserialization parity with @pieceduel/engine
 * 2. LAN P2P UDP Beacon Discovery + TCP Lockstep Match (moves, preview, chat, draw, resign, rematch)
 * 3. 3DS UDS Local Wireless Loopback Simulation (PD_SIMULATE_UDS=1)
 * 4. Web Server Cross-Play Client (Auth.js CSRF + Credentials login, REST create/join,
 *    RFC 6455 WebSocket upgrade, Engine.IO v4 ping/pong, Socket.IO v4 game:join / game:state /
 *    game:move / game:preview / chat:send / game:draw-offer / game:rematch-ready)
 */

import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { execFileSync, spawn } from "node:child_process";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const BUILD_DIR = path.join(ROOT, "build", "host");
const DRIVER_SRC = path.join(BUILD_DIR, "test_net_driver.c");
const DRIVER_BIN = path.join(BUILD_DIR, "test_net_driver");

fs.mkdirSync(BUILD_DIR, { recursive: true });

const C_DRIVER_SOURCE = `
#define _DEFAULT_SOURCE
#include "pd_net.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void poll_both(pd_net_ctx_t *a, pd_net_ctx_t *b, int iterations) {
    for (int i = 0; i < iterations; i++) {
        pd_net_poll(a);
        pd_net_poll(b);
        usleep(2000);
    }
}

static int test_json_roundtrip(void) {
    char buf[1024];
    pd_move_t m_in, m_out;

    /* 1. Open move */
    m_in.kind = PD_MOVE_OPEN;
    m_in.cell = PD_NONE;
    m_in.give = 7;
    assert(pd_net_format_move_json(m_in, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, "{\\"kind\\":\\"open\\",\\"give\\":7}") == 0);
    assert(pd_net_parse_move_json(buf, &m_out));
    assert(m_out.kind == PD_MOVE_OPEN && m_out.cell == PD_NONE && m_out.give == 7);

    /* 2. Place move with give */
    m_in.kind = PD_MOVE_PLACE;
    m_in.cell = 5;
    m_in.give = 12;
    assert(pd_net_format_move_json(m_in, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, "{\\"kind\\":\\"place\\",\\"cell\\":5,\\"give\\":12}") == 0);
    assert(pd_net_parse_move_json(buf, &m_out));
    assert(m_out.kind == PD_MOVE_PLACE && m_out.cell == 5 && m_out.give == 12);

    /* 3. Terminal place move with give: null */
    m_in.kind = PD_MOVE_PLACE;
    m_in.cell = 15;
    m_in.give = PD_NONE;
    assert(pd_net_format_move_json(m_in, buf, sizeof(buf)) > 0);
    assert(strcmp(buf, "{\\"kind\\":\\"place\\",\\"cell\\":15,\\"give\\":null}") == 0);
    assert(pd_net_parse_move_json(buf, &m_out));
    assert(m_out.kind == PD_MOVE_PLACE && m_out.cell == 15 && m_out.give == PD_NONE);

    /* 4. GameState round-trip */
    pd_game_state_t st_in, st_out;
    pd_create_game(&st_in, 0, PD_VARIANT_ADVANCED);
    pd_move_t open_mv = { PD_MOVE_OPEN, PD_NONE, 3 };
    assert(pd_apply_move(&st_in, open_mv, &st_in));
    pd_move_t place_mv = { PD_MOVE_PLACE, 10, 9 };
    assert(pd_apply_move(&st_in, place_mv, &st_in));

    assert(pd_net_format_state_json(&st_in, buf, sizeof(buf)) > 0);
    assert(pd_net_parse_state_json(buf, &st_out));
    assert(st_out.current == st_in.current);
    assert(st_out.piece_in_hand == 9);
    assert(st_out.board[10] == 3);
    assert(st_out.available_count == 14);
    assert(st_out.move_count == 2);
    assert(st_out.variant == PD_VARIANT_ADVANCED);
    assert(st_out.status == PD_STATUS_PLAYING);

    printf("PASS: test_json_roundtrip\\n");
    return 0;
}

static int test_p2p_match(uint16_t udp_port) {
    pd_net_ctx_t host, joiner;
    pd_net_init(&host, "Host3DS");
    pd_net_init(&joiner, "Joiner3DS");

    assert(pd_net_p2p_start_scan(&joiner, udp_port));
    assert(joiner.state == PD_NET_STATE_SCANNING);

    assert(pd_net_p2p_host(&host, udp_port, 0, PD_VARIANT_STANDARD));
    assert(host.state == PD_NET_STATE_HOSTING);
    assert(host.bound_tcp_port > 0);

    assert(pd_net_p2p_send_beacon(&host, "127.0.0.1", udp_port));
    poll_both(&host, &joiner, 10);

    assert(joiner.beacon_count == 1);
    assert(strcmp(joiner.beacons[0].host_name, "Host3DS") == 0);
    assert(joiner.beacons[0].tcp_port == host.bound_tcp_port);
    assert(joiner.beacons[0].variant == PD_VARIANT_STANDARD);

    assert(pd_net_p2p_connect(&joiner, joiner.beacons[0].ip_addr, joiner.beacons[0].tcp_port));
    poll_both(&host, &joiner, 15);

    assert(host.state == PD_NET_STATE_ACTIVE);
    assert(joiner.state == PD_NET_STATE_ACTIVE);
    assert(strcmp(host.opponent_name, "Joiner3DS") == 0);
    assert(strcmp(joiner.opponent_name, "Host3DS") == 0);

    /* Chat exchange */
    assert(pd_net_send_chat(&host, "gl hf!"));
    poll_both(&host, &joiner, 10);
    assert(strcmp(joiner.last_chat_sender, "Host3DS") == 0);
    assert(strcmp(joiner.last_chat, "gl hf!") == 0);

    /* Draw offer & decline */
    assert(pd_net_send_draw_offer(&host));
    poll_both(&host, &joiner, 10);
    assert(joiner.draw_offered_by_peer == true);
    assert(pd_net_send_draw_respond(&joiner, false));
    poll_both(&host, &joiner, 10);
    assert(host.draw_offered_by_me == false);
    assert(joiner.draw_offered_by_peer == false);

    /* Play a 4-in-a-row win on row 0 (cells 0, 1, 2, 3) with pieces 0, 1, 2, 3 (all < 8, share bit 3 == 0) */
    /* Ply 0 (Host, seat 0): open give=0 */
    assert(pd_net_send_move(&host, (pd_move_t){ PD_MOVE_OPEN, PD_NONE, 0 }));
    poll_both(&host, &joiner, 10);
    assert(joiner.game_state.piece_in_hand == 0);
    assert(joiner.game_state.current == 1);

    /* Joiner sends placement preview on cell 0 before committing */
    assert(pd_net_send_preview(&joiner, 0));
    poll_both(&host, &joiner, 10);
    assert(host.preview_cell == 0);
    assert(host.preview_piece == 0);

    /* Ply 1 (Joiner, seat 1): place cell=0, give=1 */
    assert(pd_net_send_move(&joiner, (pd_move_t){ PD_MOVE_PLACE, 0, 1 }));
    poll_both(&host, &joiner, 10);
    assert(host.preview_cell == PD_NONE);
    assert(host.game_state.board[0] == 0);
    assert(host.game_state.piece_in_hand == 1);
    assert(host.game_state.current == 0);

    /* Ply 2 (Host, seat 0): place cell=1, give=2 */
    assert(pd_net_send_move(&host, (pd_move_t){ PD_MOVE_PLACE, 1, 2 }));
    poll_both(&host, &joiner, 10);

    /* Ply 3 (Joiner, seat 1): place cell=2, give=3 */
    assert(pd_net_send_move(&joiner, (pd_move_t){ PD_MOVE_PLACE, 2, 3 }));
    poll_both(&host, &joiner, 10);

    /* Ply 4 (Host, seat 0): winning placement at cell=3, give=PD_NONE */
    assert(pd_net_send_move(&host, (pd_move_t){ PD_MOVE_PLACE, 3, PD_NONE }));
    poll_both(&host, &joiner, 10);

    assert(host.state == PD_NET_STATE_FINISHED);
    assert(joiner.state == PD_NET_STATE_FINISHED);
    assert(host.game_state.status == PD_STATUS_WIN);
    assert(joiner.game_state.status == PD_STATUS_WIN);
    assert(host.game_state.winner == 0);
    assert(joiner.game_state.winner == 0);
    assert(joiner.game_state.has_winning_line == true);

    /* Both players offer rematch */
    assert(pd_net_send_rematch(&host));
    poll_both(&host, &joiner, 10);
    assert(joiner.rematch_offered_by_peer == true);
    assert(pd_net_send_rematch(&joiner));
    poll_both(&host, &joiner, 10);

    assert(host.state == PD_NET_STATE_ACTIVE);
    assert(joiner.state == PD_NET_STATE_ACTIVE);
    assert(host.game_state.status == PD_STATUS_PLAYING);
    assert(joiner.game_state.status == PD_STATUS_PLAYING);
    assert(host.game_state.move_count == 0);
    assert(joiner.game_state.move_count == 0);

    /* Test resign in game 2 */
    assert(pd_net_send_resign(&joiner));
    poll_both(&host, &joiner, 10);
    assert(host.state == PD_NET_STATE_FINISHED);
    assert(joiner.state == PD_NET_STATE_FINISHED);
    assert(host.game_state.status == PD_STATUS_WIN);
    assert(host.game_state.winner == 0);

    pd_net_close(&host);
    pd_net_close(&joiner);
    printf("PASS: test_p2p_match\\n");
    return 0;
}

static int test_uds_simulation(void) {
    setenv("PD_SIMULATE_UDS", "1", 1);
    assert(pd_net_uds_available() == true);

    pd_net_ctx_t host, joiner;
    pd_net_init(&host, "UDS_Host");
    pd_net_init(&joiner, "UDS_Client");

    assert(pd_net_uds_scan(&joiner));
    assert(pd_net_uds_host(&host, PD_VARIANT_MISERE));
    assert(pd_net_uds_scan(&joiner));
    assert(joiner.beacon_count >= 1);
    assert(joiner.beacons[0].is_uds == true);
    assert(joiner.beacons[0].variant == PD_VARIANT_MISERE);

    assert(pd_net_uds_connect(&joiner, 0));
    poll_both(&host, &joiner, 15);
    assert(host.state == PD_NET_STATE_ACTIVE);
    assert(joiner.state == PD_NET_STATE_ACTIVE);

    assert(pd_net_send_move(&host, (pd_move_t){ PD_MOVE_OPEN, PD_NONE, 11 }));
    poll_both(&host, &joiner, 10);
    assert(joiner.game_state.piece_in_hand == 11);

    pd_net_close(&host);
    pd_net_close(&joiner);
    unsetenv("PD_SIMULATE_UDS");
    printf("PASS: test_uds_simulation\\n");
    return 0;
}

static int test_web_crossplay(uint16_t port) {
    pd_net_ctx_t p0, p1;
    pd_net_init(&p0, "alice");
    pd_net_init(&p1, "bob");

    pd_net_web_configure(&p0, "127.0.0.1", port, NULL);
    pd_net_web_configure(&p1, "127.0.0.1", port, NULL);

    /* 1. Login via /api/auth/csrf + /api/auth/callback/credentials */
    assert(pd_net_web_login(&p0, "alice", "secret123"));
    assert(strstr(p0.auth_cookie, "authjs.session-token=") != NULL);

    assert(pd_net_web_login(&p1, "bob", "secret456"));
    assert(strstr(p1.auth_cookie, "authjs.session-token=") != NULL);

    /* 2. Alice creates a PvP game */
    assert(pd_net_web_create_game(&p0, "pvp", NULL));
    assert(strlen(p0.game_id) > 0);
    assert(strlen(p0.invite_code) > 0);
    assert(p0.state == PD_NET_STATE_IN_LOBBY);

    /* 3. Alice connects Socket.IO WebSocket */
    assert(pd_net_web_connect_socket(&p0, p0.game_id));

    /* 4. Bob joins Alice's game using the inviteCode and connects Socket.IO */
    assert(pd_net_web_join_game(&p1, p0.invite_code));
    assert(strcmp(p1.game_id, p0.game_id) == 0);
    assert(pd_net_web_connect_socket(&p1, p1.game_id));

    poll_both(&p0, &p1, 20);
    assert(p0.sio_connected == true);
    assert(p1.sio_connected == true);
    assert(p0.state == PD_NET_STATE_ACTIVE);
    assert(p1.state == PD_NET_STATE_ACTIVE);
    assert(p0.my_seat == 0);
    assert(p1.my_seat == 1);
    assert(strcmp(p0.opponent_name, "bob") == 0);
    assert(strcmp(p1.opponent_name, "alice") == 0);

    /* 5. Chat over Socket.IO */
    assert(pd_net_send_chat(&p0, "Hello from 3DS!"));
    poll_both(&p0, &p1, 15);
    assert(strcmp(p1.last_chat_sender, "alice") == 0);
    assert(strcmp(p1.last_chat, "Hello from 3DS!") == 0);

    /* 6. Draw offer & decline over Socket.IO */
    assert(pd_net_send_draw_offer(&p0));
    poll_both(&p0, &p1, 15);
    assert(p1.draw_offered_by_peer == true);
    assert(pd_net_send_draw_respond(&p1, false));
    poll_both(&p0, &p1, 15);
    assert(p0.draw_offered_by_me == false);
    assert(p1.draw_offered_by_peer == false);

    /* 7. Play moves to a win */
    /* Ply 0 (Alice, seat 0): open give=0 */
    assert(pd_net_send_move(&p0, (pd_move_t){ PD_MOVE_OPEN, PD_NONE, 0 }));
    poll_both(&p0, &p1, 15);
    assert(p1.game_state.piece_in_hand == 0);
    assert(p1.game_state.current == 1);

    /* Bob previews cell 0 */
    assert(pd_net_send_preview(&p1, 0));
    poll_both(&p0, &p1, 15);
    assert(p0.preview_cell == 0);
    assert(p0.preview_piece == 0);

    /* Ply 1 (Bob, seat 1): place cell=0, give=1 */
    assert(pd_net_send_move(&p1, (pd_move_t){ PD_MOVE_PLACE, 0, 1 }));
    poll_both(&p0, &p1, 15);
    assert(p0.game_state.board[0] == 0);
    assert(p0.game_state.piece_in_hand == 1);

    /* Ply 2 (Alice, seat 0): place cell=1, give=2 */
    assert(pd_net_send_move(&p0, (pd_move_t){ PD_MOVE_PLACE, 1, 2 }));
    poll_both(&p0, &p1, 15);

    /* Ply 3 (Bob, seat 1): place cell=2, give=3 */
    assert(pd_net_send_move(&p1, (pd_move_t){ PD_MOVE_PLACE, 2, 3 }));
    poll_both(&p0, &p1, 15);

    /* Ply 4 (Alice, seat 0): winning placement cell=3, give=null */
    assert(pd_net_send_move(&p0, (pd_move_t){ PD_MOVE_PLACE, 3, PD_NONE }));
    poll_both(&p0, &p1, 15);

    assert(p0.state == PD_NET_STATE_FINISHED);
    assert(p1.state == PD_NET_STATE_FINISHED);
    assert(p0.game_state.status == PD_STATUS_WIN);
    assert(p1.game_state.status == PD_STATUS_WIN);
    assert(p0.game_state.winner == 0);
    assert(p1.game_state.winner == 0);

    /* 8. Rematch flow: both send game:rematch -> receive game:rematch-ready -> auto-join new game */
    char old_game_id[64];
    snprintf(old_game_id, sizeof(old_game_id), "%s", p0.game_id);

    assert(pd_net_send_rematch(&p0));
    poll_both(&p0, &p1, 15);
    assert(p1.rematch_offered_by_peer == true);

    assert(pd_net_send_rematch(&p1));
    poll_both(&p0, &p1, 25);

    assert(strcmp(p0.game_id, old_game_id) != 0);
    assert(strcmp(p0.game_id, p1.game_id) == 0);
    assert(p0.state == PD_NET_STATE_ACTIVE);
    assert(p1.state == PD_NET_STATE_ACTIVE);
    assert(p0.game_state.status == PD_STATUS_PLAYING);
    assert(p0.game_state.move_count == 0);

    pd_net_close(&p0);
    pd_net_close(&p1);
    printf("PASS: test_web_crossplay\\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *mode = argv[1];
    if (strcmp(mode, "local") == 0) {
        uint16_t udp_port = (argc >= 3) ? (uint16_t)atoi(argv[2]) : 43299;
        if (test_json_roundtrip() != 0) return 1;
        if (test_p2p_match(udp_port) != 0) return 1;
        if (test_uds_simulation() != 0) return 1;
        return 0;
    }
    if (strcmp(mode, "web") == 0 && argc >= 3) {
        uint16_t port = (uint16_t)atoi(argv[2]);
        return test_web_crossplay(port);
    }
    return 1;
}
`;

fs.writeFileSync(DRIVER_SRC, C_DRIVER_SOURCE, "utf8");

execFileSync(
  "gcc",
  [
    "-std=c11",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-I",
    path.join(ROOT, "include"),
    DRIVER_SRC,
    path.join(ROOT, "src/net/net_common.c"),
    path.join(ROOT, "src/net/net_p2p.c"),
    path.join(ROOT, "src/net/net_uds.c"),
    path.join(ROOT, "src/net/net_web.c"),
    "-o",
    DRIVER_BIN,
  ],
  { stdio: "inherit" }
);

/* --- Minimal in-process Piece Duel Engine + Socket.IO v4 / Engine.IO v4 Server --- */

const LINES = [
  [0, 1, 2, 3],
  [4, 5, 6, 7],
  [8, 9, 10, 11],
  [12, 13, 14, 15],
  [0, 4, 8, 12],
  [1, 5, 9, 13],
  [2, 6, 10, 14],
  [3, 7, 11, 15],
  [0, 5, 10, 15],
  [3, 6, 9, 12],
];

function createGame(starting = 0, variant = "standard") {
  return {
    board: Array(16).fill(null),
    current: starting,
    pieceInHand: null,
    available: Array.from({ length: 16 }, (_, i) => i),
    status: "playing",
    winner: null,
    winningLine: null,
    moveCount: 0,
    variant,
  };
}

function findWinningLine(board) {
  for (const line of LINES) {
    const [a, b, c, d] = line;
    const pa = board[a], pb = board[b], pc = board[c], pd = board[d];
    if (pa === null || pb === null || pc === null || pd === null) continue;
    if ((pa & pb & pc & pd) !== 0 || (~pa & ~pb & ~pc & ~pd & 0xf) !== 0) {
      return line;
    }
  }
  return null;
}

function applyMove(state, move) {
  if (state.pieceInHand === null) {
    assert.equal(move.kind, "open");
    return {
      ...state,
      pieceInHand: move.give,
      available: state.available.filter((p) => p !== move.give),
      current: state.current === 0 ? 1 : 0,
      moveCount: state.moveCount + 1,
    };
  }
  assert.equal(move.kind, "place");
  const board = state.board.slice();
  board[move.cell] = state.pieceInHand;
  const winningLine = findWinningLine(board);
  if (winningLine) {
    return {
      ...state,
      board,
      pieceInHand: null,
      status: "win",
      winner: state.current,
      winningLine,
      moveCount: state.moveCount + 1,
    };
  }
  return {
    ...state,
    board,
    pieceInHand: move.give,
    available: state.available.filter((p) => p !== move.give),
    current: state.current === 0 ? 1 : 0,
    moveCount: state.moveCount + 1,
  };
}

function encodeWsTextFrame(str) {
  const payload = Buffer.from(str, "utf8");
  const len = payload.length;
  let header;
  if (len < 126) {
    header = Buffer.from([0x81, len]);
  } else {
    header = Buffer.from([0x81, 126, (len >> 8) & 0xff, len & 0xff]);
  }
  return Buffer.concat([header, payload]);
}

function parseWsFrames(buffer, onTextFrame) {
  let offset = 0;
  while (offset + 2 <= buffer.length) {
    const b0 = buffer[offset];
    const b1 = buffer[offset + 1];
    const opcode = b0 & 0x0f;
    const masked = (b1 & 0x80) !== 0;
    let payloadLen = b1 & 0x7f;
    let hdrLen = 2;
    if (payloadLen === 126) {
      if (offset + 4 > buffer.length) break;
      payloadLen = (buffer[offset + 2] << 8) | buffer[offset + 3];
      hdrLen = 4;
    }
    const maskOffset = hdrLen;
    if (masked) hdrLen += 4;
    if (offset + hdrLen + payloadLen > buffer.length) break;

    const payload = Buffer.alloc(payloadLen);
    for (let i = 0; i < payloadLen; i++) {
      const byte = buffer[offset + hdrLen + i];
      payload[i] = masked ? byte ^ buffer[offset + maskOffset + (i & 3)] : byte;
    }
    offset += hdrLen + payloadLen;
    if (opcode === 0x1) {
      onTextFrame(payload.toString("utf8"));
    }
  }
  return buffer.subarray(offset);
}

async function runTests() {
  /* 1. Run local JSON, P2P, and simulated UDS tests */
  const localOut = execFileSync(DRIVER_BIN, ["local", "43298"], { encoding: "utf8" });
  process.stdout.write(localOut);
  assert(localOut.includes("PASS: test_json_roundtrip"));
  assert(localOut.includes("PASS: test_p2p_match"));
  assert(localOut.includes("PASS: test_uds_simulation"));

  /* 2. Start mock Piece Duel Web + Socket.IO v4 Server and run Web Cross-Play test */
  const sessions = new Map();
  const games = new Map();
  const rooms = new Map(); // gameId -> Set<socket>
  const rematchOffers = new Map();
  let pongReceivedCount = 0;

  function serializeGame(game) {
    return {
      id: game.id,
      status: game.status,
      mode: game.mode,
      inviteCode: game.inviteCode,
      ranked: true,
      directed: false,
      winnerSeat: game.winnerSeat,
      players: game.players,
      state: game.state,
      aiLevel: null,
      lastCell: null,
    };
  }

  function broadcastRoom(gameId, event, data, excludeSocket = null) {
    const members = rooms.get(gameId);
    if (!members) return;
    const frame = encodeWsTextFrame(`42${JSON.stringify([event, data])}`);
    for (const sock of members) {
      if (sock !== excludeSocket && !sock.destroyed) {
        sock.write(frame);
      }
    }
  }

  const server = http.createServer((req, res) => {
    let body = "";
    req.on("data", (chunk) => {
      body += chunk.toString("utf8");
    });
    req.on("end", () => {
      if (req.method === "GET" && req.url === "/api/auth/csrf") {
        res.setHeader("Set-Cookie", "authjs.csrf-token=csrf123%7C" + "a".repeat(64) + "; Path=/; HttpOnly");
        res.setHeader("Content-Type", "application/json");
        res.end(JSON.stringify({ csrfToken: "csrf123" }));
        return;
      }

      if (req.method === "POST" && req.url === "/api/auth/callback/credentials") {
        const params = new URLSearchParams(body);
        const identifier = params.get("identifier");
        const csrfToken = params.get("csrfToken");
        assert.equal(csrfToken, "csrf123");
        assert(identifier === "alice" || identifier === "bob");
        const token = `sess_${identifier}`;
        sessions.set(token, { userId: `uid_${identifier}`, username: identifier });
        res.statusCode = 302;
        res.setHeader("Set-Cookie", `authjs.session-token=${token}; Path=/; HttpOnly`);
        res.end();
        return;
      }

      const cookieHdr = req.headers.cookie || "";
      const match = cookieHdr.match(/authjs\.session-token=([^;\s]+)/);
      const user = match ? sessions.get(match[1]) : null;
      if (!user) {
        res.statusCode = 401;
        res.end(JSON.stringify({ error: "unauthorized" }));
        return;
      }

      if (req.method === "POST" && req.url === "/api/games") {
        const game = {
          id: "game-web-1",
          status: "lobby",
          mode: "pvp",
          inviteCode: "PD3DS1",
          winnerSeat: null,
          players: [{ seat: 0, userId: user.userId, username: user.username }],
          state: createGame(0, "standard"),
        };
        games.set(game.id, game);
        res.statusCode = 201;
        res.setHeader("Content-Type", "application/json");
        res.end(JSON.stringify(serializeGame(game)));
        return;
      }

      if (req.method === "POST" && req.url === "/api/games/join") {
        const parsed = JSON.parse(body);
        const found = [...games.values()].find(
          (g) => g.id === parsed.idOrCode || g.inviteCode === parsed.idOrCode
        );
        assert(found, "Game should be found by invite code");
        found.players.push({ seat: 1, userId: user.userId, username: user.username });
        found.status = "active";
        res.statusCode = 200;
        res.setHeader("Content-Type", "application/json");
        res.end(JSON.stringify(serializeGame(found)));
        return;
      }

      res.statusCode = 404;
      res.end();
    });
  });

  server.on("upgrade", (req, socket) => {
    socket.on("error", () => {});
    assert(req.url.startsWith("/socket.io/?EIO=4&transport=websocket"));
    const cookieHdr = req.headers.cookie || "";
    const match = cookieHdr.match(/authjs\.session-token=([^;\s]+)/);
    const user = match ? sessions.get(match[1]) : null;
    assert(user, "WebSocket upgrade must include session cookie");

    const key = req.headers["sec-websocket-key"];
    const accept = crypto
      .createHash("sha1")
      .update(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")
      .digest("base64");

    socket.write(
      "HTTP/1.1 101 Switching Protocols\r\n" +
        "Upgrade: websocket\r\n" +
        "Connection: Upgrade\r\n" +
        `Sec-WebSocket-Accept: ${accept}\r\n` +
        "\r\n"
    );

    /* Send Engine.IO v4 open packet + ping */
    socket.write(
      encodeWsTextFrame(
        JSON.stringify({
          sid: `sid_${user.username}`,
          upgrades: [],
          pingInterval: 25000,
          pingTimeout: 20000,
        }).replace(/^/, "0")
      )
    );
    socket.write(encodeWsTextFrame("2"));

    let rxBuf = Buffer.alloc(0);
    socket.on("data", (chunk) => {
      rxBuf = Buffer.concat([rxBuf, chunk]);
      rxBuf = parseWsFrames(rxBuf, (msg) => {
        if (msg === "3") {
          pongReceivedCount++;
          return;
        }
        if (msg === "40") {
          socket.write(encodeWsTextFrame(`40{"sid":"sio_${user.username}"}`));
          return;
        }
        if (msg.startsWith("42[")) {
          const [event, payload] = JSON.parse(msg.slice(2));
          if (event === "game:join") {
            const gameId = payload;
            if (!rooms.has(gameId)) rooms.set(gameId, new Set());
            rooms.get(gameId).add(socket);
            const game = games.get(gameId);
            if (game) {
              broadcastRoom(gameId, "game:state", serializeGame(game));
            }
          } else if (event === "chat:send") {
            const game = games.get(payload.gameId);
            const me = game.players.find((p) => p.userId === user.userId);
            broadcastRoom(payload.gameId, "chat:message", {
              seat: me.seat,
              username: me.username,
              text: payload.text,
              ts: Date.now(),
            });
          } else if (event === "game:draw-offer") {
            const game = games.get(payload);
            const me = game.players.find((p) => p.userId === user.userId);
            broadcastRoom(payload, "game:draw-offered", { seat: me.seat });
          } else if (event === "game:draw-respond") {
            const game = games.get(payload.gameId);
            const me = game.players.find((p) => p.userId === user.userId);
            if (!payload.accept) {
              broadcastRoom(payload.gameId, "game:draw-declined", { seat: me.seat });
            }
          } else if (event === "game:preview") {
            const game = games.get(payload.gameId);
            const me = game.players.find((p) => p.userId === user.userId);
            broadcastRoom(
              payload.gameId,
              "game:preview",
              {
                seat: me.seat,
                cell: payload.cell,
                piece: payload.cell === null ? null : game.state.pieceInHand,
              },
              socket
            );
          } else if (event === "game:move") {
            const game = games.get(payload.gameId);
            game.state = applyMove(game.state, payload.move);
            if (game.state.status !== "playing") {
              game.status = "finished";
              game.winnerSeat = game.state.winner;
            }
            broadcastRoom(payload.gameId, "game:state", serializeGame(game));
          } else if (event === "game:rematch") {
            const gameId = payload;
            const game = games.get(gameId);
            const me = game.players.find((p) => p.userId === user.userId);
            if (!rematchOffers.has(gameId)) rematchOffers.set(gameId, new Set());
            rematchOffers.get(gameId).add(user.userId);
            broadcastRoom(gameId, "game:rematch-offer", { seat: me.seat });
            if (rematchOffers.get(gameId).size === 2) {
              const nextGame = {
                id: "game-web-2",
                status: "active",
                mode: "pvp",
                inviteCode: null,
                winnerSeat: null,
                players: game.players,
                state: createGame(0, "standard"),
              };
              games.set(nextGame.id, nextGame);
              broadcastRoom(gameId, "game:rematch-ready", { gameId: nextGame.id });
            }
          }
        }
      });
    });
  });

  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const port = server.address().port;

  try {
    const webOut = await new Promise((resolve, reject) => {
      const child = spawn(DRIVER_BIN, ["web", String(port)], {
        stdio: ["ignore", "pipe", "pipe"],
      });
      let out = "";
      let err = "";
      child.stdout.on("data", (d) => (out += d.toString("utf8")));
      child.stderr.on("data", (d) => (err += d.toString("utf8")));
      child.on("error", reject);
      child.on("close", (code, sig) => {
        if (code === 0) resolve(out);
        else reject(new Error(`Driver exited code=${code} sig=${sig}: ${err}\n${out}`));
      });
    });
    process.stdout.write(webOut);
    assert(webOut.includes("PASS: test_web_crossplay"));
    assert(pongReceivedCount >= 2, "Both clients should respond to Engine.IO ping (2 -> 3)");
  } finally {
    server.close();
    fs.rmSync(DRIVER_SRC, { force: true });
    fs.rmSync(DRIVER_BIN, { force: true });
  }

  console.log("ALL NET COMPATIBILITY TESTS PASSED!");
}

runTests().catch((err) => {
  console.error(err);
  process.exit(1);
});
