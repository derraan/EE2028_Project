---
name: Integration Telemetry
overview: Move PR #3 sensor prints from semihosting to USART / ST-LINK Virtual COM, add machine-readable EVT lines, and wire one parser for live UART and VESPER replay.
todos:
  - id: capture-uart-mission
    content: With the debugger disconnected, capture a real UART mission (mode changes, sensors, charge, fire, rescue, termination) and save it as the parser’s replay fixture.
    status: pending
  - id: wire-vesper-regression
    content: Wire VESPER so replay and live UART produce the same dashboard state, then run the full board regression including rescue, timeout, early self-destruct, and a display fault.
    status: pending
isProject: false
---

# EE2028 unified integration plan

## 3. Telemetry and VESPER ingestion

Move the existing sensor print output from semihosting to **USART / ST-LINK Virtual COM**. Keep the current human-readable sensor layout and add a small set of machine-readable `EVT` lines. Each message occupies one line, so the same stream works in a serial terminal and in VESPER.

```
Telemetry -> T:26.4 °C, P:1012.5 hPa, H:55.1 % | Accel:[0.0, 0.1, 9.8] m/s^2 | Gyro:[0.0, 0.0, 0.2] dps | Magneto:[31.0, 42.0, 18.0] mgauss
EVT MODE=BATTLE_MODE
EVT CHARGE=6/10
EVT FIRE charge=1/10
EVT MODE=THE_LAST_OF_EE2028
EVT COUNTDOWN remaining_ms=9000
EVT SELF_DESTRUCT reason=timeout
EVT TERMINATE
Cheerio!
```

The sensor line follows PR #3’s layout. The `EVT` lines are a proposed contract. Generate them from the same state and actions that feed the adapter. A `FIRE` line includes charge **after** the shot, and countdown updates come from the firmware’s rescue deadline. Agree whether full sensor telemetry continues outside Standby, since PR #3 currently prints the full sensor line there. If the dashboard shows a persistent threshold warning, include a way to explicitly clear it when readings recover.

Use **one ingestion parser for live UART and recorded missions**. Buffer incoming chunks until a newline, then parse sensor lines and known `EVT` messages. Keep unrecognized text in the raw log. Preserve units and vector axes in the parsed data. Treat a cable disconnect as a link loss and `EVT TERMINATE` as mission termination. If `Cheerio!` must be the final line, send the termination tag immediately before it and let the parser read through the final message.

## 4. Implementation order

| Step                                                   | Done when                                                                                                                                                                                        |
| ------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **4. Capture a real UART mission**                     | With the debugger disconnected, a serial terminal captures mode changes, sensor readings, charge, fire, rescue, and termination. Save that capture as the parser’s replay fixture.               |
| **5. Wire VESPER and run the full regression**         | Replay and live UART produce the same dashboard state. The full mission passes on the board, including rescue, timeout, early self-destruct, and a display fault.                                |
