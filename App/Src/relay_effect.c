#include "relay_effect.h"
#include <string.h>

static void SafeSet(Relay_HandleTypeDef *h, uint8_t high)
{
    if ((h == NULL) || (h->backend.set_output == NULL)) {
        return;
    }
    h->backend.set_output(h->backend.ctx, (uint8_t)((high != 0U) ? 1U : 0U));
}

void RelayEffect_Init(Relay_HandleTypeDef *h,
                      const Relay_Backend_t *backend,
                      const Relay_Config_t *cfg)
{
    if ((h == NULL) || (backend == NULL)) {
        return;
    }

    (void)memset(h, 0, sizeof(*h));
    h->backend = *backend;
    if (cfg != NULL) {
        h->cfg = *cfg;
    }

    /* Default LOW — no startup glitch into the relay module */
    SafeSet(h, 0U);
}

void RelayEffect_Trigger(Relay_HandleTypeDef *h, uint32_t now_ms)
{
    if (h == NULL) {
        return;
    }
    if (h->fired != 0U) {
        return;
    }

    h->fired = 1U;
    h->active = 1U;
    h->active_since_ms = now_ms;
    SafeSet(h, 1U);
}

void RelayEffect_Reset(Relay_HandleTypeDef *h)
{
    if (h == NULL) {
        return;
    }

    h->fired = 0U;
    h->active = 0U;
    h->active_since_ms = 0U;
    SafeSet(h, 0U);
}

void RelayEffect_Update(Relay_HandleTypeDef *h, uint32_t now_ms)
{
    if ((h == NULL) || (h->active == 0U) || (h->cfg.pulse_ms == 0U)) {
        return;
    }

    if ((uint32_t)(now_ms - h->active_since_ms) >= (uint32_t)h->cfg.pulse_ms) {
        h->active = 0U;
        SafeSet(h, 0U);
        /* fired stays set so countdown-zero cannot retrigger until Reset */
    }
}

uint8_t RelayEffect_IsActive(const Relay_HandleTypeDef *h)
{
    if (h == NULL) {
        return 0U;
    }
    return (uint8_t)((h->active != 0U) ? 1U : 0U);
}
