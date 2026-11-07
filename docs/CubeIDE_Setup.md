# STM32CubeIDE Setup — install, link, build, flash

How to install this repository as a flashable STM32CubeIDE project for the **B-L4S5I-IOT01A**, without copying source files into the IDE workspace.

The repo holds application source only (`Core/Src/main.c`, `Core/Inc/drone.h`, `App/`). Startup code, HAL, CMSIS, the board BSP and the linker script come from the course **Assignment 2** CubeIDE project, which already matches how `main.c` is written (`HAL_Init()` + `BSP_*_Init()`, semihosting `printf`, no CubeMX-generated `MX_*_Init`).

**Status (2026-11-07):** STM32CubeIDE **1.19.0** (GNU Tools for STM32 13.3.rel1). Debug and Enhanced compile and link. The **Enhanced** build has been flashed and run on the board with the LED matrix and buzzer working. Radar / servo / laser hardware is not yet brought up.

**Steps at a glance**

1. Install CubeIDE and get the Assignment 2 project building (§2).
2. Clone this repo (§2).
3. Copy Assignment 2 into a new `EE2028_Project` and link the repo into it (§3.1–3.3).
4. Add the `_kill` stub and the Enhanced build configuration (§3.4–3.5).
5. Wire the Grove modules (§3.6).
6. Build (§4), then flash with the OpenOCD debug probe (§5).

---

## 1. How the pieces fit

```
STM32CubeIDE workspace                         git repo (C:\...\Projects\EE2028_Project)
EE2028_Project/  (copy of Assignment2)
├── Core/
│   ├── Inc/   main.h, stm32l4xx_hal_conf.h, stm32l4xx_it.h   (local)
│   │   └── drone.h ───────────── link ───────────►  Core/Inc/drone.h
│   ├── Src/   stm32l4xx_it.c, hal_msp.c, system_*.c,          (local)
│   │          sysmem.c, syscalls.c, rdimon_stubs.c           (local)
│   │   └── main.c ────────────── link ───────────►  Core/Src/main.c
│   └── Startup/startup_stm32l4s5vitx.s                       (local)
├── Drivers/   HAL, CMSIS, BSP/B-L4S5I-IOT01 + Components     (local)
├── App/ ───────────────────────── link ───────────►  App/   (App/Test excluded)
├── STM32L4S5VITX_FLASH.ld                                    (local)
└── Debug/ Enhanced/ Release/   build output, one folder per configuration
```

- **Linked** = the IDE builds the repo file directly. Edit in either place, it is the same file.
- **Local** = belongs to the IDE project only; `git pull` never touches it.

---

## 2. Prerequisites

- STM32CubeIDE 1.19.0 (other 1.x versions should work; the toolchain path in §8 changes).
- The course **Assignment 2** project (`Assignment2.zip`), already building and flashing on your board. It provides the BSP under `Drivers/BSP/B-L4S5I-IOT01`. In CubeIDE: **File → Import → General → Existing Projects into Workspace → Select archive file** → `Assignment2.zip`, then build and flash it once to prove the board, cable and ST-LINK work.
- Git, and a clone of this repo checked out on **`main`**:

  ```bash
  cd C:/Users/<you>/Projects
  git clone https://github.com/derraan/EE2028_Project.git
  cd EE2028_Project
  git switch main
  ```

  To try an unmerged PR on the board, switch this folder to the PR branch (`git switch <branch>`), test, then switch back to `main` after it merges.

> Keep the folder CubeIDE links to on `main`. CubeIDE builds whatever branch is checked out there. Do branch work in separate worktrees (`git worktree add ../EE2028_Project-<topic> -b <branch>`), merge via PR, then `git pull` in the main folder.

---

## 3. Create the project (one-time, ~10 minutes)

All steps are in the CubeIDE GUI so they work on any PC. Replace `<REPO>` with your clone path, using forward slashes (e.g. `C:/Users/you/Projects/EE2028_Project`).

### 3.1 Copy the Assignment 2 project

