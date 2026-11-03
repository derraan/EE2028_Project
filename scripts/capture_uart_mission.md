# Capture a real UART mission (lab)

Hardware capture cannot run until USART retarget + board flash are available. Until then, use synthetic fixtures under `Command/fixtures/`.

## When the board is ready

1. Flash firmware with `Telemetry_WriteLine` → USART1 (ST-LINK VCP) @ **115200 8N1**.
2. **Disconnect the debugger session** if it monopolizes the COM port / semihosting.
3. Open a serial terminal (Tera Term / PuTTY) on the ST-LINK Virtual COM port.
4. Fly a mission covering: Standby telemetry → Battle → charge → fire → Last of 2028 → countdown → rescue **or** timeout / early self-destruct → `EVT TERMINATE` → `Cheerio!`.
5. Save the raw log as:

```text
Command/fixtures/mission_uart_capture.log
```

6. Replay in VESPER (same parser as live UART):

```bash
npx --yes serve Command
# browser → Replay fixture (or point main.js at the new file)
node scripts/test_uart_parser.mjs
```

## Design note

PR #3 used semihosting `printf`. Integration routes the same human lines plus `EVT …` through `App/Src/telemetry_evt.c` so Tera Term and VESPER share one stream.
