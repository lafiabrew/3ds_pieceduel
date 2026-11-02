# IMPLEMENTATION_PLAN.md — 3ds_pieceduel

## Overview
Native C11 Nintendo 3DS homebrew edition of `pieceduel` (`libctru` + `citro2d`) with stereoscopic 3D top-screen board, bottom-screen stylus touch + button controls, full `@pieceduel/engine` parity, atomic SD persistence, and 3-tier multiplayer (`nwm::UDS`, LAN P2P, and `pieceduel` Web Server Socket.IO v4 cross-play).

## Ruggedness Gate Record
- **Date:** 2026-11-02
- **Scope:** Full `3ds_pieceduel` implementation and build/test pipeline
- **Source:** `/rugged review` (`.wgm/rugged/2026-11-02-3ds-pieceduel-review.md`)
- **Current Verdict:** **RUGGED** (superseded initial `FRAGILE` verdict after completing `T7` remediation: fixed `libctru` UDS/SDMC API calls, added 1,500 ms non-blocking `poll()` TCP/HTTP timeouts in `src/net/net_web.c`, implemented atomic `.tmp`-then-`rename` SD saves in `src/storage/storage.c`, eliminated external `bannertool` glibc mismatch via pure-Python CBMD `.bnr` generation in `meta/generate_assets.py`, and verified `make test && make 3ds && make verify-3ds` exit 0).

---

## Tasks

### T1 Engine Port (`engine-port`)
- **Status:** done
- **Objective:** Port `@pieceduel/engine` to pure C11 (`src/engine/*.c`) with host unit tests (`tests/test_engine.c`) and CLI bridge (`tests/pd_cli.c`).
- **Files/areas:** `include/pd_engine.h`, `src/engine/`, `tests/test_engine.c`, `tests/pd_cli.c`
- **Validation command:** `make build/host/test_engine && ./build/host/test_engine`
- **Evidence:** Exited 0 (`PASS: test_engine all checks succeeded.`).

### T2 Cross-Language Differential Compatibility (`compat-diff-tests`)
- **Status:** done
- **Objective:** Verify 100% bit-exact parity between C11 `pd_cli` and TypeScript `@pieceduel/engine` across Mulberry32 PRNG, FNV-1a `dateSeed`, AI playthroughs (`standard`/`misere`/`advanced`), Daily/Win-in-Two/Safe-Give/Threat-Drill puzzles, and `reviewMove`.
- **Files/areas:** `tests/compat_diff.test.mjs`
- **Validation command:** `make build/host/pd_cli && node tests/compat_diff.test.mjs`
- **Evidence:** Exited 0 (`PASS: All 580 cross-language differential checks matched bit-for-bit!`).

### T3 SD Card Persistence, Replays & Game Modes (`storage-and-modes`)
- **Status:** done
- **Objective:** Implement atomic CRC32-checked SD save data (`src/storage/storage.c`) and full mode state machine (`src/app.c`) covering AI, Pass-and-Play, Daily + Archive, Trainers, Tutorial, Replay Coaching, and Multiplayer Lobby.
- **Files/areas:** `include/pd_storage.h`, `src/storage/storage.c`, `src/app.c`
- **Validation command:** `make test-host`
- **Evidence:** Exited 0 (`test_ui_sim: ALL TESTS PASSED`).

### T4 3DS UDS, LAN P2P & Web Cross-Play (`net-multiplayer`)
- **Status:** done
- **Objective:** Implement 3DS Local Wireless (`src/net/net_uds.c`), Wi-Fi LAN UDP/TCP P2P (`src/net/net_p2p.c`), and `pieceduel` Web Server HTTP + Socket.IO v4 client (`src/net/net_web.c`) with automated compatibility tests (`tests/net_compat.test.mjs`).
- **Files/areas:** `include/pd_net.h`, `src/net/`, `tests/net_compat.test.mjs`
- **Validation command:** `node tests/net_compat.test.mjs`
- **Evidence:** Exited 0 (`ALL NET COMPATIBILITY TESTS PASSED!`).

### T5 Dual-Screen Stereoscopic 3D & Touch UI (`dual-screen-ui`)
- **Status:** done
- **Objective:** Implement `citro2d` / `libctru` 3DS platform layer (`src/platform/platform_3ds.c`), host software rasterizer (`src/platform/platform_host.c`), stereoscopic 3D top-screen board + HUD (`src/ui/ui_render.c`), and bottom touch/button input (`src/ui/ui_input.c`).
- **Files/areas:** `include/pd_platform.h`, `include/pd_ui.h`, `src/platform/`, `src/ui/`, `src/main.c`, `tests/test_ui_sim.c`
- **Validation command:** `make build/host/test_ui_sim && ./build/host/test_ui_sim`
- **Evidence:** Exited 0 (`test_ui_sim: ALL TESTS PASSED`).

