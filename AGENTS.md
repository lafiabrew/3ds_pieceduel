# AGENTS.md — 3ds_pieceduel

> Operational guide: how to build, test, package, and deploy `3ds_pieceduel` to a CFW Nintendo 3DS.
> Status and task history live in `IMPLEMENTATION_PLAN.md`; qualitative documentation audits live in `docs/audit/`.

## What this is

Native C11 Nintendo 3DS homebrew shared-trait four-in-a-row strategy game, built with `libctru` and `citro2d`:
- **Engine (`src/engine/`)**: Pure C11 rules & AI engine (16 pieces with 4 binary attributes, `standard`/`misere`/`advanced` variants, Easy/Medium/Hard alpha-beta AI with Mulberry32 PRNG, FNV-1a `dateSeed` Daily Puzzle + Archive, Win-in-Two / Safe-Give / Hot-Squares / Puzzle Rush trainers, 3-step Tutorial, and `reviewMove` post-game move coaching).
- **Dual-Screen UI (`src/ui/`, `src/platform/`)**: Top `400×240` stereoscopic 3D board + HUD/traits; Bottom `320×240` resistive touch board + 16-piece tray + button navigation + `swkbd`.
- **Multiplayer (`src/net/`)**: Pass-and-Play, 3DS-to-3DS Local Wireless (`nwm::UDS`), Wi-Fi LAN P2P (`UDP :43210` beacon + `TCP :43211` lockstep), and Socket.IO v4 Web Cross-Play (`HTTP :4321` CSRF auth + RFC 6455 WebSocket).
- **Persistence (`src/storage/`)**: Atomic `.tmp`-then-`rename` CRC32-checked save file at `sdmc:/3ds/pieceduel/save.dat` (with `.bak` backup).

## Prerequisites

- **Containerized 3DS build (`make 3ds`)**: Rootless `podman` or `docker`, plus `python3` for `meta/generate_assets.py`.
- **Host test suite (`make test-host`)**: `gcc` with ASan/UBSan (`-fsanitize=address,undefined`) and `make`.
- **Compatibility suite (`make test-compat`)**: Node.js $\ge 22.15$.

## Build & Package (Podman / OCI preferred)

```bash
# Build 3DS ARM11 artifacts (.elf, .smdh, .3dsx, .cia) inside OCI container
make 3ds

# Verify ELF machine type (ARM11), SMDH size (0x36C0), 3DSX header, and CIA header (0x2020)
make verify-3ds

# Serve .cia over HTTP with a scannable FBI QR code (and web hub at :8765)
make qr

# Push 3ds_pieceduel.3dsx over Wi-Fi to Homebrew Launcher (press Y on 3DS first)
make netload IP=192.168.1.XXX
```

## Validate (Backpressure)

*Backpressure* means deterministic pass/fail verification commands that must exit `0` before marking any task `done`:

```bash
make test-host              # C11 engine unit tests + headless dual-screen 3D/touch UI simulation (ASan/UBSan)
make test-compat            # Engine parity suite + LAN/UDS/Web Socket.IO compatibility tests
make test                   # Run both test-host and test-compat
bash scripts/check-docs.sh  # Deterministic documentation structure & link check
```
- A task is not `done` until its validation command exits **0**.

## Documentation Index

- [`README.md`](README.md) — Player & operator guide (features, architecture, controls, CFW installation, multiplayer).
- [`IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) — Task ledger and ruggedness gate records.
- [`specs/CONSTITUTION.md`](specs/CONSTITUTION.md) — Non-negotiable architectural and quality invariants.
- [`specs/CONTEXT.md`](specs/CONTEXT.md) — Canonical domain glossary (`pd_engine.h`, `pd_net.h`, `pd_storage.h`, `pd_ui.h`).
- [`docs/audit/README.md`](docs/audit/README.md) — Qualitative 5-role documentation audit reports.
