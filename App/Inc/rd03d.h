/**
 * @file rd03d.h
 * @brief Ai-Thinker RD-03D (24 GHz mmWave) multi-target frame parser.
 *
 * Pure C (stdint only) so it runs in host tests and on the STM32 alike.
 * Transport (UART4 @ 256000 8N1 on the board) lives in enhancements_hw.c.
 *
 * Report frame (radar → host), 30 bytes, ~10 Hz:
 *   AA FF 03 00 | target[3] × 8 bytes | 55 CC
 *   target: x LE16 | y LE16 | speed LE16 | resolution LE16
 *
 * x / y / speed use sign-magnitude with an INVERTED sign bit:
 *   bit15 = 1 → positive, bit15 = 0 → negative, magnitude = bits 0..14.
 * Units: x, y in mm (y = boresight / forward, x = lateral); speed in cm/s;
 * resolution in mm. An empty slot is all zeros.
 *
 * Command frame (host → radar):
 *   FD FC FB FA | len LE16 | cmd LE16 | data… | 04 03 02 01
 *   single-target tracking cmd 0x0080, multi-target 0x0090 (len = 2).
 */
#ifndef RD03D_H
#define RD03D_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RD03D_BAUD            256000UL
#define RD03D_MAX_TARGETS     3U
#define RD03D_FRAME_LEN       30U
#define RD03D_CMD_LEN         12U

#define RD03D_CMD_SINGLE_TARGET 0x0080U
#define RD03D_CMD_MULTI_TARGET  0x0090U

typedef struct {
    int16_t x_mm;       /* lateral, sign per RadarTrack_Config_t.mirror_x */
    int16_t y_mm;       /* forward (boresight) */
    int16_t speed_cms;  /* radial speed; sign is the module's convention */
    uint16_t res_mm;    /* distance resolution / gate size */
    uint8_t valid;      /* 0 = empty slot or sentinel */
} RD03D_Target_t;

typedef struct {
    RD03D_Target_t target[RD03D_MAX_TARGETS];
    uint8_t count;      /* number of valid slots */
} RD03D_Frame_t;

typedef struct {
    uint8_t buf[RD03D_FRAME_LEN];
    uint8_t len;
    uint32_t frames_ok;
    uint32_t frames_bad; /* header matched but footer did not */
} RD03D_Parser_t;

/** Byte transport for the radar UART. NULL members are safe no-ops. */
typedef struct {
    /** Copy up to max received bytes into buf; return count (non-blocking). */
    uint16_t (*read)(void *ctx, uint8_t *buf, uint16_t max);
    /** Blocking write of a short command frame. */
    void (*write)(void *ctx, const uint8_t *buf, uint16_t len);
    void *ctx;
} RD03D_Transport_t;

void RD03D_ParserInit(RD03D_Parser_t *p);

/**
 * Feed one received byte. Returns 1 and fills *out when a complete frame with
 * a valid footer has just been decoded; 0 otherwise. Resynchronises on noise.
 */
uint8_t RD03D_PushByte(RD03D_Parser_t *p, uint8_t byte, RD03D_Frame_t *out);

/** Decode one sign-magnitude field (bit15 set = positive). */
int16_t RD03D_DecodeSigned(uint8_t lo, uint8_t hi);

/** Decode a full 30-byte frame (header/footer already checked by caller). */
void RD03D_DecodeFrame(const uint8_t frame[RD03D_FRAME_LEN], RD03D_Frame_t *out);

/** Build the 12-byte single (multi=0) / multi-target (multi=1) mode command. */
uint8_t RD03D_BuildTrackingModeCmd(uint8_t out[RD03D_CMD_LEN], uint8_t multi);

#ifdef __cplusplus
}
#endif

#endif /* RD03D_H */
