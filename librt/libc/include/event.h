/**
 * Copyright 2020, Philip Meulengracht
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
 */

#ifndef __EVENT_H__
#define __EVENT_H__

#include <crtdefs.h>

/**
 * Reset events are either set or not set. When writing or reading from a reset event
 * the value that can be written or read is a integer value. If multiple writes are done
 * the most recent value is the one read.
 */
#define EVT_RESET_EVENT   0

/**
 * Semaphore events have max values set by the initialValue. The real initial value of the semaphore will be 0.
 * Reads will always consume a value of 1, and writes can 'restore' multiple values. The value written is the count
 * and should be of size unsigned int.
 */
#define EVT_SEM_EVENT     1

/**
 * Timeout events have their timeout value defined by initialValue. Timeout events cannot be written to, but only
 * read from, and will trigger once the timeout value has been reached. Read can be invoked with NULL and length of 0.
 */
#define EVT_TIMEOUT_EVENT 2

#define EVT_TYPE(x)       (x & 0x3U)

_CODE_BEGIN

/**
 * Creates a new event descriptor that can act as different types of synchronization primitives. The initialValue
 * parameter has different meaning for each kind of event
 * @param initialValue Can either be the count of the semaphore, or timeout value
 * @param flags        Configuration of the event descriptor
 * @return             An descriptor that can be used as a handle
 */
CRTDECL(int, eventd(unsigned int initialValue, unsigned int flags));

_CODE_END

#endif //!__EVENT_H__
