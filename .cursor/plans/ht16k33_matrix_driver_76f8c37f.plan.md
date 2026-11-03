---
name: HT16K33 Matrix Driver
overview: Create a self-contained HT16K33 8x8 LED matrix driver using STM32 HAL conventions (HAL_StatusTypeDef, HAL_I2C_Master_Transmit, HAL_GetTick) with injectable I2C handle and a hardware smoke test — all in new files under `App/`.
todos:
  - id: ht16k33-header
    content: Create `App/Inc/ht16k33.h` with device struct, status enum, and all public function declarations
    status: completed
  - id: ht16k33-impl
    content: Create `App/Src/ht16k33.c` implementing Init, SetBrightness, SetBlinkRate, Clear, WriteFrame with mock-mode support
    status: completed
  - id: ht16k33-test
    content: Create `App/Test/ht16k33_test.c` with tick-based pixel/row/column sweep hardware test
    status: completed
isProject: false
---

# Task 1 — HT16K33 Matrix Driver

## Context

The HT16K33 is an I2C LED controller (default address `0x70`). The driver follows STM32 HAL conventions throughout: `HAL_StatusTypeDef` for return values, `HAL_I2C_Master_Transmit` for bus access, `HAL_GetTick` for timing, and CubeIDE-style include guards and type naming.

Existing code lives in [Core/Src/main.c](Core/Src/main.c) and [Core/Inc/drone.h](Core/Inc/drone.h) — we do **not** touch these. New files go under `App/`.

## New files

- `App/Inc/ht16k33.h` — public API, types, and constants
- `App/Src/ht16k33.c` — implementation
- `App/Test/ht16k33_test.c` — tick-based hardware smoke test (non-blocking, uses `HAL_GetTick`)

## API design (HAL-style)

```c
#include "stm32l4xx_hal.h"          /* official HAL header for types and I2C */

/* Default I2C address (7-bit, left-shifted for HAL) */
#define HT16K33_I2C_ADDR_DEFAULT  (0x70 << 1)
#define HT16K33_I2C_TIMEOUT       100U   /* ms */

/* Device handle — injectable I2C peripheral and address */
typedef struct {
    I2C_HandleTypeDef *hi2c;       /* from CubeMX, e.g. &hi2c1 */
    uint16_t           addr;       /* 8-bit (left-shifted) address for HAL */
} HT16K33_HandleTypeDef;

/* All functions return HAL_StatusTypeDef (HAL_OK / HAL_ERROR / HAL_BUSY / HAL_TIMEOUT) */
HAL_StatusTypeDef HT16K33_Init(HT16K33_HandleTypeDef *hdev,
                               I2C_HandleTypeDef *hi2c, uint16_t addr);
HAL_StatusTypeDef HT16K33_SetBrightness(HT16K33_HandleTypeDef *hdev, uint8_t level);
HAL_StatusTypeDef HT16K33_SetBlinkRate(HT16K33_HandleTypeDef *hdev, uint8_t rate);
HAL_StatusTypeDef HT16K33_Clear(HT16K33_HandleTypeDef *hdev);
HAL_StatusTypeDef HT16K33_WriteFrame(HT16K33_HandleTypeDef *hdev,
                                     const uint8_t frame[8]);
```

Key HAL-alignment decisions:
- **`HAL_StatusTypeDef`** return type everywhere — no custom enum; matches every other HAL driver
- **Handle struct named `*_HandleTypeDef`** — follows `I2C_HandleTypeDef`, `UART_HandleTypeDef` pattern
- **Address stored left-shifted (8-bit)** — matches `HAL_I2C_Master_Transmit` expectation directly
- **`HAL_I2C_Master_Transmit(hdev->hi2c, hdev->addr, ...)`** — the official HAL I2C transmit call
- **`#include "stm32l4xx_hal.h"`** — official CubeIDE HAL header, same as `drone.h`

## I2C protocol (HT16K33 datasheet)

- System oscillator on: transmit `0x21`
- Display on, blink off: transmit `0x81`
- Display on, blink rate N (0-3): transmit `0x81 | (rate << 1)`
- Brightness 0-15: transmit `0xE0 | level`
- Write display RAM: transmit 17 bytes — `0x00` (RAM start address) followed by 16 data bytes (8 rows x 2 bytes each; second byte of each pair is `0x00` for 8x8 matrix)

## Implementation details

- **`HT16K33_Init`**: store handle and address, then sequentially call `HAL_I2C_Master_Transmit` for oscillator-on, display-on, default brightness (max), and clear. Propagate the first non-`HAL_OK` status.
- **`HT16K33_WriteFrame`**: pack `frame[8]` into 17-byte buffer (`0x00` + interleaved row/`0x00` pairs), transmit with `HAL_I2C_Master_Transmit`.
- **`HT16K33_Clear`**: call `WriteFrame` with an all-zeros frame.
- **`HT16K33_SetBrightness`**: clamp to 0-15, transmit `0xE0 | level`.
- **`HT16K33_SetBlinkRate`**: clamp to 0-3, transmit `0x81 | (rate << 1)`.
- All timeouts use `HT16K33_I2C_TIMEOUT` (100 ms).

## Hardware smoke test

`App/Test/ht16k33_test.c` provides:

```c
void HT16K33_Test_Init(HT16K33_HandleTypeDef *hdev);
uint8_t HT16K33_Test_Update(uint32_t now_ms);  /* returns 1 when test complete */
```

Test sequence (non-blocking, driven by `HAL_GetTick`):
1. Light one pixel at a time (row sweep)
2. Light one full row at a time
3. Light one full column at a time
4. Clear — return complete

No `HAL_Delay()` — uses elapsed-time checks only.

## Folder structure after Task 1

```
App/
  Inc/
    ht16k33.h
  Src/
    ht16k33.c
  Test/
    ht16k33_test.c
```

## Checklist (from Darren's Tasks)

- Injectable I2C handle/address (not hard-coded `hi2c2`)
- Return `HAL_StatusTypeDef` to the caller
- Frame data independent from HAL (plain `uint8_t[8]`)
- Accepts an 8x8 bitmap and sends it to the HT16K33
- Hardware test for pixel/row/column sweep
- Uses official STM32 HAL I2C API and type conventions throughout