1. Import Assignment 2 if it is not already in the workspace: **File → Import → General → Existing Projects into Workspace**.
2. In **Project Explorer**, select `Assignment2` → **Ctrl+C**, **Ctrl+V** → name it **`EE2028_Project`**. Eclipse makes a full copy and renames the project; the original is untouched.
3. In the copy, **delete or rename** `Core/Src/main.c` (e.g. to `main.c.assignment2-template.txt`). The repo's `main.c` replaces it.
4. If the copy has `Debug/` or `Release/` folders from the old project, delete them; they are regenerated.

### 3.2 Link the repo sources

1. Right-click `Core/Src` → **New → File → Advanced ▸ → Link to file in the file system** → `<REPO>/Core/Src/main.c` → Finish.
2. Right-click `Core/Inc` → **New → File → Advanced ▸ → Link to file** → `<REPO>/Core/Inc/drone.h`.
3. Right-click the project → **New → Folder → Advanced ▸ → Link to alternate location** → `<REPO>/App`, folder name **`App`**.
4. Right-click `App/Test` → **Resource Configurations → Exclude from Build…** → tick **all** configurations. (These are bench sketches with their own entry points.)
5. Right-click `App` → **Properties → C/C++ General → Paths and Symbols → Source Location**: confirm `/EE2028_Project/App` is listed for every configuration (add it with **Add Folder…** if not).

`App/Src/telemetry_uart.c` (mission telemetry on the ST-LINK COM port) is part of the linked `App/Src`, so it builds automatically.

### 3.3 Include paths

**Project → Properties → C/C++ Build → Settings → MCU/MPU GCC Compiler → Include paths**, configuration **[All configurations]**, add:

```
"<REPO>/Core/Inc"
"<REPO>/App/Inc"
```

Keep the existing `../Core/Inc`, `../Drivers/...` entries. The repo code includes the BSP as `"../../Drivers/BSP/B-L4S5I-IOT01/..."`, which resolves through `../Core/Inc`, so no extra BSP path is needed.

Linked files do not exist on disk inside the project, so headers next to them are **not** found by relative path. That is why the repo's `Core/Inc` and `App/Inc` must be explicit include paths.

### 3.4 Add the `_kill` stub

The Assignment 2 Debug configuration excludes `syscalls.c` and links `librdimon` (semihosting). rdimon provides every syscall except `_kill`, so the linker falls back to a libnosys stub and prints a note that CubeIDE miscounts as **"Build Failed. 1 errors"** even though the `.elf` is fine.

Create `Core/Src/rdimon_stubs.c` (local to the IDE project, not the repo):

```c
/* rdimon_stubs.c - syscall stubs that librdimon (semihosting) does not provide. */
#include <errno.h>

__attribute__((weak)) int _kill(int pid, int sig)
{
	(void)pid;
	(void)sig;
	errno = EINVAL;
	return -1;
}
```

`weak` lets the Release configuration (which compiles `syscalls.c`) keep its own definition.

### 3.5 Build configurations

**Project → Build Configurations → Manage… → New…** → name **`Enhanced`**, **Copy settings from: Existing configuration → Debug**.

Then **Properties → C/C++ Build → Settings → MCU/MPU GCC Compiler → Preprocessor → Define symbols**, configuration **Enhanced only**, add:

```
EE_ENHANCEMENTS=1
EE_RADAR_AIM=1
```

Add the macros to the **compiler** defines, not the assembler ones.

| Configuration | Defines (besides `DEBUG USE_HAL_DRIVER STM32L4S5xx`) | What you get | Output |
|---|---|---|---|
| **Debug** | — | Mandatory baseline only; enhancements compiled out | `Debug/EE2028_Project.elf` (~34 KB text) |
| **Enhanced** | `EE_ENHANCEMENTS=1 EE_RADAR_AIM=1` | Baseline + matrix/buzzer/IR/relay adapter + RD-03D radar + pan-servo laser | `Enhanced/EE2028_Project.elf` (~60 KB text) |
| Release | — | Template default (`-Os`, no `rdimon`). **Not usable as-is**: `main.c` calls `initialise_monitor_handles()`, which only `rdimon` provides, so linking fails with an undefined reference. Use Debug / Enhanced | — |

