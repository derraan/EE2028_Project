# Enhancement — Dual-edge push-button input (button only)

This document describes a **button-specific enhancement** over a naive “EXTI = one press” design. It does **not** change sensors, telemetry formatting, LED blink rates, or matrix/IR/buzzer menus. It only changes how the **USER push button** is sampled, classified, and how those classifications drive **mode / laser / rescue flow**.

**Scope:** `USER` button (BSP `BUTTON_USER` / `USER_BUTTON_PIN`), active-low.  
**Code:** `HAL_GPIO_EXTI_Callback()`, `Process_PushButton()`, `PB_OnSinglePress()`, `PB_OnDoublePress()` in `Core/Src/main.c`.

---

## Why this enhancement

A single EXTI edge treated as a “press” cannot tell apart:

- contact bounce
- short click vs hold-to-fire
- first click of a double vs a true single
- press vs release (if both edges fire)

This design separates **edge notification (EXTI)** from **level + duration classification (GPIO + time windows)** so the same physical button cleanly drives Standby ↔ Battle, recharge, full-auto fire, and Last-of-EE2028 rescue without those collisions.

---

## Hardware / Cube setup

| Item | Requirement |
|------|-------------|
| Pin | Board USER button (active-low: pressed = `GPIO_PIN_RESET`) |
| EXTI | Prefer **rising and falling** on the button line |
| Init | `BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI)` |
| Fallback | If BSP/Cube only enables one edge, **GPIO polling in `Process_PushButton()` still recovers press/hold/release** |

Rising = typically **release** (line returns high).  
Falling = typically **press** (line pulled low).  
The ISR does **not** assume which edge fired; the main loop **reads GPIO level** to decide.

---

## Timing constants

| Symbol | Value | Role |
|--------|------:|------|
| `PB_DEBOUNCE_MS` | 100 ms | Ignore EXTI edges closer than this to the last accepted edge |
| `PB_HOLD_MS` | 500 ms | Still pressed this long → **hold** (not a short click) |
| `PB_DOUBLE_PRESS_MS` | 500 ms | Max gap between two **short-click completions** to count as a double |
| `PB_FIRE_PERIOD_MS` | 500 ms | Full-auto shot spacing while held in Battle |

All intervals use `HAL_GetTick()` (SysTick). There is one MCU time base; multiple **software windows** ride on it.

---

## Signal path (edge + level)

```text
                    ┌─────────────────────────┐
  Button line ───►  │ EXTI rising + falling   │
                    │ HAL_GPIO_EXTI_Callback  │
                    │  • 100 ms debounce      │
                    │  • stamp tick + flag    │
                    └───────────┬─────────────┘
                                │ pb_edge_pending
                                ▼
                    ┌─────────────────────────┐
  Main loop ──────► │ Process_PushButton()    │
                    │  • BSP_PB_GetState()    │
                    │  • start/end window     │
                    │  • hold latch           │
                    │  • short / double       │
                    │  • deferred single      │
                    └───────────┬─────────────┘
                                │
              ┌─────────────────┼─────────────────┐
              ▼                 ▼                 ▼
      PB_OnDoublePress   PB_OnSinglePress   hold latch
      (mode / rescue)    (Battle recharge)  (Battle fire)
```

### Debounce (EXTI only)

In `HAL_GPIO_EXTI_Callback`:

1. Accept only `USER_BUTTON_PIN`.
2. If `now - pb_last_edge_tick < 100 ms` → **return** (bounce discarded).
3. Else update `pb_last_edge_tick`, set `pb_edge_pending = true`.

No mode changes and no `printf` in the ISR.

### EXTI rising + falling

- Each qualified edge only **notifies** that the line moved.
- Classification waits for `Process_PushButton()`, which samples **GPIO level at the same time** as it advances the press/hold/release state.

### GPIO level + duration window

| GPIO (active-low) | Transition | Action |
|-------------------|------------|--------|
| Becomes **LOW** while was up | Press start | `pb_is_down = true`, `pb_down_tick = now`, clear hold latch |
| Stays **LOW** for ≥ 500 ms | Hold setup | `pb_hold_latched = true`, cancel pending single / double arming |
| Becomes **HIGH** while was down | Release | `duration = now - pb_down_tick` |

Then:

- If hold was latched → release ends the hold; **duration is not a short click**.
- Else if `duration < 500 ms` → **short click completed** (candidate for single or double).

So the “setup window” is literally: **falling (press) → rising (release)**, measured while GPIO confirms level.

