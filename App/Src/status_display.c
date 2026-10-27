#include "status_display.h"

#define STATUS_SCROLL_STEP_MS       120U
#define STATUS_BATTLE_SCROLL_MS      50U
#define STATUS_LAST_SCROLL_MS        50U
#define STATUS_ARMED_FLASH_MS        50U
#define STATUS_EXPLOSION_FRAME_MS    40U
#define STATUS_EXPLOSION_TOTAL_MS   400U
#define STATUS_LAST_BANNER_MS     10000U
/* Digits 10..0 inclusive → 11 s; flash half-period = MIN + val*STEP */
#define STATUS_COUNTDOWN_MS              11000U
#define STATUS_COUNTDOWN_FLASH_MIN_MS       40U
#define STATUS_COUNTDOWN_FLASH_STEP_MS      45U

typedef struct {
    char ch;
    uint8_t width;
    uint8_t cols[6];
} Glyph_t;

static const Glyph_t kGlyphs[] = {
    {' ', 3U, {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U}},
    {'0', 5U, {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU, 0x00U}},
    {'1', 3U, {0x42U, 0x7FU, 0x40U, 0x00U, 0x00U, 0x00U}},
    {'2', 5U, {0x42U, 0x61U, 0x51U, 0x49U, 0x46U, 0x00U}},
    {'3', 5U, {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U, 0x00U}},
    {'4', 5U, {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U, 0x00U}},
    {'5', 5U, {0x27U, 0x45U, 0x45U, 0x45U, 0x39U, 0x00U}},
    {'6', 5U, {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U, 0x00U}},
    {'7', 5U, {0x01U, 0x71U, 0x09U, 0x05U, 0x03U, 0x00U}},
    {'8', 5U, {0x36U, 0x49U, 0x49U, 0x49U, 0x36U, 0x00U}},
    {'9', 5U, {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU, 0x00U}},
    {'A', 5U, {0x7EU, 0x09U, 0x09U, 0x09U, 0x7EU, 0x00U}},
    {'B', 5U, {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U, 0x00U}},
    {'D', 5U, {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU, 0x00U}},
    {'E', 5U, {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U, 0x00U}},
    {'F', 5U, {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U, 0x00U}},
    {'L', 5U, {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U, 0x00U}},
    {'M', 5U, {0x7FU, 0x02U, 0x04U, 0x02U, 0x7FU, 0x00U}},
    {'O', 5U, {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU, 0x00U}},
    {'S', 5U, {0x26U, 0x49U, 0x49U, 0x49U, 0x32U, 0x00U}},
    {'T', 5U, {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U, 0x00U}}
};

static const uint8_t kExplosionFrames[][8] = {
    {0x18U, 0x3CU, 0x7EU, 0xDBU, 0xDBU, 0x7EU, 0x3CU, 0x18U},
    {0x81U, 0x42U, 0x24U, 0x18U, 0x18U, 0x24U, 0x42U, 0x81U}
};

static const char kBattleText[] = "BATTLE MODE";
static const char kLastText[] = "LAST OF EE2028";

static void Frame_Clear(uint8_t frame[8])
{
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
        frame[i] = 0U;
    }
}

static const Glyph_t *FindGlyph(char ch)
{
    uint32_t i;
    for (i = 0U; i < (sizeof(kGlyphs) / sizeof(kGlyphs[0])); i++) {
        if (kGlyphs[i].ch == ch) {
            return &kGlyphs[i];
        }
    }
    return &kGlyphs[0];
}

static uint16_t TextWidth(const char *text)
{
    uint16_t width = 0U;
    while (*text != '\0') {
        width = (uint16_t)(width + FindGlyph(*text)->width + 1U);
        text++;
    }
    return width;
}

static uint8_t TextColumn(const char *text, uint16_t idx, uint8_t repeat)
{
    uint16_t text_width = TextWidth(text);
    uint16_t walk = 0U;
    const Glyph_t *g;

    if (text_width == 0U) {
        return 0U;
    }

    if (repeat != 0U) {
        idx = (uint16_t)(idx % text_width);
    } else if (idx >= text_width) {
        return 0U;
    }

    while (*text != '\0') {
        g = FindGlyph(*text);
        if (idx < (uint16_t)(walk + g->width)) {
            return g->cols[idx - walk];
        }
        walk = (uint16_t)(walk + g->width);
        if (idx == walk) {
            return 0U;
        }
        walk++;
        text++;
    }
    return 0U;
}

