/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - Utils Definitions & Structures
 * - This header describes the base utils-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _UTILS_INTERFACE_H_
#define _UTILS_INTERFACE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

/* Global <always-on> definitions
 * These are enabled no matter which kind of debugging is enabled */
#define SYSTEM_DEBUG_TRACE			0x00000000
#define SYSTEM_DEBUG_WARNING		0x00000001
#define SYSTEM_DEBUG_ERROR			0x00000002

#define WARNING(...)				SystemDebug(SYSTEM_DEBUG_WARNING, __VA_ARGS__)
#define ERROR(...)					SystemDebug(SYSTEM_DEBUG_ERROR, __VA_ARGS__)

/* Global <toggable> definitions
 * These can be turned on per-source file by pre-defining
 * the __TRACE before inclusion */
#ifdef __TRACE
#define TRACE(...)					SystemDebug(SYSTEM_DEBUG_TRACE, __VA_ARGS__)
#else
#define TRACE(...)
#endif

/* Threading Utility
 * Waits for a condition to set in a busy-loop using
 * ThreadSleep */
#define WaitForCondition(condition, runs, wait, message, ...)\
    for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
        if (timeout_ >= runs) {\
             SystemDebug(SYSTEM_DEBUG_WARNING, message, __VA_ARGS__);\
             break;\
												        }\
        ThreadSleep(wait);\
						    }

/* Threading Utility
 * Waits for a condition to set in a busy-loop using
 * ThreadSleep */
#define WaitForConditionWithFault(fault, condition, runs, wait)\
	fault = 0; \
    for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
        if (timeout_ >= runs) {\
			 fault = 1; \
             break;\
										        }\
        ThreadSleep(wait);\
					    }

// Cpp Barrier
_CODE_BEGIN

/* SystemDebug 
 * Debug/trace printing for userspace application and drivers */
MOSAPI
void
MOSABI
SystemDebug(
	_In_ int Type,
	_In_ __CONST char *Format, ...);


// Cpp Barrier
_CODE_END

#endif //!_UTILS_INTERFACE_H_
