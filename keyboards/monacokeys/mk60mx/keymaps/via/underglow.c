// Copyright 2026 MonacoKeys
// SPDX-License-Identifier: GPL-2.0-or-later

// The 16 WS2812-2020s (LED_FLAG_UNDERGLOW) run their own effect, independent of
// the RGB Matrix effect on the keys. The settings are VIA custom-channel values
// 1-6, the Underglow menu in via/mk60mx.json and in the original MK60MX
// definition, kept in the user EEPROM datablock.

#include "quantum.h"
#include <lib/lib8tion/lib8tion.h>
#include "via.h"

typedef struct PACKED {
    uint8_t backlight; // key LEDs on
    uint8_t enable;    // underglow on
    uint8_t effect;
    uint8_t hue;
    uint8_t sat;
    uint8_t val;
    uint8_t speed;
} underglow_config_t;
_Static_assert(sizeof(underglow_config_t) == EECONFIG_USER_DATA_SIZE, "EECONFIG_USER_DATA_SIZE must match underglow_config_t");

enum underglow_value_id {
    id_underglow_enable = 1,
    id_backlight_enable,
    id_underglow_effect,
    id_underglow_brightness,
    id_underglow_color,
    id_underglow_speed,
};

// Numbered as in the VIA definition's underglow effect list.
enum underglow_effect {
    UG_SOLID,
    UG_BREATHING,
    UG_RAINBOW_MOVING_CHEVRON,
    UG_CYCLE_ALL,
    UG_CYCLE_LEFT_RIGHT,
    UG_HUE_BREATHING,
    UG_HUE_WAVE,
    UG_GRADIENT_LEFT_RIGHT,
    UG_DUAL_BEACON,
    UG_RIVERFLOW,
    UG_EFFECT_COUNT,
};

static const underglow_config_t underglow_defaults = {
    .backlight = 1,
    .enable    = 1,
    .effect    = UG_CYCLE_LEFT_RIGHT,
    .hue       = 0,
    .sat       = 255,
    .val       = 255,
    .speed     = 127,
};

static underglow_config_t underglow;
static bool               underglow_dirty; // redraw needed while RGB Matrix is off

// Defined in rgb_matrix.c, which has no header declarations for them. A QMK
// update that renames or removes either one breaks the build.
extern const led_point_t k_rgb_matrix_center;
rgb_t                    rgb_matrix_hsv_to_rgb(hsv_t hsv);

// QMK calls this on an EEPROM reset and, since QMK PR #26282, at boot whenever
// the block is missing or from another version, before keyboard_post_init_user.
// Without it the block would be zero-filled: key LEDs and underglow both off.
// Updating the block also marks it valid.
void eeconfig_init_user_datablock(void) {
    underglow = underglow_defaults;
    eeconfig_update_user_datablock(&underglow, 0, sizeof(underglow));
}

void keyboard_post_init_user(void) {
    // QMK before PR #26282 doesn't prepare the block at boot.
    if (!eeconfig_is_user_datablock_valid()) {
        eeconfig_init_user_datablock();
    }
    eeconfig_read_user_datablock(&underglow, 0, sizeof(underglow));
    underglow.backlight = underglow.backlight ? 1 : 0;
    underglow.enable    = underglow.enable ? 1 : 0;
    if (underglow.effect >= UG_EFFECT_COUNT) {
        underglow.effect = underglow_defaults.effect;
    }
}