static void Draw_Column(uint8_t frame[8], uint8_t col, uint8_t bits)
{
    uint8_t row;
    if (col > 7U) {
        return;
    }
    for (row = 0U; row < 8U; row++) {
        if (((bits >> row) & 0x01U) != 0U) {
            frame[row] |= (uint8_t)(1U << col);
        }
    }
}

static void Draw_ChargeBar(uint8_t frame[8], uint8_t charge)
{
    uint8_t cols;
    uint8_t c;
    Frame_Clear(frame);
    if (charge > 10U) {
        charge = 10U;
    }
    cols = (uint8_t)((charge * 8U + 5U) / 10U);
    for (c = 0U; c < cols; c++) {
        Draw_Column(frame, c, 0xFFU);
    }
}

static void Draw_Pixel(uint8_t frame[8], uint8_t col, uint8_t row)
{
    if ((col < 8U) && (row < 8U)) {
        frame[row] |= (uint8_t)(1U << col);
    }
}

static void Draw_Line(uint8_t frame[8], int8_t x0, int8_t y0, int8_t x1, int8_t y1)
{
    int8_t dx = (int8_t)((x1 >= x0) ? (x1 - x0) : (x0 - x1));
    int8_t sx = (x0 < x1) ? 1 : -1;
    int8_t dy = (int8_t)((y1 >= y0) ? (y0 - y1) : (y1 - y0));
    int8_t sy = (y0 < y1) ? 1 : -1;
    int8_t err = (int8_t)(dx + dy);
    int8_t e2;

    for (;;) {
        if ((x0 >= 0) && (x0 < 8) && (y0 >= 0) && (y0 < 8)) {
            Draw_Pixel(frame, (uint8_t)x0, (uint8_t)y0);
        }
        if ((x0 == x1) && (y0 == y1)) {
            break;
        }
        e2 = (int8_t)(err << 1);
        if (e2 >= dy) {
            err = (int8_t)(err + dy);
            x0 = (int8_t)(x0 + sx);
        }
        if (e2 <= dx) {
            err = (int8_t)(err + dx);
            y0 = (int8_t)(y0 + sy);
        }
    }
}

static void Draw_RadarLine(uint8_t frame[8], uint8_t azimuth)
{
    Frame_Clear(frame);
    if (azimuth < 8U) {
        Draw_Column(frame, azimuth, 0xFFU);
    }
}

/*
 * 180° fan dial: pivot at bottom-edge centre (cols 3-4, row 7).
 * Azimuth 0..7 sweeps left → up → right.
 */
static const int8_t kDialTips[8][2] = {
    {0, 6}, /* 0 leftmost  */
    {0, 3},
    {1, 1},
    {3, 0}, /* 3 near top-left  */
    {4, 0}, /* 4 near top-right */
    {6, 1},
    {7, 3},
    {7, 6}  /* 7 rightmost */
};

static void Draw_RadarDial(uint8_t frame[8], uint8_t azimuth)
{
    Frame_Clear(frame);

    /* Pivot hub on bottom edge centre */
    Draw_Pixel(frame, 3U, 7U);
    Draw_Pixel(frame, 4U, 7U);
    Draw_Pixel(frame, 3U, 6U);
    Draw_Pixel(frame, 4U, 6U);

    if (azimuth >= 8U) {
        return;
    }

    /* Thick needle from pivot to tip */
    Draw_Line(frame, 3, 7, kDialTips[azimuth][0], kDialTips[azimuth][1]);
    Draw_Line(frame, 4, 7, kDialTips[azimuth][0], kDialTips[azimuth][1]);
}

static void Draw_Radar(uint8_t frame[8], uint8_t azimuth, uint8_t style)
{
    if (style == (uint8_t)STATUS_RADAR_DIAL) {
        Draw_RadarDial(frame, azimuth);
    } else {
        Draw_RadarLine(frame, azimuth);
    }
}

