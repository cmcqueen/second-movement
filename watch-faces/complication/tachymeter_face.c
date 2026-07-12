/*
 * MIT License
 *
 * Copyright (c) 2026 Craig McQueen
 * Based on fast_stopwatch face Copyright (c) 2022 Andreas Nebinger
 * and Copyright (c) 2025 Alessandro Genova
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <limits.h>
#include "tachymeter_face.h"
#include "watch.h"
#include "watch_common_display.h"
#include "watch_utility.h"
#include "watch_rtc.h"
#include "slcd.h"

#define TICK_FREQ_HZ   128u                   // Should be the same as watch_rtc_get_frequency()
#define ONE_HOUR_TICKS (3600u * TICK_FREQ_HZ)

// Loosely implement the watch as a state machine
typedef enum {
    TC_STATUS_IDLE = 0,
    TC_STATUS_RUNNING,
    TC_STATUS_RUNNING_LAPPING,
    TC_STATUS_STOPPED,
    TC_STATUS_STOPPED_LAPPING,

    TC_STATUS_SETTING_UNITS, // set the units for the distance (km, miles, etc.)
    TC_STATUS_SETTING_3,     // set thousands digit of distance
    TC_STATUS_SETTING_2,     // set hundreds digit of distance
    TC_STATUS_SETTING_1,     // set tens digit of distance
    TC_STATUS_SETTING_0,     // set ones digit of distance
} tachymeter_status_t;

typedef enum {
    TC_UNITS_KM,
    TC_UNITS_METERS,
    TC_UNITS_MILES,
    TC_UNITS_YARDS,
    TC_UNITS_FEET,
    TC_NUM_UNITS
} TC_UNITS_T;

typedef struct {
    rtc_counter_t seconds;
    rtc_counter_t minutes;
    rtc_counter_t hours;
} hms_t;

typedef struct {
    rtc_counter_t start_counter; // rtc counter when the tachymeter was started
    rtc_counter_t lap_counter;   // rtc counter when the tachymeter was lapped
    rtc_counter_t stop_counter;  // rtc counter when the tachymeter was stopped
    uint8_t result_ticks;        // tick counter for alternating between time and speed display
    uint32_t distance;           // distance in "units", either km or miles
    uint32_t speed_100;          // speed in 1/100 "units" per hour, either km/h or mph
    TC_UNITS_T units;            // units for the distance
    tachymeter_status_t status;  // the status the tachymeter is in (idle, running, stopped)
    bool slow_refresh;           // update the display slowly (same 128Hz timekeeping accuracy)
    bool scrolling;              // whether the digit setting is scrolling or not
    hms_t old_display;           // the digits currently being displayed on screen
} tachymeter_state_t;

static inline void _button_beep() {
    // play a beep as confirmation for a button press (if applicable)
    if (movement_button_should_sound()) watch_buzzer_play_note_with_volume(BUZZER_NOTE_C7, 50, movement_button_volume());
}

// How quickly should the elapsing time be displayed?
// This is just for looks, timekeeping is always accurate to 128Hz
static const uint8_t DISPLAY_RUNNING_RATE = 32;
static const uint8_t DISPLAY_RUNNING_RATE_SLOW = 2;

static const char * units_str(TC_UNITS_T units) {
    switch (units) {
        case TC_UNITS_KM:
            if (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM)
                return "KM";
            else
                return "K ";
        case TC_UNITS_METERS:
            return "M ";
        case TC_UNITS_MILES:
            return "MI";
        case TC_UNITS_YARDS:
            return "YD";
        case TC_UNITS_FEET:
            return "FT";
        default:
            return "--";
    }
}

static const char * units_result_str(TC_UNITS_T units) {
    switch (units) {
        case TC_UNITS_KM:
        case TC_UNITS_METERS:
            if (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM)
                return "KM";
            else
                return "K ";
        case TC_UNITS_MILES:
        case TC_UNITS_YARDS:
        case TC_UNITS_FEET:
            return "MI";
        default:
            return "--";
    }
}

static void calc_speed(tachymeter_state_t *state, uint32_t elapsed) {
    if (elapsed > 0 && state->distance > 0) {
        uint32_t distance = state->distance;
        uint32_t multiplier;
        switch (state->units) {
            case TC_UNITS_KM:
            case TC_UNITS_MILES:
            default:
                // Speed result is in km/h or mph, corresponding to the distance units.
                // Calculation is the same in both cases.
                multiplier = 100u * 3600u * TICK_FREQ_HZ;
                break;
            case TC_UNITS_METERS:
                // Speed calculation result is in km/h, so we need to divide by 1000.
                // + (1000u / 2u) is to round the multiplier to the nearest integer instead of truncating.
                multiplier = (100u * 3600u * TICK_FREQ_HZ + (1000u / 2u)) / 1000u;
                break;
            case TC_UNITS_YARDS:
                // Speed calculation result is in mph, so we need to divide by 1760 (yards per mile).
                // + (1760u / 2u) is to round the multiplier to the nearest integer instead of truncating.
                multiplier = (100u * 3600u * TICK_FREQ_HZ + (1760u / 2u)) / 1760u;
                break;
            case TC_UNITS_FEET:
                // Speed calculation result is in mph, so we need to divide by 5280 (feet per mile).
                // + (5280u / 2u) is to round the multiplier to the nearest integer instead of truncating.
                multiplier = (100u * 3600u * TICK_FREQ_HZ + (5280u / 2u)) / 5280u;
                break;
        }
        // If necessary, scale the distance and elapsed time down to avoid overflow in the calculation.
        uint32_t distance_max = UINT32_MAX / multiplier + 1u;
        while (distance > distance_max) {
            distance >>= 1;
            elapsed >>= 1;
        }
        // Calculate speed in 1/100 units per hour, rounded to the nearest integer.
        // + (elapsed / 2u) is to round the division to the nearest integer instead of truncating.
        state->speed_100 = (distance * multiplier + (elapsed / 2u)) / elapsed;
    } else {
        state->speed_100 = 0;
    }
}

static void _display_title(void) {
    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, "TCY", "TC");
}

/// @brief Display minutes, seconds and fractions derived from 128 Hz tick counter
///        on the lcd.
/// @param ticks
static void _display_elapsed(tachymeter_state_t *state, uint32_t ticks) {
    char buf[3];

    uint32_t seconds = ticks >> 7;

    if (ticks >= ONE_HOUR_TICKS) {
        // Display HH:MM:SS
        // Seconds
        if (seconds == state->old_display.seconds) {
            return;
        }
        state->old_display.seconds = seconds;
        sprintf(buf, "%02" PRIu32, seconds % 60);
        watch_display_text(WATCH_POSITION_SECONDS, buf);

        // Minutes
        uint32_t minutes = seconds / 60;
        if (minutes == state->old_display.minutes) {
            return;
        }
        state->old_display.minutes = minutes;
        sprintf(buf, "%02" PRIu32, minutes % 60);
        watch_display_text(WATCH_POSITION_MINUTES, buf);

        // Hours
        uint32_t hours = (minutes / 60) % 24;
        if (hours == state->old_display.hours) {
            return;
        }
        state->old_display.hours = hours;
        sprintf(buf, "%02" PRIu32, hours);
        watch_display_text(WATCH_POSITION_HOURS, buf);
    } else {
        // Display MM:SS.ss (or MM:SS if slow refresh is enabled)
        // Hundredths
        if (state->slow_refresh && (state->status == TC_STATUS_RUNNING || state->status == TC_STATUS_IDLE)) {
            watch_display_character_lp_seconds(' ', 8);
            watch_display_character_lp_seconds(' ', 9);
        } else {
            uint8_t sec_100 = (ticks & 0x7F) * 100 / 128;
            watch_display_character_lp_seconds('0' + sec_100 / 10, 8);
            watch_display_character_lp_seconds('0' + sec_100 % 10, 9);
        }

        // Seconds
        if (seconds == state->old_display.seconds) {
            return;
        }
        state->old_display.seconds = seconds;
        sprintf(buf, "%02" PRIu32, seconds % 60);
        watch_display_text(WATCH_POSITION_MINUTES, buf);

        // Minutes
        uint32_t minutes = seconds / 60;
        if (minutes == state->old_display.minutes) {
            return;
        }
        state->old_display.minutes = minutes;
        sprintf(buf, "%02" PRIu32, minutes % 60);
        watch_display_text(WATCH_POSITION_HOURS, buf);
    }
}

/// @brief Display the distance in the top right corner of the lcd, if it is between 1 and 39.
static void _display_small_distance(tachymeter_state_t *state) {
    char buf[3];

    if (state->distance > 0u && state->distance <= 39u) {
        sprintf(buf, "%2" PRIu32, state->distance);
    } else {
        strcpy(buf, "  ");
    }
    watch_display_text(WATCH_POSITION_TOP_RIGHT, buf);
}

/// @brief Display the calculated speed in the bottom of the lcd.
/// Display as many significant digits as possible, with a maximum of 4 digits,
/// and any decimal point represented as a dash in one digit position of the LCD.
static void _display_speed(tachymeter_state_t *state) {
    char buf[7];
    const char *result_units;
    uint32_t speed_100 = state->speed_100;
    uint32_t speed_int = 0u;
    uint32_t speed_frac = 0u;

    if (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM) {
        result_units = units_result_str(state->units);
    } else {
        result_units = "  ";
    }

    if (state->distance == 0u || speed_100 > 999949u) {
        sprintf(buf, "----%s", result_units);
    } else if (speed_100 > 9994u) {
        speed_100 += 50u;
        speed_int = speed_100 / 100u;
        sprintf(buf, "%4" PRIu32 "%s", speed_int, result_units);
    } else if (speed_100 > 999u) {
        speed_100 = (speed_100 + 5u) / 10u;
        speed_int = speed_100 / 10u;
        speed_frac = speed_100 % 10u;
        sprintf(buf, "%2" PRIu32 "-%01" PRIu32 "%s", speed_int, speed_frac, result_units);
    } else {
        speed_int = speed_100 / 100u;
        speed_frac = speed_100 % 100u;
        sprintf(buf, "%1" PRIu32 "-%02" PRIu32 "%s", speed_int, speed_frac, result_units);
    }
    watch_display_text(WATCH_POSITION_BOTTOM, buf);
}

static void _draw_elapsed_indicators(tachymeter_state_t *state, movement_event_t event, uint32_t elapsed) {
    uint8_t subsecond;
    bool tock;

    switch (state->status) {
        case TC_STATUS_RUNNING:
            subsecond = elapsed & 127;
            tock = subsecond >= 64;

            watch_clear_indicator(WATCH_INDICATOR_LAP);
            if (tock) {
                watch_clear_colon();
            } else {
                watch_set_colon();
            }

            return;

        case TC_STATUS_RUNNING_LAPPING:
            tock = event.subsecond > 0;

            if (tock) {
                watch_clear_indicator(WATCH_INDICATOR_LAP);
                watch_clear_colon();
            } else {
                watch_set_indicator(WATCH_INDICATOR_LAP);
                watch_set_colon();
            }

            return;

        case TC_STATUS_STOPPED_LAPPING:
            watch_set_indicator(WATCH_INDICATOR_LAP);
            watch_set_colon();

            return;

        case TC_STATUS_STOPPED:
        case TC_STATUS_IDLE:
        default:
            watch_clear_indicator(WATCH_INDICATOR_LAP);
            watch_set_colon();
            return;
    }
}

static void _draw_speed_indicators(tachymeter_state_t *state) {
    watch_clear_colon();
    if (state->status == TC_STATUS_STOPPED_LAPPING) {
        watch_set_indicator(WATCH_INDICATOR_LAP);
    } else {
        watch_clear_indicator(WATCH_INDICATOR_LAP);
    }
}

static void _draw_setting_indicators(void) {
    watch_clear_indicator(WATCH_INDICATOR_LAP);
    watch_clear_colon();
}

static void _display_setting(tachymeter_state_t *state, movement_event_t event) {
    char buf[5];
    bool tock = event.subsecond >= 2;

    if (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM) {
        if (tock && state->status == TC_STATUS_SETTING_UNITS) {
            watch_display_text(WATCH_POSITION_SECONDS, "  ");
        } else {
            watch_display_text(WATCH_POSITION_SECONDS, units_str(state->units));
        }
    } else {
        watch_display_text(WATCH_POSITION_SECONDS, "  ");
        if (tock && state->status == TC_STATUS_SETTING_UNITS) {
            watch_display_text(WATCH_POSITION_TOP_LEFT, "  ");
        } else {
            watch_display_text(WATCH_POSITION_TOP_LEFT, units_str(state->units));
        }
    }
    sprintf(buf, "%04" PRIu32, state->distance);
    if (tock) {
        switch (state->status) {
            case TC_STATUS_SETTING_3:
                buf[0] = ' ';
                break;
            case TC_STATUS_SETTING_2:
                buf[1] = ' ';
                break;
            case TC_STATUS_SETTING_1:
                buf[2] = ' ';
                break;
            case TC_STATUS_SETTING_0:
                buf[3] = ' ';
                break;
            default:
                break;
        }
    }
    watch_display_text(WATCH_POSITION_BOTTOM, buf);
}

static void _display_update(tachymeter_state_t *state, movement_event_t event, uint32_t elapsed) {
    switch (state->status) {
        case TC_STATUS_IDLE:
        case TC_STATUS_RUNNING:
        case TC_STATUS_RUNNING_LAPPING:
            _draw_elapsed_indicators(state, event, elapsed);
            _display_elapsed(state, elapsed);
            return;
        case TC_STATUS_STOPPED:
        case TC_STATUS_STOPPED_LAPPING:
            if (event.event_type == EVENT_TICK) {
                state->result_ticks++;
            }
            // Alternate between displaying the elapsed time and the calculated speed
            if ((state->result_ticks & 0x02u) && state->distance) {
                state->old_display = (hms_t) { -1, -1, -1 };
                _draw_speed_indicators(state);
                _display_speed(state);
            } else {
                _draw_elapsed_indicators(state, event, elapsed);
                _display_elapsed(state, elapsed);
            }
            return;
        case TC_STATUS_SETTING_UNITS:
        case TC_STATUS_SETTING_3:
        case TC_STATUS_SETTING_2:
        case TC_STATUS_SETTING_1:
        case TC_STATUS_SETTING_0:
            _draw_setting_indicators();
            _display_setting(state, event);
            return;
        default:
            return;
    }
}

static uint8_t get_refresh_rate(tachymeter_state_t *state) {
    switch (state->status) {
        case TC_STATUS_RUNNING:
            if (state->slow_refresh) {
                return DISPLAY_RUNNING_RATE_SLOW;
            } else {
                return DISPLAY_RUNNING_RATE;
            }
        case TC_STATUS_RUNNING_LAPPING:
            return 2;
        case TC_STATUS_SETTING_UNITS:
        case TC_STATUS_SETTING_3:
        case TC_STATUS_SETTING_2:
        case TC_STATUS_SETTING_1:
        case TC_STATUS_SETTING_0:
            return 4;
        case TC_STATUS_STOPPED:
        case TC_STATUS_IDLE:
        default:
            return 1;
    }
}

static void setting_digit_inc(tachymeter_state_t *state) {
    // Increment the digit being set, with wrap-around.
    switch (state->status) {
        case TC_STATUS_SETTING_UNITS:
            state->units = (state->units + 1) % TC_NUM_UNITS;
            break;
        case TC_STATUS_SETTING_3:
            state->distance = (state->distance + 1000u) % 10000u;
            break;
        case TC_STATUS_SETTING_2:
            state->distance += (((state->distance / 100u) + 1u) % 10u) ? 100 : -900;
            break;
        case TC_STATUS_SETTING_1:
            state->distance += (((state->distance / 10u) + 1u) % 10u) ? 10 : -90;
            break;
        case TC_STATUS_SETTING_0:
            state->distance += ((state->distance + 1u) % 10u) ? 1 : -9;
            break;
        default:
            break;
    }
}

static void button_event_beep(tachymeter_state_t *state, movement_event_t event) {
    switch (event.event_type) {
        case EVENT_ALARM_BUTTON_DOWN:
        case EVENT_LIGHT_BUTTON_DOWN:
        case EVENT_LIGHT_LONG_PRESS:
            switch (state->status) {
                case TC_STATUS_SETTING_UNITS:
                case TC_STATUS_SETTING_3:
                case TC_STATUS_SETTING_2:
                case TC_STATUS_SETTING_1:
                case TC_STATUS_SETTING_0:
                    // Don't beep when setting the digits, just flash the display.
                    break;
                default:
                    _button_beep();
                    break;
            }
            break;
        case EVENT_ALARM_LONG_PRESS:
            switch (state->status) {
                case TC_STATUS_IDLE:
                    // fall through
                case TC_STATUS_RUNNING:
                    _button_beep();
                    break;
                default:
                    break;
            }
            break;
        case EVENT_LIGHT_BUTTON_UP:
            if (state->status == TC_STATUS_SETTING_0) {
                _button_beep();
            }
        default:
            break;
    }
}
static void state_transition(tachymeter_state_t *state, rtc_counter_t counter, movement_event_type_t event_type) {
    switch (state->status) {
        case TC_STATUS_IDLE:
            switch (event_type) {
                case EVENT_ALARM_BUTTON_DOWN:
                    state->status = TC_STATUS_RUNNING;
                    state->start_counter = counter;
                    movement_request_tick_frequency(get_refresh_rate(state));
                    return;
                case EVENT_LIGHT_LONG_PRESS:
                    state->slow_refresh = !state->slow_refresh;
                    return;
                case EVENT_ALARM_LONG_PRESS:
                    state->status = TC_STATUS_SETTING_UNITS;
                    movement_request_tick_frequency(get_refresh_rate(state));
                    return;
                default:
                    return;
            }

        case TC_STATUS_RUNNING:
            switch (event_type) {
                case EVENT_ALARM_BUTTON_DOWN:
                    state->status = TC_STATUS_STOPPED;
                    state->stop_counter = counter;
                    state->result_ticks = 0;
                    calc_speed(state, counter - state->start_counter);
                    movement_request_tick_frequency(get_refresh_rate(state));
                    return;
                case EVENT_LIGHT_BUTTON_DOWN:
                    state->status = TC_STATUS_RUNNING_LAPPING;
                    state->lap_counter = counter;
                    movement_request_tick_frequency(get_refresh_rate(state));
                    return;
                case EVENT_ALARM_LONG_PRESS:
                    state->status = TC_STATUS_SETTING_UNITS;
                    movement_request_tick_frequency(get_refresh_rate(state));
                default:
                    return;
            }

        case TC_STATUS_RUNNING_LAPPING:
            switch (event_type) {
                case EVENT_ALARM_BUTTON_DOWN:
                    state->status = TC_STATUS_STOPPED_LAPPING;
                    state->stop_counter = counter;
                    state->result_ticks = 0;
                    calc_speed(state, counter - state->start_counter);
                    movement_request_tick_frequency(get_refresh_rate(state));
                    return;
                case EVENT_LIGHT_BUTTON_DOWN:
                    state->status = TC_STATUS_RUNNING;
                    state->lap_counter = counter;
                    movement_request_tick_frequency(get_refresh_rate(state));
                    return;
                case EVENT_LIGHT_LONG_PRESS:
                    state->status = TC_STATUS_RUNNING;
                    state->slow_refresh = !state->slow_refresh;
                    movement_request_tick_frequency(get_refresh_rate(state));
                    return;
                default:
                    return;
            }

        case TC_STATUS_STOPPED_LAPPING:
            switch (event_type) {
                case EVENT_ALARM_BUTTON_DOWN:
                    state->status = TC_STATUS_RUNNING_LAPPING;
                    state->start_counter = counter - state->stop_counter + state->start_counter;
                    state->lap_counter = counter - state->stop_counter + state->lap_counter;
                    movement_request_tick_frequency(get_refresh_rate(state));
                    return;
                case EVENT_LIGHT_BUTTON_DOWN:
                    state->status = TC_STATUS_STOPPED;
                    state->result_ticks = 0;
                    state->old_display = (hms_t) { -1, -1, -1 };
                    calc_speed(state, state->stop_counter - state->start_counter);
                    return;
                default:
                    return;
            }

        case TC_STATUS_STOPPED:
            switch (event_type) {
                case EVENT_ALARM_BUTTON_DOWN:
                    state->old_display = (hms_t) { -1, -1, -1 };
                    state->status = TC_STATUS_RUNNING;
                    state->start_counter = counter - state->stop_counter + state->start_counter;
                    movement_request_tick_frequency(get_refresh_rate(state));
                    return;
                case EVENT_LIGHT_BUTTON_DOWN:
                    state->old_display = (hms_t) { -1, -1, -1 };
                    state->status = TC_STATUS_IDLE;
                    return;
                default:
                    return;
            }

        case TC_STATUS_SETTING_UNITS:
        case TC_STATUS_SETTING_3:
        case TC_STATUS_SETTING_2:
        case TC_STATUS_SETTING_1:
        case TC_STATUS_SETTING_0:
            switch (event_type) {
                case EVENT_ALARM_BUTTON_UP:
                    setting_digit_inc(state);
                    return;
                case EVENT_LIGHT_BUTTON_UP:
                    if (state->status == TC_STATUS_SETTING_0) {
                        state->status = TC_STATUS_IDLE;
                        state->scrolling = false;
                        state->old_display = (hms_t) { -1, -1, -1 };
                        _display_title();
                        _display_small_distance(state);
                    } else {
                        state->status++;
                    }
                    return;
                case EVENT_LIGHT_LONG_PRESS:
                    state->status = TC_STATUS_SETTING_UNITS;
                    state->distance = 0;
                    return;
                case EVENT_ALARM_LONG_PRESS:
                    state->scrolling = true;
                    return;
                default:
                    return;
            }
        default:
            return;
    }
}

static uint32_t elapsed_time(tachymeter_state_t *state, rtc_counter_t counter) {
    switch (state->status) {
        case TC_STATUS_IDLE:
            return 0;

        case TC_STATUS_RUNNING:
            return counter - state->start_counter;

        case TC_STATUS_RUNNING_LAPPING:
        case TC_STATUS_STOPPED_LAPPING:
            return state->lap_counter - state->start_counter;

        case TC_STATUS_STOPPED:
            return state->stop_counter - state->start_counter;

        default:
            return 0;
    }
}

void tachymeter_face_setup(uint8_t watch_face_index, void ** context_ptr) {
    (void) watch_face_index;
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(tachymeter_state_t));
        memset(*context_ptr, 0, sizeof(tachymeter_state_t));
        tachymeter_state_t *state = (tachymeter_state_t *)*context_ptr;
        state->start_counter = 0;
        state->stop_counter = 0;
        state->lap_counter = 0;
        state->status = TC_STATUS_IDLE;
    }
}

void tachymeter_face_activate(void *context) {
    tachymeter_state_t *state = (tachymeter_state_t *) context;
    if (state->status >= TC_STATUS_SETTING_UNITS && state->status <= TC_STATUS_SETTING_0) {
        state->status = TC_STATUS_IDLE;
    }
    // force full re-draw
    state->old_display = (hms_t) { -1, -1, -1 };
    movement_request_tick_frequency(get_refresh_rate(state));
}

bool tachymeter_face_loop(movement_event_t event, void *context) {
    tachymeter_state_t *state = (tachymeter_state_t *)context;

    rtc_counter_t counter = watch_rtc_get_counter();

    button_event_beep(state, event);
    state_transition(state, counter, event.event_type);
    rtc_counter_t elapsed = elapsed_time(state, counter);

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            _display_title();
            _display_update(state, event, elapsed);
            _display_small_distance(state);
            break;
        case EVENT_LIGHT_BUTTON_DOWN:
            // Inhibit the LED
            break;
        case EVENT_TICK:
            _display_update(state, event, elapsed);
            break;
        case EVENT_ALARM_BUTTON_DOWN:
            _display_update(state, event, elapsed);
            break;
        default:
            movement_default_loop_handler(event);
            break;
    }

    return true;
}

void tachymeter_face_resign(void *context) {
    (void) context;
    movement_request_tick_frequency(1);
}
