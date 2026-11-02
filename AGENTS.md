# AGENTS.md — 3ds_pieceduel

> Operational guide: how to build, test, package, and deploy `3ds_pieceduel` to a CFW Nintendo 3DS.
> Status and task history live in `IMPLEMENTATION_PLAN.md`; qualitative documentation audits live in `docs/audit/`.

## What this is

Native C11 Nintendo 3DS homebrew edition of **pieceduel** (`~/sandbox/pieceduel/`), built with `libctru` and `citro2d`:
- **Engine (`src/engine/`)**: Bit-exact C11 port of `@pieceduel/engine` (16 pieces with 4 binary attributes, `standard`/`misere`/`advanced` variants, Easy/Medium/Hard alpha-beta AI with Mulberry32 PRNG, FNV-1a `dateSeed` Daily Puzzle + Archive, Win-in-Two / Safe-Give / Hot-Squares / Puzzle Rush trainers, 3-step Tutorial, and `reviewMove` post-game move coaching).
- **Dual-Screen UI (`src/ui/`, `src/platform/`)**: Top `400×240` stereoscopic 3D board + HUD/traits; Bottom `320×240` resistive touch board + 16-piece tray + button navigation + `swkbd`.
- **Multiplayer (`src/net/`)**: Pass-and-Play, 3DS-to-3DS Local Wireless (`nwm::UDS`), Wi-Fi LAN P2P (`UDP :43210` beacon + `TCP :43211` lockstep), and live Web Cross-Play (`HTTP :4321` Auth.js CSRF login + RFC 6455 WebSocket / Socket.IO v4).
- **Persistence (`src/storage/`)**: Atomic `.tmp`-then-`rename` CRC32-checked save file at `sdmc:/3ds/pieceduel/save.dat`.

## Build & Package (Podman / OCI preferred)

```bash
# Build 3DS ARM11 artifacts (.elf, .smdh, .3dsx, .cia) inside OCI container
make 3ds

# Verify ELF machine type (ARM11), SMDH size (0x36C0), 3DSX header, and CIA header (0x2020)
make verify-3ds

# Push 3ds_pieceduel.3dsx over Wi-Fi to Homebrew Launcher (press Y on 3DS first)
make netload IP=192.168.1.XXX
```

## Validate (Backpressure)

```bash
make test-host          # C11 engine unit tests + headless dual-screen 3D/touch UI simulation (ASan/UBSan)
make test-compat        # 580-check C-vs-TS (@pieceduel/engine) differential suite + LAN/UDS/Web Socket.IO tests
make test               # Run both test-host and test-compat
bash scripts/check-docs.sh  # Deterministic documentation structure & link check
```
- A task is not `done` until its validation command exits **0**.
