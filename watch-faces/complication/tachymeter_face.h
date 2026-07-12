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
 * Hold the ALARM button to enter the distance setting mode. Use the LIGHT button to
 * cycle through the digits, and the ALARM button to increment the selected digit.
 *
 * Press the ALARM button briefly to start and stop the stopwatch.
 * When the stopwatch is stopped, the average speed is calculated and displayed based
 * on the elapsed time and the set distance.
 * Press the LIGHT button when the stopwatch is not running to reset the stopwatch.
 *
 * Press the LIGHT button while the stopwatch is running to view the lap time.
 * (The stopwatch continues running in the background, indicated by a blinking colon.)
 * Press the LIGHT button again to switch back to the running stopwatch.
 */

#include "movement.h"

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