static void Draw_Scroll(uint8_t frame[8],
                        const char *text,
                        uint16_t offset,
                        uint8_t repeat,
                        uint8_t leading_blank_cols)
{
    uint8_t col;
    Frame_Clear(frame);
    for (col = 0U; col < 8U; col++) {
        Draw_Column(frame,
                    col,
                    TextColumn(text, (uint16_t)(offset + col - leading_blank_cols), repeat));
    }
}

static void Draw_Digit(uint8_t frame[8], uint8_t digit)
{
    char ch = (char)('0' + (digit % 10U));
    const Glyph_t *g = FindGlyph(ch);
    uint8_t col;
    Frame_Clear(frame);
    for (col = 0U; col < g->width; col++) {
        Draw_Column(frame, (uint8_t)(1U + col), g->cols[col]);
    }
}

/* 10 packs as '1'+'0' (3+5 cols); 0..9 use centred single digit. */
static void Draw_CountdownDigit(uint8_t frame[8], uint8_t value)
{
    if (value >= 10U) {
        const Glyph_t *g1 = FindGlyph('1');
        const Glyph_t *g0 = FindGlyph('0');
        uint8_t col;
        Frame_Clear(frame);
        for (col = 0U; col < g1->width; col++) {
            Draw_Column(frame, col, g1->cols[col]);
        }
        for (col = 0U; col < g0->width; col++) {
            Draw_Column(frame, (uint8_t)(g1->width + col), g0->cols[col]);
        }
    } else {
        Draw_Digit(frame, value);
    }
}

static uint32_t CountdownFlashHalfMs(uint8_t countdown_val)
{
    return (uint32_t)STATUS_COUNTDOWN_FLASH_MIN_MS
         + ((uint32_t)countdown_val * (uint32_t)STATUS_COUNTDOWN_FLASH_STEP_MS);
}

static StatusScreen_t ResolveScreen(const StatusDisplay_HandleTypeDef *h)
{
    if ((h->explosion_active != 0U) || (h->terminated != 0U)) {
        return STATUS_SCREEN_EXPLOSION;
    }
    if (h->last_active != 0U) {
        return STATUS_SCREEN_LAST_2028;
    }
    if (h->battle_active != 0U) {
        return STATUS_SCREEN_BATTLE_TEXT;
    }
    if (h->armed != 0U) {
        return STATUS_SCREEN_ARMED;
    }
    if (h->charge > 0U) {
        return STATUS_SCREEN_CHARGE;
    }
    if (h->azimuth < 8U) {
        return STATUS_SCREEN_RADAR;
    }
    return STATUS_SCREEN_BLANK;
}

void StatusDisplay_Init(StatusDisplay_HandleTypeDef *h, HT16K33_HandleTypeDef *dev)
{
    if (h == NULL) {
        return;
    }

    h->dev = dev;
    h->charge = 0U;
    h->azimuth = 0xFFU;
    h->radar_style = (uint8_t)STATUS_RADAR_LINE;
    h->armed = 0U;
    h->battle_active = 0U;
    h->battle_start_ms = 0U;
    h->last_active = 0U;
    h->countdown_on = 0U;
    h->countdown_val = 10U;
    h->countdown_start_ms = 0U;
    h->explosion_active = 0U;
    h->explosion_start_ms = 0U;
    h->terminated = 0U;
    h->scroll_col = 0U;
    h->last_step_ms = 0U;
    h->last_drawn = STATUS_SCREEN_BLANK;
    h->dirty = 1U;
}

void StatusDisplay_SetCharge(StatusDisplay_HandleTypeDef *h, uint8_t charge_0_10)
{
    if (h == NULL) {
        return;
    }
    if (charge_0_10 > 10U) {
        charge_0_10 = 10U;
    }
    h->charge = charge_0_10;
    h->dirty = 1U;
}

void StatusDisplay_SetArmed(StatusDisplay_HandleTypeDef *h, uint8_t armed)
{
    if (h == NULL) {
        return;
    }
    h->armed = (armed != 0U) ? 1U : 0U;
    h->dirty = 1U;
}

