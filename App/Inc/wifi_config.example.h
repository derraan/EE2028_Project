/**
 * @file wifi_config.example.h
 * @brief Template for Wi-Fi telemetry credentials.
 *
 * Copy this file to App/Inc/wifi_config.h (git-ignored — never commit it),
 * fill in your hotspot, rebuild. Without wifi_config.h the firmware runs
 * with UART telemetry only.
 *
 * The on-board ISM43362 is 2.4 GHz, WPA2-Personal only:
 *   - iPhone hotspot: Settings → Personal Hotspot → turn on "Maximize Compatibility"
 *   - Android hotspot: set AP band to 2.4 GHz
 *   - university WPA2-Enterprise networks do not work.
 */
#ifndef WIFI_CONFIG_H
#define WIFI_CONFIG_H

#define WIFI_SSID      "your-hotspot-name"
#define WIFI_PASSWORD  "your-hotspot-password"

/* Optional: TCP port VESPER's bridge connects to (default 5028). */
/* #define WIFI_TCP_PORT  5028U */

#endif /* WIFI_CONFIG_H */
