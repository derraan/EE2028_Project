/**
 * @file telemetry_evt.c
 * @brief EVT / sensor / mission-prose emitters for USART Virtual COM + VESPER.
 */
#include "telemetry_evt.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef TELEMETRY_LINE_MAX
#define TELEMETRY_LINE_MAX 256
#endif

/**
 * Fallback transport (printf) for host tests. On the board the strong
 * Telemetry_WriteLine in telemetry_uart.c sends lines to the ST-LINK COM port.
 */
__attribute__((weak)) void Telemetry_WriteLine(const char *line)
{
	if (line == NULL) {
		return;
	}
	printf("%s\r\n", line);
}

void Telemetry_Printf(const char *fmt, ...)
{
	char buf[TELEMETRY_LINE_MAX];
	va_list ap;
	int n;

	if (fmt == NULL) {
		return;
	}

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (n < 0) {
		return;
	}
	/* Drop a trailing newline if the caller left one in; WriteLine adds CRLF. */
	if ((n > 0) && (buf[n - 1] == '\n')) {
		buf[n - 1] = '\0';
		n--;
		if ((n > 0) && (buf[n - 1] == '\r')) {
			buf[n - 1] = '\0';
		}
	}
	Telemetry_WriteLine(buf);
}

void Telemetry_SendSensors(float t_c, float p_hpa, float h_pct,
                           const float accel_mps2[3],
                           const float gyro_dps[3],
                           const float magneto_mgauss[3])
{
	char buf[TELEMETRY_LINE_MAX];

	(void)snprintf(buf, sizeof(buf),
		"Telemetry -> T:%.1f °C, P:%.1f hPa, H:%.1f %% | "
		"Accel:[%.1f, %.1f, %.1f] m/s^2 | "
		"Gyro:[%.1f, %.1f, %.1f] dps | "
		"Magneto:[%.1f, %.1f, %.1f] mgauss",
		(double)t_c, (double)p_hpa, (double)h_pct,
		(double)accel_mps2[0], (double)accel_mps2[1], (double)accel_mps2[2],
		(double)gyro_dps[0], (double)gyro_dps[1], (double)gyro_dps[2],
		(double)magneto_mgauss[0], (double)magneto_mgauss[1], (double)magneto_mgauss[2]);
	Telemetry_WriteLine(buf);
}

void Telemetry_EvtMode(const char *mode_tag)
{
	char buf[TELEMETRY_LINE_MAX];
	(void)snprintf(buf, sizeof(buf), "EVT MODE=%s",
		mode_tag != NULL ? mode_tag : EVT_MODE_STANDBY);
	Telemetry_WriteLine(buf);
}

void Telemetry_EvtCharge(uint8_t charge_tenths)
{
	char buf[64];
	if (charge_tenths > 10U) {
		charge_tenths = 10U;
	}
	(void)snprintf(buf, sizeof(buf), "EVT CHARGE=%u/10", (unsigned)charge_tenths);
	Telemetry_WriteLine(buf);
}

void Telemetry_EvtFire(uint8_t charge_after_shot)
{
	char buf[64];
	if (charge_after_shot > 10U) {
		charge_after_shot = 10U;
	}
	(void)snprintf(buf, sizeof(buf), "EVT FIRE charge=%u/10", (unsigned)charge_after_shot);
	Telemetry_WriteLine(buf);
}

void Telemetry_EvtCountdown(uint32_t remaining_ms)
{
	char buf[64];
	(void)snprintf(buf, sizeof(buf), "EVT COUNTDOWN remaining_ms=%lu",
		(unsigned long)remaining_ms);
	Telemetry_WriteLine(buf);
}

void Telemetry_EvtSelfDestruct(const char *reason_token)
{
	char buf[96];
	(void)snprintf(buf, sizeof(buf), "EVT SELF_DESTRUCT reason=%s",
		reason_token != NULL ? reason_token : EVT_REASON_TIMEOUT);
	Telemetry_WriteLine(buf);
}

void Telemetry_EvtTerminate(void)
{
	Telemetry_WriteLine("EVT TERMINATE");
}

void Telemetry_EvtThresholdOk(void)
{
	Telemetry_WriteLine("EVT THRESHOLD_OK");
}

void Telemetry_EvtRescue(void)
{
	Telemetry_WriteLine("EVT RESCUE");
}

void Telemetry_EvtAttack(void)
{
	Telemetry_WriteLine("EVT ATTACK");
}

/* Fixed-point tenths → "-12.4" without relying on printf float support. */
static void FormatTenths(char *out, size_t n, int16_t tenths)
{
	int v = (int)tenths;
	const char *sign = (v < 0) ? "-" : "";
	if (v < 0) {
		v = -v;
	}
	(void)snprintf(out, n, "%s%d.%d", sign, v / 10, v % 10);
}

void Telemetry_EvtRadar(int16_t az_ddeg, uint16_t range_mm, uint8_t locked,
                        int16_t pan_ddeg, uint8_t laser_on, uint8_t contacts)
{
	char buf[112];
	char az[12];
	char pan[12];

	FormatTenths(az, sizeof(az), az_ddeg);
	FormatTenths(pan, sizeof(pan), pan_ddeg);
	(void)snprintf(buf, sizeof(buf),
		"EVT RADAR az=%s range_mm=%u lock=%u pan=%s laser=%u n=%u",
		az, (unsigned)range_mm, (unsigned)(locked != 0U), pan,
		(unsigned)(laser_on != 0U), (unsigned)contacts);
	Telemetry_WriteLine(buf);
}

void Telemetry_EvtRadarLost(void)
{
	Telemetry_WriteLine("EVT RADAR lost");
}