void StatusDisplay_SetAzimuth(StatusDisplay_HandleTypeDef *h, uint8_t col_0_7)
{
    if (h == NULL) {
        return;
    }
    h->azimuth = (col_0_7 < 8U) ? col_0_7 : 0xFFU;
    h->dirty = 1U;
}

void StatusDisplay_SetRadarStyle(StatusDisplay_HandleTypeDef *h, StatusRadarStyle_t style)
{
    if (h == NULL) {
        return;
    }
    h->radar_style = (uint8_t)style;
    h->dirty = 1U;
}

void StatusDisplay_EnterBattle(StatusDisplay_HandleTypeDef *h)
{
    if (h == NULL) {
        return;
    }
    h->battle_active = 1U;
    h->last_active = 0U;
    h->scroll_col = 0U;
    h->dirty = 1U;
}

void StatusDisplay_EnterLastOf2028(StatusDisplay_HandleTypeDef *h)
{
    if (h == NULL) {
        return;
    }
    h->last_active = 1U;
    h->countdown_on = 0U;       /* banner phase first */
    h->countdown_val = 10U;
    h->countdown_start_ms = 0U;
    h->scroll_col = 0U;
    h->dirty = 1U;
}

void StatusDisplay_Rescue(StatusDisplay_HandleTypeDef *h)
{
    if (h == NULL) {
        return;
    }
    h->last_active = 0U;
    h->countdown_on = 0U;
    h->explosion_active = 0U;
    h->terminated = 0U;
    h->dirty = 1U;
    StatusDisplay_EnterBattle(h);
}

void StatusDisplay_TriggerExplosion(StatusDisplay_HandleTypeDef *h)
{
    if (h == NULL) {
        return;
    }
    h->explosion_active = 1U;
    h->last_active = 0U;
    h->countdown_on = 0U;
    h->dirty = 1U;
}

void StatusDisplay_Reset(StatusDisplay_HandleTypeDef *h)
{
    StatusDisplay_Init(h, (h == NULL) ? NULL : h->dev);
}

