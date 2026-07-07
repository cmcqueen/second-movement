/*
 * MIT License
 *
 * Copyright (c) 2026 Craig McQueen
 * Based on fast_stopwatch face Copyright (c) 2022 Andreas Nebinger
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

#ifndef TACHYMETER_FACE_H_
#define TACHYMETER_FACE_H_

/*
 * TACHYMETER face
 *
 * The Tachymeter face implements an average speed measurement functionality.
 * It incorporates stopwatch functionality, but adds the ability to calculate
 * average speed based on a known distance.
 *
 * Use the ALARM button to start and stop the stopwatch.
 * Press the LIGHT button while the stopwatch is running to view the lap time.
 *  (The stopwatch continues running in the background, indicated by a blinking colon.)
 * Press the LIGHT button again to switch back to the running stopwatch.
 * Press the LIGHT button when the timekeeping is stopped to reset the stopwatch.
 */

#include "movement.h"

typedef struct {
    rtc_counter_t start_counter; // rtc counter when the stopwatch was started
    rtc_counter_t lap_counter;   // rtc counter when the stopwatch was lapped
    rtc_counter_t stop_counter;  // rtc counter when the stopwatch was stopped
    uint8_t status;              // the status the stopwatch is in (idle, running, stopped)
    bool slow_refresh;           // update the display slowly (same 128Hz timekeeping accuracy)
    struct {
        rtc_counter_t seconds;
        rtc_counter_t minutes;
        rtc_counter_t hours;
    } old_display;               // the digits currently being displayed on screen
} tachymeter_state_t;

void tachymeter_face_setup(uint8_t watch_face_index, void ** context_ptr);
void tachymeter_face_activate(void *context);
bool tachymeter_face_loop(movement_event_t event, void *context);
void tachymeter_face_resign(void *context);

#define tachymeter_face ((const watch_face_t){ \
    tachymeter_face_setup, \
    tachymeter_face_activate, \
    tachymeter_face_loop, \
    tachymeter_face_resign, \
    NULL, \
})

#endif // TACHYMETER_FACE_H_
