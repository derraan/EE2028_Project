/**
 * Emits a full mission through the REAL App/Src/telemetry_evt.c, in the same
 * order and with the same prose as Core/Src/main.c, so the exact bytes the
 * board puts on the ST-LINK COM port can be fed to VESPER's parser.
 *
 * On the host, telemetry_evt.c's weak Telemetry_WriteLine prints
 * "<line>\r\n" — the same framing telemetry_uart.c sends over USART1.
 *
 * Run via: node scripts/test_vesper_ingest.mjs
 */
#include <stdio.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "telemetry_evt.h"

int main(void)
{
    const float acc[3] = {0.1f, -0.2f, 9.8f};
    const float gyr[3] = {0.0f, 0.1f, -0.1f};
    const float mag[3] = {120.0f, -45.0f, 380.0f};
    int charge = 50;

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); /* keep \r\n exactly as on the wire */
#endif

    /* Boot (main.c) */
    Telemetry_Printf("Drone initialized: Entering STANDBY MODE (Laser Charge: %d/50)", charge);
    Telemetry_EvtMode(EVT_MODE_STANDBY);
    Telemetry_EvtCharge((uint8_t)charge);
    Telemetry_SendSensors(26.4f, 1008.2f, 55.0f, acc, gyr, mag);
    Telemetry_Printf("IR threat detected (enhancement only - no mode change).");

    /* Double press → Battle */
    Telemetry_Printf("PB Double Press (Standby) -> Entering BATTLE MODE. (Laser Charge: %d/50)", charge);
    Telemetry_EvtMode(EVT_MODE_BATTLE);

    /* Radar lock while in Battle (enhancements.c) */
    Telemetry_EvtRadar(-124, 1830U, 1U, -119, 1U, 2U);

    /* Earlier shots drained the gun to 30/50; two more shots, then recharge */
    charge = 30;
    charge -= 5;
    Telemetry_Printf("Laser gun fired continuously! PEW PEW! (Remaining charge: %d/50)", charge);
    Telemetry_EvtFire((uint8_t)charge);
    charge -= 5;
    Telemetry_Printf("Laser gun fired continuously! PEW PEW! (Remaining charge: %d/50)", charge);
    Telemetry_EvtFire((uint8_t)charge);
    Telemetry_Printf("Laser charge too low (%d/50) to fire! Need at least 25. Please single-press to recharge the laser gun.", charge);
    charge += 3;
    Telemetry_Printf("Laser recharging... Current charge: %d/50", charge);
    Telemetry_EvtCharge((uint8_t)charge);
    Telemetry_EvtRadarLost();

    /* Giant cannon: overcharge to 10/50 on a locked giant, two hits (main.c + adapter order) */
    Telemetry_EvtRadar(31, 2500U, 1U, 31, 0U, 1U);
    charge = 50;
    Telemetry_Printf("Cannon charging... Current charge: %d/50%s", charge, " - GIANT CANNON READY, hold to fire!");
    Telemetry_EvtCharge((uint8_t)charge);
    charge -= 50;
    Telemetry_Printf("GIANT CANNON FIRED! KA-BOOM! (Remaining charge: %d/50)", charge);
    Telemetry_EvtCannon((uint8_t)charge);
    Telemetry_Printf("Giant hit! (%u/%u) - recharge to 10/50 to finish it.", 1U, 2U);
    Telemetry_EvtGiantHit(1U, 2U);
    Telemetry_EvtRadar(31, 2500U, 1U, 31, 1U, 1U); /* beam on */
    charge = 50;
    Telemetry_EvtCharge((uint8_t)charge);
    charge -= 50;
    Telemetry_Printf("GIANT CANNON FIRED! KA-BOOM! (Remaining charge: %d/50)", charge);
    Telemetry_EvtCannon((uint8_t)charge);
    Telemetry_Printf("GIANT DOWN! The giant collapses.");
    Telemetry_EvtGiantDown();
    Telemetry_EvtRadarLost();
    charge = 3;
    Telemetry_EvtCharge((uint8_t)charge);

    /* Flip → THE LAST OF EE2028, one failed rescue, then rescued */
    Telemetry_Printf("Drone is upside down! Entering THE LAST OF EE2028.");
    Telemetry_EvtMode(EVT_MODE_THE_LAST_OF_2028);
    Telemetry_EvtCountdown(10000U);
    Telemetry_Printf("Drone was attacked!");
    Telemetry_EvtAttack();
    Telemetry_EvtCountdown(9000U);
    Telemetry_Printf("Rescue attempt %d/5 (Double Press detected)...", 1);
    Telemetry_Printf("Drone is still upside down! Flip upright and double press to rescue.");
    Telemetry_Printf("Rescue attempt %d/5 (Double Press detected)...", 2);
    Telemetry_Printf("PB Rescue Action: Successfully reverted to Battle Mode.");
    Telemetry_EvtRescue();
    Telemetry_EvtMode(EVT_MODE_BATTLE);

    /* Flip again and wait it out → self-destruct */
    Telemetry_Printf("Drone is upside down! Entering THE LAST OF EE2028.");
    Telemetry_EvtMode(EVT_MODE_THE_LAST_OF_2028);
    Telemetry_EvtCountdown(10000U);
    Telemetry_Printf("Drone was attacked!");
    Telemetry_EvtAttack();
    Telemetry_EvtCountdown(1000U);
    Telemetry_Printf("%s! Drone self-destructing...", "10s elapsed without rescue");
    Telemetry_EvtSelfDestruct(EVT_REASON_TIMEOUT);
    Telemetry_Printf("Drone has made a noble sacrifice for mankind...");
    Telemetry_EvtTerminate();
    Telemetry_Printf("Cheerio!");
    return 0;
}
