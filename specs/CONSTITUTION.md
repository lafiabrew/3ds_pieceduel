# Constitution — `3ds_pieceduel`

## Core Principles

1. **Bit-Exact Engine Determinism**: `src/engine/` must remain 100% deterministic across Mulberry32 PRNG, FNV-1a `dateSeed`, legal moves, alpha-beta AI, puzzles, and `reviewMove` coaching, verified by `tests/compat_diff.test.mjs`.
2. **Cooperative 3DS Main-Loop Safety**: Because `libctru`'s `aptMainLoop()` must be polled every frame on the single main thread to service 3DS HOME Menu suspension, lid-close sleep, and power interrupts, all TCP/HTTP socket operations (`connect()`, `send()`, `recv()`) in `src/net/net_p2p.c` and `src/net/net_web.c` must use non-blocking `O_NONBLOCK` + bounded `poll()` timeouts ($\le 1500\text{ ms}$). *(Note: numeric IPv4 addresses are recommended on 3DS so `gethostbyname()` DNS resolution is bypassed).*
3. **Crash-Safe Atomic Persistence**: All SD card saves (`sdmc:/3ds/pieceduel/save.dat`) must write to `<path>.tmp`, flush, and atomically `rename()` with IEEE 802.3 CRC32 verification.
4. **Zero-Privilege Reproducible Builds**: Building `.elf`, `.smdh`, `.3dsx`, and `.cia` must succeed via rootless Podman/Docker (`Containerfile.3ds`) with `-Wall -Wextra -Werror` (using `-std=c11` on host and `-std=gnu11` on 3DS for `libctru` system headers) and zero host-installed `devkitPro` prerequisites.
5. **Dual-Input Accessibility**: Every screen (`Title`, `Match`, `Daily`, `Trainers`, `Tutorial`, `Replay`, `Multiplayer`, and `Stats`) must be operable via both Stylus touch (`320×240` bottom screen) and hardware buttons (D-Pad / Circle Pad + `A`/`B`/`X`/`Y`/`L`/`R`), with an optional color-blind-safe palette.
