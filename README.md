# 3ds_pieceduel

**3ds_pieceduel** is a native C11 Nintendo 3DS homebrew edition of
[**pieceduel**](https://github.com/SchwartzKamel/pieceduel) — a shared-trait
four-in-a-row abstract strategy duel (you hand your opponent the piece they must
place). Built with `libctru` and `citro2d` for Custom Firmware (CFW) 3DS
systems, it brings a stereoscopic 3D top-screen board, a stylus-driven
bottom-screen touch board and 16-piece tray, bit-exact parity with
`@pieceduel/engine`, and three-tier multiplayer (3DS Local Wireless, Wi-Fi LAN
P2P, and live cross-play with a `pieceduel` web server).

---

## Feature Matrix

| Area | Feature | Status | Implementation |
|---|---|---|---|
| **Core Engine** | 16 pieces (4 binary traits: Tall/Short, Light/Dark, Square/Round, Solid/Hollow) | Implemented | `src/engine/attributes.c`, `src/engine/rules.c` |
| **Rules Variants** | `standard` (10 lines), `misere` (completing a line loses), `advanced` (+9 2×2 squares) | Implemented | `src/engine/board.c`, `src/engine/state.c` |
| **Built-in AI** | Easy (1-ply safe give), Medium (depth-2 alpha-beta), Hard (depth-3 + exact endgame) | Implemented | `src/engine/ai.c` (Mulberry32 PRNG) |
| **Daily Puzzle** | Deterministic FNV-1a `dateSeed("YYYY-MM-DD")` + Daily Archive stepper (`L`/`R`) | Implemented | `src/engine/puzzle.c` (100% seed-compatible with Web) |
| **Tactics Trainers** | Practice, Forced Win-in-Two, Safe Give, Hot Squares (Threat Vision), Puzzle Rush | Implemented | `src/engine/forced_win.c`, `src/engine/puzzle.c` |
| **Learn & Review** | 3-step Interactive Tutorial + Post-Game Replay Scrubber & Move Coaching (`reviewMove`) | Implemented | `src/engine/tutorial.c`, `src/engine/review.c` |
| **3DS Dual Screens** | Stereoscopic 3D Top Board (`400×240`) + Stylus Touch Grid & 16-Piece Tray (`320×240`) | Implemented | `src/ui/ui_render.c`, `src/ui/ui_input.c` |
| **Multiplayer: Pass & Play** | Two players sharing one 3DS with optional beginner "hot square" threat hints | Implemented | `src/app.c` |
| **Multiplayer: 3DS Wireless** | Ad-hoc 3DS-to-3DS Local Wireless via `libctru` `nwm::UDS` (no Wi-Fi router needed) | Implemented | `src/net/net_uds.c` |
| **Multiplayer: LAN P2P** | Wi-Fi UDP beacon discovery (`:43210`) + TCP lockstep rooms (`:43211`) | Implemented | `src/net/net_p2p.c` |
| **Multiplayer: Web Cross-Play** | Connects to `pieceduel` web server (`:4321`) over HTTP CSRF auth + Socket.IO v4 | Implemented | `src/net/net_web.c` |
| **SD Persistence** | CRC32-verified save (`sdmc:/3ds/pieceduel/save.dat`): streaks, Elo, stats, 20 replays | Implemented | `src/storage/storage.c` |
| **CFW Packaging** | `.3dsx` + `.smdh` (Homebrew Launcher / `3dslink`) and `.cia` (FBI HOME Menu install) | Implemented | `Makefile`, `Containerfile.3ds`, `meta/app.rsf` |

---

## Architecture

```mermaid
flowchart TB
    subgraph TopScreen["Top Screen (400×240 Stereoscopic 3D)"]
        Board3D["2.5D Board + Tall/Short Stereo Parallax"]
        HUD["Trait Matrix · In-Hand Piece · Move Coaching"]
    end

    subgraph BottomScreen["Bottom Touch Screen (320×240 Stylus + Buttons)"]
        TouchGrid["Interactive 4×4 Placement Grid"]
        PieceTray["16-Piece Selection Tray"]
        ActionBar["Action Bar & Software Keyboard (swkbd)"]
    end

    subgraph Core["Pure C11 Engine & State Machine"]
        Engine["pd_engine (Rules, Variants, Alpha-Beta AI, Puzzles, Coaching)"]
        App["pd_app (Title, Match, Daily, Trainers, Tutorial, Replay, Lobby)"]
        Storage["pd_storage (CRC32 sdmc:/3ds/pieceduel/save.dat)"]
    end

    subgraph Net["Multiplayer Stack (src/net/)"]
        UDS["3DS Local Wireless (nwm::UDS)"]
        LAN["Wi-Fi LAN P2P (UDP Beacon + TCP)"]
        Web["Web Cross-Play (HTTP Auth + Socket.IO v4)"]
    end

    BottomScreen --> App
    App --> TopScreen
    App <--> Engine
    App <--> Storage
    App <--> Net
```

---

## Controls (Stylus Touch + Buttons)

| Input | Action in Match / Puzzles | Action in Menus / Replays |
|---|---|---|
| **Stylus Touch** | Tap any **4×4 Board Cell** to stage placement; tap any **Tray Piece** to hand it over | Tap menu rows, variant pills, or bottom action buttons |
| **D-Pad / Circle Pad** | Move cursor across the 4×4 board or 16-piece tray | Navigate menu items / lobby rooms |
| **A Button** | Confirm cell placement or piece gift | Select menu item / start mode |
| **B Button** | Cancel staged placement or return to Title Menu | Back to Title Menu |
| **X Button** | Toggle between Board cursor and Piece Tray cursor (or open Replay Coaching when finished) | Cycle AI Difficulty / Generate new trainer seed |
| **Y Button** | Toggle Threat ("Hot Square") Hints | Reveal puzzle answer / Toggle Color-Blind palette |
| **L / R Shoulders** | Step backward / forward in Daily Archive or Replay Scrubber | Cycle Rules Variant (`Standard` / `Misère` / `Advanced`) |
| **SELECT** | Offer / accept Rematch (or quick restart against AI) | — |
| **START** | Cleanly exit to Homebrew Launcher / 3DS HOME Menu | Exit application |

---

## Building (`Containerfile.3ds` or Native `devkitARM`)

The build requires **zero host privileges**: if `DEVKITARM` is not installed on
the host, `make 3ds` automatically builds inside an OCI container
(`Containerfile.3ds`, compatible with `podman` or `docker`).

```bash
# 1. Build 3DS ARM11 packages (.elf, .smdh, .3dsx, and .cia)
make 3ds

# 2. Verify ELF architecture, SMDH icon metadata, 3DSX header, and CIA structure
make verify-3ds
```

Artifacts are written to `build/3ds/`:
- `build/3ds/3ds_pieceduel.3dsx` — launch from **The Homebrew Launcher** or push live over Wi-Fi via `3dslink`.
- `build/3ds/3ds_pieceduel.cia` — install directly to the 3DS HOME Menu using **FBI**.

---

## Loading on a CFW 3DS

### Option 1: Wireless Netload via `3dslink` (Fastest for Development)
1. Open **The Homebrew Launcher** on your 3DS and press **Y** to open the `3dslink` receiver (note the IP shown on screen).
2. Run:
   ```bash
   make netload IP=192.168.1.XXX
   ```

### Option 2: SD Card (`.3dsx` or `.cia`)
- **Homebrew Launcher (`.3dsx`)**: Copy `build/3ds/3ds_pieceduel.3dsx` and `build/3ds/3ds_pieceduel.smdh` to `sdmc:/3ds/pieceduel/` on your SD card.
- **HOME Menu (`.cia`)**: Copy `build/3ds/3ds_pieceduel.cia` to `sdmc:/cias/` on your SD card, open **FBI** on your 3DS, select **SD → cias → 3ds_pieceduel.cia → Install and delete CIA**.

---

## Multiplayer Guide

1. **Pass-and-Play (Single 3DS)**: Choose **Pass & Play** from the Title Menu and pass the console after each turn.
2. **3DS-to-3DS Local Wireless (`UDS`)**: Choose **Multiplayer → Host 3DS Wireless** on one 3DS and **Join 3DS Wireless** on the second 3DS. Uses Nintendo's local ad-hoc wireless (`nwm::UDS`, comm ID `0x50443344`) — no Wi-Fi router needed.
3. **Wi-Fi LAN P2P (`UDP/TCP`)**: Choose **Multiplayer → Host LAN Room** on one device and **Scan LAN Rooms** (or connect by IP) on another 3DS or PC host running on the same Wi-Fi network.
4. **Cross-Play with `pieceduel` Web Server**:
   - Point **Server Host / Port** in the Multiplayer menu at your running `pieceduel` server (default port `4321`, e.g. `192.168.1.50:4321`, seeded account `demo` / `pieceduel123`).
   - Log in, then **Create Room** (displays a 6-character invite code that a browser or mobile player can join at `http://<host>:4321`), **Join by Invite Code**, or enter **Ranked Matchmaking**.

---

## Automated Compatibility & Verification Suite

We validate engine parity, network protocol interoperability, UI rendering, and
ARM11 binary packaging automatically without requiring physical 3DS hardware in
the loop:

```bash
# Run native C11 unit tests + headless dual-screen UI/stereo simulation (with ASan/UBSan)
make test-host

# Run cross-language differential fuzzing (C11 vs TypeScript @pieceduel/engine)
# + automated LAN P2P & Socket.IO v4 Web Cross-Play integration tests
make test-compat

# Run all host + compatibility tests
make test
```

---

## Safety & Legal Notes

- **Save Data Integrity**: Save data in `sdmc:/3ds/pieceduel/save.dat` is protected by an IEEE 802.3 CRC32 checksum; corrupted or mismatched save versions safely fall back to clean defaults without crashing.
- **Legal**: Like upstream `pieceduel`, `3ds_pieceduel` is an independent project and is not affiliated with, endorsed by, or sponsored by Gigamic or Nintendo.
