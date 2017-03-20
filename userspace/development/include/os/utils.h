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

/* Threading Utility
 * Waits for a condition to set in a busy-loop using
 * ThreadSleep */
#define WaitForCondition(condition, runs, wait, message, ...)\
    for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
        if (timeout_ >= runs) {\
             MollenOSSystemLog(message, __VA_ARGS__);\
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

#endif //!_UTILS_INTERFACE_H_
