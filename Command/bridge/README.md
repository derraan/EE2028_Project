# VESPER bridges

## Wi-Fi board bridge (`wifi_bridge.mjs`, built into `serve.mjs`)

The board's on-board Wi-Fi (ISM43362) runs a TCP server on **port 5028** and streams exactly the UART telemetry bytes (`<line>\r\n`). Browsers cannot open raw TCP, so `node Command/serve.mjs` relays it:

```text
board ISM43362 ──TCP :5028──► serve.mjs /wifi/stream ──SSE──► Command/src/wifi.js ──► parser.js
```

| Route | Returns |
|---|---|
| `GET /wifi/stream?host=172.20.10.5[&port=5028]` | `text/event-stream`: one `data:` event per telemetry line; `event: status` = `connecting …` / `connected …` / `closed` / `error …`. Retries every 2 s while the browser stays connected |
| `GET /wifi/scan[?port=5028][&subnet=172.20.10]` | JSON `{ port, subnets, found }`: hosts with the port open on the laptop's own /24 networks |

In VESPER: Signal source **Live board (Wi-Fi)** → **Find board** or type the IP → **Connect**.

Rules: the bridge forwards raw lines only and never creates dashboard state. All parsing stays in `parser.js`, so Wi-Fi, Web Serial, fixture replay and the virtual board render identically. `serve.mjs` listens on `127.0.0.1` only.

Tests: `node scripts/test_wifi_bridge.mjs`.

## USB serial

Use Chrome/Edge **Web Serial** directly (`src/serial.js`), no bridge needed.
