#include "ir_threat.h"
#include <string.h>

static uint8_t SafeReadRaw(const IR_HandleTypeDef *h)
{
    if ((h == NULL) || (h->backend.read_raw == NULL)) {
        return 1U;
    }
    return (uint8_t)((h->backend.read_raw(h->backend.ctx) != 0U) ? 1U : 0U);
}

static uint8_t RawToPresent(const IR_HandleTypeDef *h, uint8_t raw)
{
    uint8_t active_level = (uint8_t)((h->cfg.active_level != 0U) ? 1U : 0U);
    return (uint8_t)((raw == active_level) ? 1U : 0U);
}

void IR_Init(IR_HandleTypeDef *h, const IR_Backend_t *backend, const IR_Config_t *cfg)
{
    uint8_t raw;

    if ((h == NULL) || (backend == NULL) || (cfg == NULL)) {
        return;
    }

    (void)memset(h, 0, sizeof(*h));
    h->backend = *backend;
    h->cfg = *cfg;
    h->cfg.active_level = (uint8_t)((h->cfg.active_level != 0U) ? 1U : 0U);

    raw = SafeReadRaw(h);
    h->last_raw = raw;
    h->present = RawToPresent(h, raw);
    h->armed = (uint8_t)((h->present == 0U) ? 1U : 0U);
}

void IR_Reset(IR_HandleTypeDef *h)
{
    uint8_t raw;

    if (h == NULL) {
        return;
    }

    h->event_pending = 0U;
    h->raw_since_ms = 0U;
    h->clear_since_ms = 0U;

    raw = SafeReadRaw(h);
    h->last_raw = raw;
    h->present = RawToPresent(h, raw);
    h->armed = (uint8_t)((h->present == 0U) ? 1U : 0U);
}

void IR_Update(IR_HandleTypeDef *h, uint32_t now_ms)
{
    uint8_t raw;
    uint8_t present_now;
    uint8_t changed = 0U;

    if (h == NULL) {
        return;
    }

    raw = SafeReadRaw(h);
    if (raw != h->last_raw) {
        if (h->raw_since_ms == 0U) {
            h->raw_since_ms = now_ms;
        } else if ((uint32_t)(now_ms - h->raw_since_ms) >= h->cfg.debounce_ms) {
            h->last_raw = raw;
            h->raw_since_ms = 0U;
            changed = 1U;
        }
    } else {
        h->raw_since_ms = 0U;
    }

    present_now = RawToPresent(h, h->last_raw);
    if ((changed != 0U) && (present_now != h->present)) {
        h->present = present_now;
        if (h->present != 0U) {
            if (h->armed != 0U) {
                h->event_pending = 1U;
                h->armed = 0U;
            }
            h->clear_since_ms = 0U;
        } else {
            h->clear_since_ms = now_ms;
        }
    }

    if ((h->present == 0U) && (h->armed == 0U) && (h->clear_since_ms != 0U)) {
        if ((uint32_t)(now_ms - h->clear_since_ms) >= h->cfg.rearm_ms) {
            h->armed = 1U;
        }
    }
}

uint8_t IR_PollThreat(IR_HandleTypeDef *h)
{
    uint8_t pending;

    if (h == NULL) {
        return 0U;
    }

    pending = h->event_pending;
    h->event_pending = 0U;
    return pending;
}

uint8_t IR_IsPresent(const IR_HandleTypeDef *h)
{
    if (h == NULL) {
        return 0U;
    }
    return h->present;
}

uint8_t IR_ReadRaw(const IR_HandleTypeDef *h)
{
    return SafeReadRaw(h);
}