// Each effect mirrors the RGB Matrix effect of the same name, driven by the
// underglow settings instead of rgb_matrix_config.
static hsv_t underglow_hsv(uint8_t i) {
    hsv_t       hsv  = {underglow.hue, underglow.sat, scale8(underglow.val, RGB_MATRIX_MAXIMUM_BRIGHTNESS)};
    led_point_t p    = g_led_config.point[i];
    uint8_t     time = scale16by8(g_rgb_timer, qadd8(underglow.speed / 4, 1));

    switch (underglow.effect) {
        case UG_BREATHING:
            hsv.v = scale8(abs8(sin8(time / 2) - 128) * 2, hsv.v);
            break;
        case UG_RAINBOW_MOVING_CHEVRON:
            hsv.h += abs8(p.y - k_rgb_matrix_center.y) + (p.x - time);
            break;
        case UG_CYCLE_ALL:
            hsv.h = time;
            break;
        case UG_CYCLE_LEFT_RIGHT:
            hsv.h = p.x - time;
            break;
        case UG_HUE_BREATHING:
            hsv.h += scale8(abs8(sin8(time / 2) - 128) * 2, 12);
            break;
        case UG_HUE_WAVE:
            hsv.h += scale8(abs8(p.x - time), 24);
            break;
        case UG_GRADIENT_LEFT_RIGHT:
            hsv.h += scale8(64, underglow.speed) * p.x >> 5;
            break;
        case UG_DUAL_BEACON: {
            uint16_t t         = scale16by8(g_rgb_timer, underglow.speed / 4);
            int8_t   cos_value = cos8(t) - 128;
            int8_t   sin_value = sin8(t) - 128;
            // effect_runner_sin_cos_i passes cos as DUAL_BEACON_math's "sin" argument and vice versa.
            hsv.h += ((p.y - k_rgb_matrix_center.y) * sin_value + (p.x - k_rgb_matrix_center.x) * cos_value) / 128;
            break;
        }
        case UG_RIVERFLOW: {
            uint8_t t = scale16by8(g_rgb_timer + (i * 315), underglow.speed / 8);
            hsv.v     = scale8(abs8(sin8(t) - 128) * 2, hsv.v);
            break;
        }
        default: // UG_SOLID
            break;
    }
    return hsv;
}

static void underglow_render(uint8_t led_min, uint8_t led_max) {
    for (uint8_t i = led_min; i < led_max; i++) {
        if (HAS_FLAGS(g_led_config.flags[i], LED_FLAG_UNDERGLOW)) {
            if (underglow.enable) {
                rgb_t rgb = rgb_matrix_hsv_to_rgb(underglow_hsv(i));
                rgb_matrix_set_color(i, rgb.r, rgb.g, rgb.b);
            } else {
                rgb_matrix_set_color(i, 0, 0, 0);
            }
        } else if (!underglow.backlight) {
            rgb_matrix_set_color(i, 0, 0, 0);
        }
    }
}

bool rgb_matrix_indicators_advanced_user(uint8_t led_min, uint8_t led_max) {
    underglow_render(led_min, led_max);
    return false;
}

void housekeeping_task_user(void) {
    // RGB Matrix stops rendering, and calling the indicator hooks, while it's
    // disabled. The underglow has its own on/off, so draw it here instead.
    static uint32_t last_draw;
    if (!rgb_matrix_is_enabled() && !rgb_matrix_get_suspend_state() && (underglow.enable || underglow_dirty) && timer_elapsed32(last_draw) >= RGB_MATRIX_LED_FLUSH_LIMIT) {
        last_draw = timer_read32();
        rgb_matrix_set_color_all(0, 0, 0);
        underglow_render(0, RGB_MATRIX_LED_COUNT);
        rgb_matrix_update_pwm_buffers();
        underglow_dirty = false;
    }
}

static bool underglow_set_value(uint8_t value_id, uint8_t *value_data) {
    switch (value_id) {
        case id_underglow_enable:
            underglow.enable = value_data[0] ? 1 : 0;
            break;
        case id_backlight_enable:
            underglow.backlight = value_data[0] ? 1 : 0;
            break;
        case id_underglow_effect:
            if (value_data[0] >= UG_EFFECT_COUNT) {
                return false;
            }
            underglow.effect = value_data[0];
            break;
        case id_underglow_brightness:
            underglow.val = value_data[0];
            break;
        case id_underglow_color:
            underglow.hue = value_data[0];
            underglow.sat = value_data[1];
            break;
        case id_underglow_speed:
            underglow.speed = value_data[0];
            break;
        default:
            return false;
    }
    underglow_dirty = true;
    return true;
}

