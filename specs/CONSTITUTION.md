# Constitution — `3ds_pieceduel`

## Core Principles

1. **Bit-Exact Engine Parity**: `src/engine/` must remain 100% deterministic and bit-for-bit compatible with upstream `@pieceduel/engine` (Mulberry32 PRNG, FNV-1a `dateSeed`, legal moves, alpha-beta AI, puzzles, and `reviewMove` coaching), verified by `tests/compat_diff.test.mjs`.
2. **Cooperative 3DS Main-Loop Safety**: No network or filesystem call on the `aptMainLoop()` thread may block unboundedly; all TCP/HTTP operations in `src/net/` must use non-blocking `O_NONBLOCK` + bounded `poll()` timeouts ($\le 1500\text{ ms}$).
3. **Crash-Safe Atomic Persistence**: All SD card saves (`sdmc:/3ds/pieceduel/save.dat`) must write to `<path>.tmp`, flush, and atomically `rename()` with IEEE 802.3 CRC32 verification.
4. **Zero-Privilege Reproducible Builds**: Building `.elf`, `.smdh`, `.3dsx`, and `.cia` must succeed via rootless Podman/Docker (`Containerfile.3ds`) with zero warnings and zero host-installed `devkitPro` prerequisites.
5. **Dual-Input Accessibility**: Every screen must be operable via both Stylus touch (`320×240` bottom screen) and hardware buttons (D-Pad / Circle Pad + `A`/`B`/`X`/`Y`/`L`/`R`), with an optional color-blind-safe palette.
