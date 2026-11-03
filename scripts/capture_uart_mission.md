# Capture a real UART mission (lab)

Hardware capture cannot run until USART retarget + board flash are available. Until then, use synthetic fixtures under `Command/fixtures/`.

## When the board is ready

1. Flash firmware with the strong `Telemetry_WriteLine` override (see `App/Src/telemetry_uart_hal.c.example`, `EE_TELEMETRY_USART=1`) → USART1 (ST-LINK VCP) @ **115200 8N1**. Mission lines use `Telemetry_*`; CubeIDE `printf` is debug-only.
2. **Disconnect the debugger session** if it monopolizes the COM port / semihosting.
3. Open a serial terminal (Tera Term / PuTTY) on the ST-LINK Virtual COM port.
4. Fly a mission covering: Standby telemetry → Battle → charge → hold-to-fire → **The Last of EE2028** → countdown → rescue **or** timeout / early self-destruct → `EVT TERMINATE` → `Cheerio!`.
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

## Expected EVT checklist

A good capture should include most of:

```text
EVT MODE=STANDBY_MODE
EVT MODE=BATTLE_MODE
EVT CHARGE=n/10
EVT FIRE charge=n/10
EVT MODE=THE_LAST_OF_2028
EVT ATTACK
EVT COUNTDOWN remaining_ms=N
EVT RESCUE                  # if rescued
# — or —
EVT SELF_DESTRUCT reason=timeout|rescue_limit|early
EVT TERMINATE
Cheerio!
```

Canonical enum / EVT tag is `THE_LAST_OF_2028`; human banner / printf prose is `THE LAST OF EE2028`. See [docs/Telemetry_EVT_Contract.md](../docs/Telemetry_EVT_Contract.md).

## Design note

PR #3 used semihosting `printf`. Integration routes the same human lines plus `EVT …` through `App/Src/telemetry_evt.c` so Tera Term and VESPER share one stream.