static bool underglow_get_value(uint8_t value_id, uint8_t *value_data) {
    switch (value_id) {
        case id_underglow_enable:
            value_data[0] = underglow.enable;
            break;
        case id_backlight_enable:
            value_data[0] = underglow.backlight;
            break;
        case id_underglow_effect:
            value_data[0] = underglow.effect;
            break;
        case id_underglow_brightness:
            value_data[0] = underglow.val;
            break;
        case id_underglow_color:
            value_data[0] = underglow.hue;
            value_data[1] = underglow.sat;
            break;
        case id_underglow_speed:
            value_data[0] = underglow.speed;
            break;
        default:
            return false;
    }
    return true;
}

// The UG_* keycodes belong to RGB Light, which this keyboard doesn't have. QMK
// would apply them to the whole RGB Matrix instead, so take them here and apply
// them to the underglow. Shift reverses a key, as it does for the RM_* keycodes.
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode < QK_UNDERGLOW_TOGGLE || keycode > QK_UNDERGLOW_SPEED_DOWN) {
        return true;
    }
    if (!record->event.pressed) {
        return false;
    }

    bool up = !(get_mods() & MOD_MASK_SHIFT);
    switch (keycode) {
        case QK_UNDERGLOW_TOGGLE:
            underglow.enable = !underglow.enable;
            break;
        case QK_UNDERGLOW_MODE_PREVIOUS:
            up = !up;
            // fallthrough
        case QK_UNDERGLOW_MODE_NEXT:
            underglow.effect = (underglow.effect + (up ? 1 : UG_EFFECT_COUNT - 1)) % UG_EFFECT_COUNT;
            break;
        case QK_UNDERGLOW_HUE_DOWN:
            up = !up;
            // fallthrough
        case QK_UNDERGLOW_HUE_UP:
            underglow.hue += up ? RGB_MATRIX_HUE_STEP : -RGB_MATRIX_HUE_STEP;
            break;
        case QK_UNDERGLOW_SATURATION_DOWN:
            up = !up;
            // fallthrough
        case QK_UNDERGLOW_SATURATION_UP:
            underglow.sat = up ? qadd8(underglow.sat, RGB_MATRIX_SAT_STEP) : qsub8(underglow.sat, RGB_MATRIX_SAT_STEP);
            break;
        case QK_UNDERGLOW_VALUE_DOWN:
            up = !up;
            // fallthrough
        case QK_UNDERGLOW_VALUE_UP:
            underglow.val = up ? qadd8(underglow.val, RGB_MATRIX_VAL_STEP) : qsub8(underglow.val, RGB_MATRIX_VAL_STEP);
            break;
        case QK_UNDERGLOW_SPEED_DOWN:
            up = !up;
            // fallthrough
        case QK_UNDERGLOW_SPEED_UP:
            underglow.speed = up ? qadd8(underglow.speed, RGB_MATRIX_SPD_STEP) : qsub8(underglow.speed, RGB_MATRIX_SPD_STEP);
            break;
    }
    underglow_dirty = true;
    eeconfig_update_user_datablock(&underglow, 0, sizeof(underglow));
    return false;
}

// The keyboard doesn't define this hook, so the keymap can.
void via_custom_value_command_kb(uint8_t *data, uint8_t length) {
    // data = [ command_id, channel_id, value_id, value_data ]
    uint8_t *command_id = &data[0];
    uint8_t  channel_id = data[1];
    uint8_t  value_id   = data[2];
    uint8_t *value_data = &data[3];

    if (channel_id != id_custom_channel) {
        *command_id = id_unhandled;
        return;
    }
    switch (*command_id) {
        case id_custom_set_value:
            if (!underglow_set_value(value_id, value_data)) {
                *command_id = id_unhandled;
            }
            break;
        case id_custom_get_value:
            if (!underglow_get_value(value_id, value_data)) {
                *command_id = id_unhandled;
            }
            break;
        case id_custom_save:
            eeconfig_update_user_datablock(&underglow, 0, sizeof(underglow));
            break;
        default:
            *command_id = id_unhandled;
            break;
    }
}
