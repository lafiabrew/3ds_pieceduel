# Context & Domain Glossary — `3ds_pieceduel`

| Canonical Term | C11 / Upstream Symbol | Meaning in `3ds_pieceduel` |
|---|---|---|
| **Piece (`0..15`)** | `pd_attributes_t` / `decodePiece` | 4-bit integer encoding 4 binary attributes: `bit 3` = Tall (`1`) / Short (`0`), `bit 2` = Light (`1`) / Dark (`0`), `bit 1` = Square (`1`) / Round (`0`), `bit 0` = Solid (`1`) / Hollow (`0`). |
| **Turn Flow (`Place` + `Give`)** | `pd_move_t` (`PD_MOVE_OPEN`, `PD_MOVE_PLACE`) | Opening turn gives the first piece (`piece_in_hand`); each subsequent turn places `piece_in_hand` on an empty board cell and hands an available piece (`give`) to the opponent. |
| **Variant** | `pd_variant_t` (`GameVariant`) | Ruleset: `standard` (10 winning lines of 4), `misere` (completing a line awards the win to the opponent), `advanced` (adds the nine 2×2 squares for 19 winning groups). |
| **Difficulty** | `pd_difficulty_t` (`Difficulty`) | Built-in AI strength: `easy` (immediate win + safe give), `medium` (depth-2 alpha-beta), `hard` (depth-3 alpha-beta + exact endgame at $\le 6$ empty cells). |
| **Hot Square** | `pd_threat_cells` (`threatCells`) | An empty board cell where a winning line/square already has 3 pieces sharing $\ge 1$ attribute and is one placement from completion. |
| **Tactics Trainers** | `pd_trainer_kind_t` | Deterministic seed-keyed training modes: `Practice` (1-move win), `Forced Win-in-Two` (`pd_generate_win_in_two`), `Safe Give` (`pd_generate_safe_give`), `Hot Squares` (`pd_generate_threat_drill`), and timed `Puzzle Rush`. |
| **Move Coaching** | `pd_move_review_t` (`reviewMove`) | Post-game replay analyzer flagging `missed_win_cell` (missed 1-move win), `unsafe_give` + `safer_give` (avoidable blunder gift), and `forced_loss` (handed opponent a forced mate in 2). |
| **UDS** | `PD_NET_MODE_UDS` (`nwm::UDS`) | Nintendo 3DS ad-hoc local wireless (WLAN comm ID `0x50443344`), connecting two 3DS consoles directly without a Wi-Fi router. |
| **LAN P2P** | `PD_NET_MODE_LAN_P2P` (`soc:U`) | Wi-Fi local network peer-to-peer mode using UDP beacon discovery (`:43210`) and TCP lockstep packets (`:43211`). |
| **Web Cross-Play** | `PD_NET_MODE_WEB` | Online mode connecting the 3DS over HTTP (`:4321`) + RFC 6455 WebSocket (Engine.IO v4 / Socket.IO v4) to a `pieceduel` server. |
| **3DS Artifacts & Paths** | `sdmc:/`, `.3dsx`, `.smdh`, `.cia`, `.bnr` | `sdmc:/` = 3DS microSD card root (`sdmc:/3ds/pieceduel/save.dat`); `.3dsx` = Homebrew Launcher binary; `.smdh` = icon/title metadata; `.bnr` = CBMD home banner + chime; `.cia` = FBI-installable HOME Menu archive. |