Other tuning macros (servo trim, radar mirroring, laser offsets, matrix I2C handle) are listed in `docs/Radar_Aim_Design.md`, `README.md` (Build macros) and at the top of `App/Src/enhancements_hw.c`. Add them to the same Define symbols list when needed, e.g. `EE_SERVO_CENTER_US=1520`.

### 3.6 Wiring (Enhanced build)

No CubeMX pin setup is needed: the firmware configures every pin below itself. Every module is optional: a missing one is skipped and the drone still runs.

| Module | Board pin | MCU pin / peripheral | Notes |
|---|---|---|---|
| Grove LED matrix (HT16K33) | **D15** SCL, **D14** SDA | PB8 / PB9, I2C1 (AF4) | Address 0x70. Brought up by `Enhancements_HW_MatrixI2C()` |
| Grove buzzer (active) | **D2** | PD14 | ON/OFF rhythms only |
| Grove IR reflective sensor | **D3** | PB0 | Active-LOW, internal pull-up |
| Relay control (CAP_EXPLODE) | **D4** | PA3 | Goes HIGH once at self-destruct. Switch only an instructor-approved low-voltage load |
| RD-03D radar TX → | **D0** | PA1, UART4_RX (AF8) | 256000 8N1, 3.3 V logic. Radar VCC = **5 V** |
| RD-03D radar RX ← | **D1** | PA0, UART4_TX (AF8) | Firmware sends the multi-target command at boot |
| Pan servo signal | **D5** | PB4, TIM3_CH1 (AF2) | 50 Hz. Power the servo from its **own 5 V** supply, common GND |
| Laser module | **D7** | PA4 | Through an NPN/MOSFET switch, active-HIGH, driven LOW at boot |

Share GND between the board and every module. On-board sensors stay on the BSP's I2C2 (PB10/PB11) and are not affected.

> **RD-03E is not supported.** The radar code parses **RD-03D** frames and needs azimuth. An RD-03E (presence + distance only) wired to D0/D1 is ignored by the parser. Leave its RX unconnected so it does not receive the RD-03D boot command. RD-03E ranging is planned separately (Task 6a in `docs/Darren's_Tasks.md`).

---

## 4. Build

1. Right-click `EE2028_Project` → **Build Configurations → Set Active → Debug** (or **Enhanced**).
2. **Project → Build Project** (hammer icon).
3. Console should end with `Build Finished. 0 errors`. Expected warnings are listed in §7.

Each configuration builds into its own folder, created on first build. `Enhanced/EE2028_Project.elf` does not exist until you build Enhanced. **Never rename one `.elf` to another**: they are different programs.

---

## 5. Flash and run

1. Connect the board's **ST-LINK** USB port.
2. **Run → Debug Configurations… → STM32 C/C++ Application**. Select the `EE2028_Project` launch, or right-click `Assignment2 Debug` → **Duplicate** and rename it.
3. **Main** tab:
   - Project: `EE2028_Project`
   - C/C++ Application: `Debug/EE2028_Project.elf` or `Enhanced/EE2028_Project.elf` (**Search Project…** finds it once built)
   - Build configuration: the matching one, or **Use Active**
4. **Debugger** tab: **Debug probe = ST-LINK (OpenOCD)**, Autostart local GDB server on port **3333**, Configuration script **Automated generation**, SWD, **Connect under reset**. These are the Assignment 2 settings. **Do not use "ST-LINK GDB server"**: it cannot do semihosting (see §7).
   **Startup** tab: Initialization Commands must contain `monitor arm semihosting enable`.
5. **Debug** → it builds, flashes, halts at `main` → **Resume (F8)**.

Tip: duplicate the launch once per configuration (`EE2028_Project Debug`, `EE2028_Project Enhanced`) so switching is one click.

### Telemetry → VESPER / Tera Term

All mission lines (prose, sensor lines, `EVT …`) go to the **ST-LINK Virtual COM port** (USART1, PB6/PB7) at **115200 8N1**, from `App/Src/telemetry_uart.c`. Both Debug and Enhanced builds do this; nothing to configure.

