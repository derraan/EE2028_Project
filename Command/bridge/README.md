# Optional UART bridge

**Preferred path:** Chrome/Edge **Web Serial** directly from `Command/index.html` (see `src/serial.js`).

Use a localhost bridge only if Web Serial is unavailable (e.g. Firefox) or when merging Wi‑Fi/BLE later.

Suggested shape (not required for fixture demo):

```text
COM port @ 115200  →  Node/Python reader  →  WebSocket ws://localhost:8765
                                              ↓
                                    same Command/src/parser.js line feed
```

Keep parsing in the browser (`parser.js`) so live UART and fixture replay stay identical. The bridge should forward raw lines, not invent dashboard state.
