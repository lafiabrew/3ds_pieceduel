# Context & Domain Glossary — `3ds_pieceduel`

| Canonical Term | Meaning in `3ds_pieceduel` |
|---|---|
| **Piece (`0..15`)** | 4-bit integer encoding 4 binary attributes: `bit 3` = Tall (`1`) / Short (`0`), `bit 2` = Light (`1`) / Dark (`0`), `bit 1` = Square (`1`) / Round (`0`), `bit 0` = Solid (`1`) / Hollow (`0`). |
| **Variant (`pd_variant_t`)** | Ruleset: `standard` (10 winning lines of 4), `misere` (completing a line awards the win to the opponent), `advanced` (adds the nine 2×2 squares for 19 winning groups). |
| **Hot Square (`threatCells`)** | An empty board cell where a winning line/square already has 3 pieces sharing $\ge 1$ attribute and is one placement from completion. |
| **UDS (`PD_NET_MODE_UDS`)** | Nintendo 3DS ad-hoc local wireless (`nwm::UDS` service, WLAN comm ID `0x50443344`), connecting two 3DS consoles directly without a Wi-Fi router. |
| **LAN P2P (`PD_NET_MODE_LAN_P2P`)** | Wi-Fi local network peer-to-peer mode using UDP beacon discovery (`:43210`) and TCP lockstep packets (`:43211`). |
| **Web Cross-Play (`PD_NET_MODE_WEB`)** | Online mode connecting the 3DS over HTTP (`:4321`) + RFC 6455 WebSocket (Engine.IO v4 / Socket.IO v4) to a `pieceduel` server. |