HAL_StatusTypeDef StatusDisplay_Update(StatusDisplay_HandleTypeDef *h, uint32_t now_ms)
{
    StatusScreen_t screen;
    HAL_StatusTypeDef status = HAL_OK;
    uint8_t frame[8];
    uint8_t draw_now = 0U;
    uint16_t battle_total_width;
    uint32_t elapsed_ms;

    if ((h == NULL) || (h->dev == NULL)) {
        return HAL_ERROR;
    }

    if (h->battle_active != 0U) {
        if (h->battle_start_ms == 0U) {
            h->battle_start_ms = now_ms;
        }
    } else {
        h->battle_start_ms = 0U;
    }

    if (h->last_active != 0U) {
        if (h->countdown_start_ms == 0U) {
            h->countdown_start_ms = now_ms;
        }
        elapsed_ms = now_ms - h->countdown_start_ms;

        if (h->countdown_on == 0U) {
            /* Phase 1: rolling LAST OF EE2028 banner for 10 s */
            if (elapsed_ms >= STATUS_LAST_BANNER_MS) {
                h->countdown_on = 1U;
                h->countdown_start_ms = now_ms;
                h->countdown_val = 10U;
                h->scroll_col = 0U;
                h->dirty = 1U;
            }
        } else {
            /* Phase 2: digits 10..0 (11 s), flash faster toward 0, then boom */
            if (elapsed_ms >= STATUS_COUNTDOWN_MS) {
                h->countdown_val = 0U;
                StatusDisplay_TriggerExplosion(h);
            } else {
                uint8_t next_val = (uint8_t)(10U - (elapsed_ms / 1000U));
                if (next_val != h->countdown_val) {
                    h->countdown_val = next_val;
                    h->dirty = 1U;
                }
            }
        }
    } else {
        h->countdown_start_ms = 0U;
    }

    if (h->explosion_active != 0U) {
        if (h->explosion_start_ms == 0U) {
            h->explosion_start_ms = now_ms;
        }
        if ((now_ms - h->explosion_start_ms) >= STATUS_EXPLOSION_TOTAL_MS) {
            h->explosion_active = 0U;
            h->terminated = 1U;
            h->dirty = 1U;
        }
    } else if (h->terminated == 0U) {
        h->explosion_start_ms = 0U;
    }

    screen = ResolveScreen(h);
    if (screen != h->last_drawn) {
        h->dirty = 1U;
        h->scroll_col = 0U;
    }

    if (h->dirty != 0U) {
        draw_now = 1U;
    }

    switch (screen) {
        case STATUS_SCREEN_EXPLOSION:
            if ((h->explosion_active != 0U) &&
                ((now_ms - h->last_step_ms) >= STATUS_EXPLOSION_FRAME_MS)) {
                draw_now = 1U;
            }
            if (draw_now != 0U) {
                if (h->explosion_active != 0U) {
                    if (((now_ms / STATUS_EXPLOSION_FRAME_MS) & 1U) == 0U) {
                        uint8_t i;
                        for (i = 0U; i < 8U; i++) {
                            frame[i] = kExplosionFrames[0][i];
                        }
                    } else {
                        uint8_t i;
                        for (i = 0U; i < 8U; i++) {
                            frame[i] = kExplosionFrames[1][i];
                        }
                    }
                } else {
                    Frame_Clear(frame);
                }
            }
            break;

        case STATUS_SCREEN_LAST_2028:
            if (h->countdown_on == 0U) {
                if ((now_ms - h->last_step_ms) >= STATUS_LAST_SCROLL_MS) {
                    h->scroll_col++;
                    draw_now = 1U;
                }
                if (draw_now != 0U) {
                    Draw_Scroll(frame, kLastText, h->scroll_col, 1U, 0U);
                }
            } else {
                uint32_t flash_ms = CountdownFlashHalfMs(h->countdown_val);
                if ((now_ms - h->last_step_ms) >= flash_ms) {
                    draw_now = 1U;
                }
                if (draw_now != 0U) {
                    if (((now_ms / flash_ms) & 1U) == 0U) {
                        Draw_CountdownDigit(frame, h->countdown_val);
                    } else {
                        Frame_Clear(frame);
                    }
                }
            }
            break;

        case STATUS_SCREEN_BATTLE_TEXT:
            if ((now_ms - h->last_step_ms) >= STATUS_BATTLE_SCROLL_MS) {
                h->scroll_col++;
                draw_now = 1U;
            }
            if (draw_now != 0U) {
                Draw_Scroll(frame, kBattleText, h->scroll_col, 0U, 8U);
                battle_total_width = (uint16_t)(TextWidth(kBattleText) + 8U);
                if (h->scroll_col > battle_total_width) {
                    h->battle_active = 0U;
                    h->scroll_col = 0U;
                    h->dirty = 1U;
                }
            }
            break;

        case STATUS_SCREEN_ARMED:
            if ((now_ms - h->last_step_ms) >= STATUS_ARMED_FLASH_MS) {
                draw_now = 1U;
            }
            if (draw_now != 0U) {
                uint8_t i;
                uint8_t on = (((now_ms / STATUS_ARMED_FLASH_MS) & 1U) != 0U) ? 1U : 0U;
                for (i = 0U; i < 8U; i++) {
                    frame[i] = (on != 0U) ? 0xFFU : 0x00U;
                }
            }
            break;

        case STATUS_SCREEN_CHARGE:
            if (draw_now != 0U) {
                Draw_ChargeBar(frame, h->charge);
            }
            break;

        case STATUS_SCREEN_RADAR:
            if (draw_now != 0U) {
                Draw_Radar(frame, h->azimuth, h->radar_style);
            }
            break;

        case STATUS_SCREEN_BLANK:
        default:
            if (draw_now != 0U) {
                Frame_Clear(frame);
            }
            break;
    }

    if (draw_now != 0U) {
        status = HT16K33_WriteFrame(h->dev, frame);
        h->last_step_ms = now_ms;
        h->last_drawn = screen;
        h->dirty = 0U;
    }

    return status;
}
