/**
 * @file rd03d.c
 * @brief RD-03D frame parser (see rd03d.h for the wire format).
 */
#include "rd03d.h"
#include <string.h>

static const uint8_t kHeader[4] = {0xAAU, 0xFFU, 0x03U, 0x00U};
static const uint8_t kFooter[2] = {0x55U, 0xCCU};
static const uint8_t kCmdHeader[4] = {0xFDU, 0xFCU, 0xFBU, 0xFAU};
static const uint8_t kCmdFooter[4] = {0x04U, 0x03U, 0x02U, 0x01U};

/* Speeds the module reports in idle / placeholder slots (ESPHome rd03d.cpp). */
#define RD03D_SPEED_SENTINEL_A 248
#define RD03D_SPEED_SENTINEL_B 256

void RD03D_ParserInit(RD03D_Parser_t *p)
{
    if (p == NULL) {
        return;
    }
    (void)memset(p, 0, sizeof(*p));
}

int16_t RD03D_DecodeSigned(uint8_t lo, uint8_t hi)
{
    int16_t mag = (int16_t)((((uint16_t)hi & 0x7FU) << 8) | (uint16_t)lo);
    return ((hi & 0x80U) != 0U) ? mag : (int16_t)(-mag);
}

void RD03D_DecodeFrame(const uint8_t frame[RD03D_FRAME_LEN], RD03D_Frame_t *out)
{
    uint8_t i;

    if ((frame == NULL) || (out == NULL)) {
        return;
    }
    out->count = 0U;
    for (i = 0U; i < RD03D_MAX_TARGETS; i++) {
        const uint8_t *b = &frame[4U + (i * 8U)];
        RD03D_Target_t *t = &out->target[i];
        int16_t abs_speed;

        t->x_mm = RD03D_DecodeSigned(b[0], b[1]);
        t->y_mm = RD03D_DecodeSigned(b[2], b[3]);
        t->speed_cms = RD03D_DecodeSigned(b[4], b[5]);
        t->res_mm = (uint16_t)(((uint16_t)b[7] << 8) | (uint16_t)b[6]);

        abs_speed = (t->speed_cms < 0) ? (int16_t)(-t->speed_cms) : t->speed_cms;
        /* Stationary people can legitimately report speed 0, so only the
         * placeholder sentinels and an all-zero position mark a slot empty. */
        t->valid = (uint8_t)((((t->x_mm != 0) || (t->y_mm != 0)) &&
                              (abs_speed != RD03D_SPEED_SENTINEL_A) &&
                              (abs_speed != RD03D_SPEED_SENTINEL_B))
                                 ? 1U
                                 : 0U);
        if (t->valid != 0U) {
            out->count++;
        }
    }
}

uint8_t RD03D_PushByte(RD03D_Parser_t *p, uint8_t byte, RD03D_Frame_t *out)
{
    if (p == NULL) {
        return 0U;
    }

    if (p->len < sizeof(kHeader)) {
        if (byte == kHeader[p->len]) {
            p->buf[p->len++] = byte;
        } else {
            /* Restart, but a stray 0xAA may itself begin the next header. */
            p->len = 0U;
            if (byte == kHeader[0]) {
                p->buf[p->len++] = byte;
            }
        }
        return 0U;
    }

    p->buf[p->len++] = byte;
    if (p->len < RD03D_FRAME_LEN) {
        return 0U;
    }

    p->len = 0U;
    if ((p->buf[RD03D_FRAME_LEN - 2U] != kFooter[0]) ||
        (p->buf[RD03D_FRAME_LEN - 1U] != kFooter[1])) {
        p->frames_bad++;
        return 0U;
    }
    p->frames_ok++;
    if (out != NULL) {
        RD03D_DecodeFrame(p->buf, out);
    }
    return 1U;
}

uint8_t RD03D_BuildTrackingModeCmd(uint8_t out[RD03D_CMD_LEN], uint8_t multi)
{
    uint16_t cmd = (multi != 0U) ? RD03D_CMD_MULTI_TARGET : RD03D_CMD_SINGLE_TARGET;

    if (out == NULL) {
        return 0U;
    }
    (void)memcpy(&out[0], kCmdHeader, sizeof(kCmdHeader));
    out[4] = 0x02U; /* payload length LE: command word only */
    out[5] = 0x00U;
    out[6] = (uint8_t)(cmd & 0xFFU);
    out[7] = (uint8_t)(cmd >> 8);
    (void)memcpy(&out[8], kCmdFooter, sizeof(kCmdFooter));
    return RD03D_CMD_LEN;
}