1. Flash with **Debug** as above, then either keep the session running or stop it: the board runs on its own from USB power (semihosting starts only when a debugger is attached).
2. Find the port: Windows **Device Manager → Ports (COM & LPT) → STMicroelectronics STLink Virtual COM Port (COMn)**.
3. **VESPER:** `node Command/serve.mjs` → open http://localhost:3000 in **Chrome or Edge** → **Connect board** → pick that COM port. The toast shows `Board connected @ 115200`.
4. Or **Tera Term / PuTTY**: same COM port, 115200 8N1.

Only one program can hold the COM port: close Tera Term before connecting VESPER. Plain `printf` (debug notes) still goes to the CubeIDE Console over semihosting, only while debugging; mission text never uses it.

### First-flash checks

**Debug (baseline)** — from `docs/Next_Steps.md`: prove LED2, the USER button and the `Drone initialized: Entering STANDBY MODE` line plus a `Telemetry -> T:…` line every second on the COM port (VESPER or Tera Term) before flashing **Enhanced** or wiring Grove hardware.

**Enhanced** — with the matrix and buzzer wired (§3.6):

| Action | Matrix | Buzzer |
|---|---|---|
| Boot (Standby) | Steady charge bar (full at 10/10) | Silent |
| Double press → Battle | `BATTLE MODE` scrolls once | Three rising beeps |
| In Battle, charge ≥ 5 | Charge bar **blinks** | — |
| Hold button with charge < 5 | — | Rough "brrr" (refused) |
| Single press (recharge +3) | Bar grows | Short blip |
| Hold to fire | Bar shrinks by 5 | "Pew" per shot |
| Flip upside down | `THE LAST OF EE2028` scrolls, then digits 5…0 flash | Beeps that speed up |
| Double press while still upside down | — | 300 ms error tone, then beeps resume |
| Upright + double press (rescue) | `BATTLE MODE` scrolls | Two blips + long "saved" tone |
| Wait out 10 s / 5 failed rescues | Explosion, then blank | Final burst, then silent. Relay D4 HIGH |
| Every 5 s (Enhanced) | — (telemetry: `Radar link: rx=… frames ok=…`, see `docs/Radar_Aim_Design.md` bring-up step 2) | — |
| RD-03D locks a person (when fitted) | Standby: dial needle. Battle: line at their azimuth (blinks if armed) | Threat chirp in Battle |

If text on the matrix is mirrored or upside down, note which and see `docs/LED_Matrix_Design.md` §3.1.

---

## 6. Day-to-day workflow

| You change… | Reaches CubeIDE? |
|---|---|
| `Core/Src/main.c`, `Core/Inc/drone.h` | Yes (linked) |
| Anything in `App/Src`, `App/Inc`, including new files | Yes (folder link) |
| New file in repo `Core/Src` or `Core/Inc` | **No.** Link it (§3.2) or put it under `App/` |
| `App/Test/*` | No (excluded on purpose) |
| HAL config, `stm32l4xx_it.c`, startup, linker script, BSP | Local to the IDE project. Not in git |

- After `git pull`, switching branches, or edits made outside the IDE: press **F5** on the project, then build (use **Project → Clean…** first if files were added or removed). Optional: **Window → Preferences → General → Workspace → Refresh using native hooks or polling**.
- If a file open in the editor changed on disk, reload it when asked. Do not save over it.
- Saving a linked file in CubeIDE edits the repo file; it shows in `git status`.
- Back up before big changes: copy the repo folder and the workspace project folder elsewhere (outside `Projects/` and the workspace).

---

