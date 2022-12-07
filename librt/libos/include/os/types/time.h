/**
 * Copyright 2022, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Time Definitions & Structures
 * - This header describes the shared time-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __TYPES_TIME_H__
#define __TYPES_TIME_H__

#include <os/osdefs.h>

enum OSTimeSource {
    OSTimeSource_MONOTONIC,
    OSTimeSource_THREAD,
    OSTimeSource_PROCESS,
    OSTimeSource_UTC,
};

enum OSClockSource {
    OSClockSource_MONOTONIC, // Provides a monotonic tick since the computer was booted
    OSClockSource_THREAD,    // Provides a monotonic tick since the calling thread was started.
    OSClockSource_PROCESS,   // Provides a monotonic tick since the current process was started.
    OSClockSource_HPC        // Provides a high-precision tick counter
};

/**
 * @brief Timestamps are fixed places in time. It's equivalent to the
 * timespec of C, however it describes the absolute place in time, with
 * the epoch of January 1, 2000.
 */
typedef struct OSTimestamp {
    int64_t Seconds;
    int64_t Nanoseconds;
} OSTimestamp_t;

/**
 * @brief Checks the timestamp for being zero
 * @param timestamp The timestamp that should be checked.
 * @return true if the timestamp is zero, otherwise false.
 */
static inline bool OSTimestampIsZero(OSTimestamp_t* timestamp) {
    return timestamp->Seconds == 0 && timestamp->Nanoseconds == 0;
}

/**
 * @brief Clones the timstamp from src to dest.
 * @param dest The destination where the cloned timestamp should be stored.
 * @param src The timestamp that should be cloned.
 */
static inline void OSTimestampCopy(OSTimestamp_t* dest, OSTimestamp_t* src) {
    dest->Seconds = src->Seconds;
    dest->Nanoseconds = src->Nanoseconds;
}

/**
 * @brief Normalizes the timestamp. This is called after performing time arethmetics
 * to facilitate some reuse.
 * @param timestamp The timestamp that should be normalized.
 */
static inline void OSTimestampNormalize(OSTimestamp_t* timestamp) {
    while (timestamp->Nanoseconds >= NSEC_PER_SEC) {
        timestamp->Seconds++;
        timestamp->Nanoseconds -= NSEC_PER_SEC;
    }
    while (timestamp->Nanoseconds < 0) {
        timestamp->Seconds--;
        timestamp->Nanoseconds += NSEC_PER_SEC;
    }
}

/**
 * @brief Subtracts two timestamps and stores the result into <result>
 * @param result The timestamp where the result should be stored.
 * @param a The left operand.
 * @param b The right operand.
 */
static inline void OSTimestampSubtract(OSTimestamp_t* result, OSTimestamp_t* a, OSTimestamp_t* b){
    result->Seconds = a->Seconds - b->Seconds;
    result->Nanoseconds = a->Nanoseconds - b->Nanoseconds;
    OSTimestampNormalize(result);
}

/**
 * @brief Adds two timestamps and stores the result into <result>
 * @param result The timestamp where the result should be stored.
 * @param a The left operand.
 * @param b The right operand.
 */
static inline void OSTimestampAdd(OSTimestamp_t* result, OSTimestamp_t* a, OSTimestamp_t* b){
    result->Seconds = a->Seconds + b->Seconds;
    result->Nanoseconds = a->Nanoseconds + b->Nanoseconds;
    OSTimestampNormalize(result);
}

/**
 * @brief Adds nanoseconds to the timestamp. This will automatically
 * normalize the resulting timestamp.
 * @param result The timestamp where the result should be stored.
 * @param a The base operand for which nanoseconds should be added to.
 * @param nsec The number of nanoseconds that should be added.
 */
static inline void OSTimestampAddNsec(OSTimestamp_t* result, OSTimestamp_t* a, int64_t nsec) {
    result->Seconds = a->Seconds;
    result->Nanoseconds = a->Nanoseconds + nsec;
    OSTimestampNormalize(result);
}

/**
 * @brief Adds individual timestamp components to the given timestamp, and stores the result
 * in the result timestamp. This solely exists as a conveniency function for C11 timespec's.
 * @param result The timestamp where the result should be stored.
 * @param a The base operand for which nanoseconds should be added to.
 * @param sec The number of seconds that should be added.
 * @param nsec The number of nanoseconds that should be added.
 */
static inline void OSTimestampAddTs(OSTimestamp_t* result, OSTimestamp_t* a, int64_t sec, int64_t nsec) {
    result->Seconds = a->Seconds + sec;
    result->Nanoseconds = a->Nanoseconds + nsec;
    OSTimestampNormalize(result);
}

/**
 * @brief Compares two timestamp values.
 * @param a The timestamp to compare.
 * @param b The timestamp to compare a against.
 * @return 1  if a > b.
 *         0  if a == b.
 *         -1 if a < b.
 */
static inline int OSTimestampCompare(OSTimestamp_t* a, OSTimestamp_t* b) {
    OSTimestampNormalize(a);
    OSTimestampNormalize(b);

    if (a->Seconds == b->Seconds && a->Nanoseconds == b->Nanoseconds) {
        return 0;
    } else if ((a->Seconds > b->Seconds) || (a->Seconds == b->Seconds && a->Nanoseconds > b->Nanoseconds)) {
        return 1;
    }
    return -1;
}

#endif //!__TYPES_TIME_H__
