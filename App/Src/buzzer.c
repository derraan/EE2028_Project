#include "buzzer.h"

#define BUZZER_COUNTDOWN_ON_MS          60U
#define BUZZER_COUNTDOWN_GAP_BASE_MS    60U
#define BUZZER_COUNTDOWN_GAP_STEP_MS    80U

/*
 * Active Grove buzzer: fixed pitch. Patterns are ON/OFF rhythms only.
 * freq_hz kept as 0 (backend ignores it for GPIO/active modules).
 */
typedef struct {
    const BuzzerStep_t *steps;
    uint8_t count;
    uint8_t loop;
} BuzzerPatternDef_t;

/* Double tap */
static const BuzzerStep_t kThreatChirp[] = {
    {1U, 0U, 60U},
    {0U, 0U, 70U},
    {1U, 0U, 60U},
    {0U, 0U,  0U}
};

/* "Pew" — three stutters then a longer hold */
static const BuzzerStep_t kLaserFire[] = {
    {1U, 0U,  40U},
    {0U, 0U,  30U},
    {1U, 0U,  40U},
    {0U, 0U,  30U},
    {1U, 0U,  40U},
    {0U, 0U,  30U},
    {1U, 0U, 180U},
    {0U, 0U,   0U}
};

/* 2 Hz alarm */
static const BuzzerStep_t kLastWarning[] = {
    {1U, 0U, 250U},
    {0U, 0U, 250U}
};

/* Accelerating pulse train into a long final tone */
static const BuzzerStep_t kFinalBurst[] = {
    {1U, 0U, 80U},
    {0U, 0U, 60U},
    {1U, 0U, 70U},
    {0U, 0U, 45U},
    {1U, 0U, 55U},
    {0U, 0U, 30U},
    {1U, 0U, 40U},
    {0U, 0U, 20U},
    {1U, 0U, 30U},
    {0U, 0U, 15U},
    {1U, 0U, 250U},
    {0U, 0U,   0U}
};

static const BuzzerPatternDef_t kPatternDefs[BUZZER_PATTERN_COUNT] = {
    {NULL,         0U, 0U}, /* SILENT */
    {kThreatChirp, 4U, 0U}, /* THREAT_CHIRP */
    {kLaserFire,   8U, 0U}, /* LASER_FIRE */
    {kLastWarning, 2U, 1U}, /* LAST_WARNING */
    {NULL,         0U, 1U}, /* COUNTDOWN (special-cased) */
    {kFinalBurst, 12U, 0U}  /* FINAL_BURST */
};

static uint8_t Buzzer_PatternPriority(BuzzerPattern_t pattern)
{
    switch (pattern) {
        case BUZZER_PATTERN_THREAT_CHIRP:
            return 1U;
        case BUZZER_PATTERN_LASER_FIRE:
            return 2U;
        case BUZZER_PATTERN_LAST_WARNING:
            return 3U;
        case BUZZER_PATTERN_COUNTDOWN:
            return 4U;
        case BUZZER_PATTERN_FINAL_BURST:
            return 5U;
        case BUZZER_PATTERN_SILENT:
        case BUZZER_PATTERN_COUNT:
        default:
            return 0U;
    }
}

static uint8_t Buzzer_IsLooping(BuzzerPattern_t pattern)
{
    return (uint8_t)((pattern == BUZZER_PATTERN_LAST_WARNING) ||
                     (pattern == BUZZER_PATTERN_COUNTDOWN));
}

static void Buzzer_Apply(Buzzer_HandleTypeDef *h, uint8_t on, uint16_t freq_hz)
{
    if (h->backend.set_tone == NULL) {
        return;
    }
    if ((h->output_on == on) && (h->output_freq_hz == freq_hz) && (h->dirty == 0U)) {
        return;
    }

    h->backend.set_tone(h->backend.ctx, on, freq_hz);
    h->output_on = on;
    h->output_freq_hz = freq_hz;
    h->dirty = 0U;
}

static void Buzzer_StartPattern(Buzzer_HandleTypeDef *h, BuzzerPattern_t pattern)
{
    h->active_pattern = pattern;
    h->step_index = 0U;
    h->step_start_ms = 0U;
    h->dirty = 1U;
}

static void Buzzer_ResumeBase(Buzzer_HandleTypeDef *h)
{
    if (h->latched_silent != 0U) {
        Buzzer_StartPattern(h, BUZZER_PATTERN_SILENT);
        return;
    }

    Buzzer_StartPattern(h, h->base_pattern);
}

void Buzzer_Init(Buzzer_HandleTypeDef *h, const Buzzer_Backend_t *backend)
{
    if ((h == NULL) || (backend == NULL)) {
        return;
    }

    h->backend = *backend;
    h->active_pattern = BUZZER_PATTERN_SILENT;
    h->base_pattern = BUZZER_PATTERN_SILENT;
    h->step_index = 0U;
    h->latched_silent = 0U;
    h->output_on = 0U;
    h->output_freq_hz = 0U;
    h->countdown_value = 10U;
    h->dirty = 1U;
    h->step_start_ms = 0U;
    Buzzer_Apply(h, 0U, 0U);
}