### T6 OCI `devkitARM` Build & `.3dsx` / `.cia` Packaging (`3ds-build-and-cia`)
- **Status:** done
- **Objective:** Build and verify `build/3ds/3ds_pieceduel.elf`, `.smdh`, `.3dsx`, and `.cia` inside rootless Podman/Docker container (`Containerfile.3ds`).
- **Files/areas:** `Containerfile.3ds`, `Makefile`, `meta/app.rsf`, `meta/generate_assets.py`, `scripts/verify-3ds-build.sh`
- **Validation command:** `make 3ds && make verify-3ds`
- **Evidence:** Exited 0 (`PASS: Verified ARM11 ELF, SMDH (0x36C0), 3DSX, and CIA (0x2020) artifacts.`).

### T7 Rugged Remediation & Docs Audit (`rugged-and-docs-audit`)
- **Status:** done
- **Objective:** Remediate initial `/rugged` findings and run the 5-role `wgm` docs-audit swarm (`docs/audit/2026-11-02T0325Z_initial-3ds-release.md`), resolving all 10 Agent actions.
- **Files/areas:** `src/net/`, `src/storage/`, `src/platform/`, `src/ui/`, `src/app.c`, `README.md`, `AGENTS.md`, `specs/`, `docs/audit/`, `scripts/check-docs.sh`
- **Validation command:** `make test && make 3ds && make verify-3ds && bash scripts/check-docs.sh`
- **Evidence:** Exited 0 (`3aaf9bf`, `05eebe5`).

### T8 UI, Trainer Rendering, Variant Coaching & Handheld UX (`ui-trainer-and-review-fixes`)
- **Status:** done
- **Objective:** Fix `resolve_active_view()` board desync in Puzzle Rush/Practice, suppress Hot Squares answer-key leak and draw `threat_found_mask` + Rush `MM:SS` timer + `selected_piece` trait preview, make `pd_review_move()` variant-aware (`advanced`/`misere`), cache replay coaching totals, add `misere` staging + `B`-exit `pd_net_close()`, and add regression tests.
- **Files/areas:** `include/pd_ui.h`, `src/engine/review.c`, `src/ui/ui_render.c`, `src/ui/ui_input.c`, `src/app.c`, `tests/test_engine.c`, `tests/test_ui_sim.c`
- **Validation command:** `make test-host && make build/host/pd_cli && node tests/compat_diff.test.mjs`
- **Evidence:** Exited 0 (`test_engine`, `test_ui_sim`, and 580-check `compat_diff.test.mjs` passed).

### T9 Network Lifecycle, Matchmaking, UDS & 3DS Hardware (`net-lifecycle-and-3ds-hardware`)
- **Status:** done
- **Objective:** Fix `/api/matchmake` `"gameId"` extraction, RFC 6455 `PING`/`PONG`, `rx_buf` overflow protection, `fd >= 0` checks, UDS scan caching + `udsExit()`, `osSetSpeedupEnable(true)` + `aptHook` sleep timer compensation in `platform_3ds.c`, and `.bak` save backup retention in `storage.c`.
- **Files/areas:** `src/net/`, `src/platform/platform_3ds.c`, `src/storage/storage.c`, `tests/net_compat.test.mjs`
- **Validation command:** `node tests/net_compat.test.mjs`
- **Evidence:** Exited 0 (`0bfc172`, `ALL NET COMPATIBILITY TESTS PASSED!`).

### T10 QR Hub V2 Short URLs, 8-Mask Scoring & Workspace Gate (`qr-hub-v2-and-workspace-gate`)
- **Status:** done
- **Objective:** Add `/c/<idx>` short download URLs (shrinking QR codes from V4 `33×33` to V2 `25×25`), ISO/IEC 18004 8-mask penalty evaluation, `Content-Length` cap, local-subnet `/push` validation + `SHUT_WR` in `../serve_3ds.py`, and include `3ds_pieceduel` in `/home/lafiamafia/homebrew/3ds/Makefile` `test`.
- **Files/areas:** `../serve_3ds.py`, `../Makefile`
- **Validation command:** `python3 ../serve_3ds.py --self-test`
- **Evidence:** Exited 0 (`PASS: Discovered 5 package(s) ... (QR 25x25)`).