## 7. Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| `Build Failed. 1 errors` but the `.elf` and size table were produced; Problems shows `libc_nano.a(libc_a-signalr.o): note: the message above does not take linker garbage collection into account` | Linker note about the missing `_kill` syscall, miscounted as an error | Add `rdimon_stubs.c` (§3.4), **F5**, Clean, rebuild |
| `multiple definition of 'buzzer_…'` / `HT16K33_…` etc. | The same driver is compiled twice: an old copy in `Core/Src` and the linked `App/Src` | Keep drivers only in `App/`. Delete or exclude the copies in the IDE project |
| `multiple definition of 'main'` | `App/Test` or the template `main.c` still in the build | §3.1 step 3 and §3.2 step 4 |
| `drone.h` / `enhancements.h: No such file` | Include paths missing | §3.3, for **All configurations** |
| `Enhanced/EE2028_Project.elf` not found when browsing | Enhanced not built yet | Set Enhanced active, build, F5 |
| Warnings `"THRESHOLD_A/T/G" redefined` | `main.c` redefines thresholds also set in `drone.h`; `main.c` values (T 50, A 20, G 150) are what runs | Known code issue; harmless until the two are reconciled |
| Warning `LOAD segment with RWX permissions` | GCC 13 note on ST linker scripts | Harmless |
| Error Log: `com.st.stm32cube.ide.mpu.remote.serial … Failed to parse wmic output` | Recent Windows 11 removed `wmic`; the MPU serial-port scanner logs this | Harmless for STM32L4. Not a build error |
| Launch fails: `Failed to execute MI command: monitor arm semihosting enable … Protocol error with Rcmd` | Debug probe set to **ST-LINK GDB server**, which does not understand this OpenOCD command (CubeIDE can switch the probe when a launch is edited or created with **Debug As**) | Debugger tab → **ST-LINK (OpenOCD)**, port 3333, Automated script (§5 step 4) |
| `undefined reference to 'initialise_monitor_handles'` | Building **Release**, which does not link `rdimon` | Use Debug or Enhanced (§3.5) |
| VESPER / Tera Term shows nothing | Wrong COM port, another program holds it, or old firmware without `telemetry_uart.c` | Pick the **STLink Virtual COM Port**, close other terminals, F5 + rebuild + flash. Reset the board (black button) to see the boot lines again |
| VESPER **Connect board** does nothing | Browser without Web Serial | Use Chrome or Edge on http://localhost:3000 |
| Matrix stays dark on Enhanced | Not on D15/D14, no power/GND, or the build is **Debug** (enhancements compiled out) | Check §3.6 wiring and that the **Enhanced** `.elf` was flashed. The firmware skips a matrix that does not answer at 0x70 |
| No buzzer / matrix changes after `git pull` | CubeIDE did not see the new files | **F5**, **Project → Clean…**, rebuild |
| Radar never locks with an RD-03E | Wrong module: the parser is for RD-03D | See the RD-03E note in §3.6 |

---

## 8. Building outside the IDE (optional check)

CubeIDE generates normal makefiles. After one IDE build you can rebuild from a shell to see the raw compiler output:

```bash
T=/c/ST/STM32CubeIDE_1.19.0/STM32CubeIDE/plugins
export PATH="$T/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.0.202411081344/tools/bin:$T/com.st.stm32cube.ide.mcu.externaltools.make.win32_2.2.0.202409170845/tools/bin:$PATH"
cd <workspace>/EE2028_Project/Enhanced
make -j8 all
```

`make` exit code 0 means the build succeeded regardless of what the IDE counter says.

---

## 9. Not done yet

- **Radar hardware bring-up** (RD-03D). Servo direction/trim, radar mirroring, parallax and laser-off-at-boot checks: `docs/Radar_Aim_Design.md`.
- The project links the repo by absolute path. If the repo moves, update the links (**Properties → Resource → Linked Resources**) and the two include paths.

---

## Appendix — reference setup (Darren's PC)

| Item | Location |
|---|---|
| Repo (CubeIDE source, keep on `main`) | `C:\Users\darre\Projects\EE2028_Project` |
| Feature worktrees | `C:\Users\darre\Projects\EE2028_Project-*` |
| CubeIDE workspace | `C:\Users\darre\STM32CubeIDE\workspace_1.19.0` |
| Flashable project | `workspace_1.19.0\EE2028_Project` (copied from `Assignment2.zip_expanded\Assignment2`) |
| Old bench project (per-driver tests, copy-based) | `workspace_1.19.0\EE2028_Project_Test`. Not the flashing target |
| Repo backup before the 2026-11-07 sync | `C:\Users\darre\Backups\EE2028_Project_2026-11-07` |

This project was created by editing `.project` / `.cproject` directly rather than through the GUI. The result is equivalent to §3. The launch file was renamed from `Assignment2` to `EE2028_Project`; it uses **ST-LINK (OpenOCD)** with an automatically generated `EE2028_Project.cfg` and flashes `Enhanced/EE2028_Project.elf`. `rdimon_stubs.c` is in the project's own `Core/Src`.