void Buzzer_Play(Buzzer_HandleTypeDef *h, BuzzerPattern_t pattern)
{
    uint8_t req_pri;
    uint8_t active_pri;

    if ((h == NULL) || (pattern >= BUZZER_PATTERN_COUNT)) {
        return;
    }
    if (h->latched_silent != 0U) {
        return;
    }

    if (Buzzer_IsLooping(pattern) != 0U || pattern == BUZZER_PATTERN_SILENT) {
        h->base_pattern = pattern;
        if ((Buzzer_IsLooping(h->active_pattern) != 0U) ||
            (h->active_pattern == BUZZER_PATTERN_SILENT)) {
            Buzzer_StartPattern(h, pattern);
        }
        return;
    }

    req_pri = Buzzer_PatternPriority(pattern);
    active_pri = Buzzer_PatternPriority(h->active_pattern);
    if (req_pri >= active_pri) {
        Buzzer_StartPattern(h, pattern);
    }
}

void Buzzer_SetCountdown(Buzzer_HandleTypeDef *h, uint8_t value_0_10)
{
    if (h == NULL) {
        return;
    }
    if (value_0_10 > 10U) {
        value_0_10 = 10U;
    }

    h->countdown_value = value_0_10;
    if (h->active_pattern == BUZZER_PATTERN_COUNTDOWN) {
        h->step_index = 0U;
        h->step_start_ms = 0U;
        h->dirty = 1U;
    }
}

void Buzzer_Stop(Buzzer_HandleTypeDef *h)
{
    if (h == NULL) {
        return;
    }

    h->base_pattern = BUZZER_PATTERN_SILENT;
    Buzzer_StartPattern(h, BUZZER_PATTERN_SILENT);
}

void Buzzer_Reset(Buzzer_HandleTypeDef *h)
{
    if (h == NULL) {
        return;
    }

    h->latched_silent = 0U;
    h->countdown_value = 10U;
    Buzzer_Stop(h);
}

static void Buzzer_UpdateCountdown(Buzzer_HandleTypeDef *h, uint32_t now_ms)
{
    uint32_t elapsed_ms;
    uint16_t duration_ms;
    uint8_t on;

    if (h->step_start_ms == 0U) {
        h->step_start_ms = now_ms;
        h->step_index = 0U;
        h->dirty = 1U;
    }

    on = (uint8_t)((h->step_index == 0U) ? 1U : 0U);
    duration_ms = (uint16_t)((h->step_index == 0U)
                                 ? BUZZER_COUNTDOWN_ON_MS
                                 : (BUZZER_COUNTDOWN_GAP_BASE_MS +
                                    ((uint16_t)h->countdown_value *
                                     BUZZER_COUNTDOWN_GAP_STEP_MS)));

    elapsed_ms = now_ms - h->step_start_ms;
    if (elapsed_ms >= duration_ms) {
        h->step_index ^= 1U;
        h->step_start_ms = now_ms;
        h->dirty = 1U;
        on = (uint8_t)((h->step_index == 0U) ? 1U : 0U);
    }

    /* Active buzzer: rhythm only (freq unused). */
    Buzzer_Apply(h, on, 0U);
}

static void Buzzer_UpdateTablePattern(Buzzer_HandleTypeDef *h,
                                      uint32_t now_ms,
                                      BuzzerPattern_t pattern)
{
    const BuzzerPatternDef_t *def = &kPatternDefs[pattern];
    const BuzzerStep_t *step;
    uint32_t elapsed_ms;

    if ((def->steps == NULL) || (def->count == 0U)) {
        Buzzer_Apply(h, 0U, 0U);
        return;
    }

    if (h->step_start_ms == 0U) {
        h->step_start_ms = now_ms;
        h->step_index = 0U;
        h->dirty = 1U;
    }

    if (h->step_index >= def->count) {
        h->step_index = 0U;
        h->step_start_ms = now_ms;
        h->dirty = 1U;
    }

    step = &def->steps[h->step_index];
    elapsed_ms = now_ms - h->step_start_ms;
    if ((step->duration_ms > 0U) && (elapsed_ms >= step->duration_ms)) {
        h->step_index++;
        h->step_start_ms = now_ms;

        if (h->step_index >= def->count) {
            if (def->loop != 0U) {
                h->step_index = 0U;
            } else {
                if (pattern == BUZZER_PATTERN_FINAL_BURST) {
                    h->latched_silent = 1U;
                    h->base_pattern = BUZZER_PATTERN_SILENT;
                }
                Buzzer_ResumeBase(h);
                return;
            }
        }

        step = &def->steps[h->step_index];
        h->dirty = 1U;
    }

    Buzzer_Apply(h, step->on, step->freq_hz);
}

void Buzzer_Update(Buzzer_HandleTypeDef *h, uint32_t now_ms)
{
    if (h == NULL) {
        return;
    }

    switch (h->active_pattern) {
        case BUZZER_PATTERN_SILENT:
            Buzzer_Apply(h, 0U, 0U);
            break;
        case BUZZER_PATTERN_COUNTDOWN:
            Buzzer_UpdateCountdown(h, now_ms);
            break;
        case BUZZER_PATTERN_THREAT_CHIRP:
        case BUZZER_PATTERN_LASER_FIRE:
        case BUZZER_PATTERN_LAST_WARNING:
        case BUZZER_PATTERN_FINAL_BURST:
            Buzzer_UpdateTablePattern(h, now_ms, h->active_pattern);
            break;
        case BUZZER_PATTERN_COUNT:
        default:
            Buzzer_Stop(h);
            break;
    }
}