---

## Gesture classification

### Short click

Released before 500 ms, and hold never latched.

1. First short click → arm deferred single; deadline = now + 500 ms; remember `pb_last_short_press_tick`.
2. Second short click with `(now - pb_last_short_press_tick) ≤ 500 ms` → **double press**; cancel deferred single; call `PB_OnDoublePress()`.
3. If deadline expires with no second short click → **single press**; call `PB_OnSinglePress()`.

### Hold

GPIO still low at 500 ms after press start:

1. Latch hold; cancel any armed single.
2. In **Battle only**, full-auto fire uses the latch + continued LOW level (see below).
3. On release, hold latch clears; this release does **not** count toward double-press.

---

## How the button changes mode / “menu” flow

The drone has no on-screen menu UI in this basic build; **mode + laser + rescue are the control surface**. The enhanced button is the only input that walks that surface as follows.

### Flow map (button-driven only)

```text
                 double short
  STANDBY_MODE ───────────────► BATTLE_MODE
       ▲                             │
       │         double short        │ hold ≥500 ms
       └─────────────────────────────┤   └─► full-auto fire (−5 / 500 ms)
                                     │       while GPIO low & charge ≥5
                                     │
                                     │ short single (deferred)
                                     │   charge <5  → +3 recharge
                                     │   charge ≥5  → hint only
                                     │
        accel upside-down (not PB)   ▼
                             THE_LAST_OF_EE2028
                                     │
                 double short        │
            ┌────────────────────────┤
            │ upright                │ still upside down
            ▼                        ▼
       BATTLE_MODE            rescue attempt++
       (rescued)                 │
                                 ├ attempt <5 → stay in mode
                                 └ attempt ≥5 → SELF_DESTRUCT_*

  (Non-button: 10 s wall-clock without rescue also → SELF_DESTRUCT_*;
   attack alerts print every 1 s but are not counted)
```

### Per-mode button menu

| Mode | Short single | Double short | Hold (≥500 ms) |
|------|--------------|--------------|----------------|
| **STANDBY_MODE** | Ignored | → `BATTLE_MODE` | Ignored (no fire) |
| **BATTLE_MODE** | Recharge +3 if charge &lt; 5/10; else print hold/double hint | → `STANDBY_MODE` | Full-auto laser while held |
| **THE_LAST_OF_EE2028** | Ignored | Rescue attempt / rescue / explode path | Ignored for laser |
| **SELF_DESTRUCT_*** | Ignored | Ignored | Ignored |

### Battle fire (hold path only)

While `pb_hold_latched &&` GPIO still pressed and mode is Battle:

- Charge ≥ 5/10 → consume 5/10 every `PB_FIRE_PERIOD_MS` (non-blocking).
- Charge &lt; 5/10 → warn once to single-press recharge.
- Release (GPIO high) → stop firing; no accidental “single” or “double” from that release.

---

## Compared to a basic EXTI-only button

| Basic EXTI-as-press | This enhancement |
|---------------------|------------------|
| Every edge ≈ one “press” | Rising/falling + GPIO level |
| Bounce can fake doubles | 100 ms EXTI debounce |
| Cannot separate hold vs click | 500 ms hold window on level |
| Single often fires on first edge of a double | Deferred single after 500 ms quiet |
| Hold edge also triggers recharge | Hold cancels pending single; release after hold ≠ click |
| Blocking fire loops common | Non-blocking fire tick while GPIO low |

---

## Operator summary

1. **Click** = down+up under 500 ms.  
2. **Double** = two clicks whose completions are ≤ 500 ms apart.  
3. **Hold** = keep down past 500 ms (Battle → fire).  
4. Debounce = ignore EXTI chatter under 100 ms.  
5. Mode “menus” are entirely these gestures plus (outside this doc) sensors for Last-of-EE2028 entry and the 10 s rescue wall-clock timeout (whichever comes first vs 5 failed upside-down rescue attempts).

---

## Implementation checklist

- [ ] Cube/BSP: USER button EXTI **rising + falling** when possible  
- [ ] Confirm active-low polarity matches `GPIO_PIN_RESET` = pressed  
- [ ] Exercise Standby double → Battle → double → Standby  
- [ ] Battle: short single recharge only after 500 ms with no second click  
- [ ] Battle: hold fires; release stops; no recharge on that release  
- [ ] Last of EE2028: singles ignored; doubles count rescue / fail path  
- [ ] ISR stays free of `printf` / mode side effects beyond edge stamp  
