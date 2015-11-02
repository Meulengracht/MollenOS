/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS x86-32 Timer Manager Header
*/
#ifndef _X86_TIMER_MANAGER_
#define _X86_TIMER_MANAGER_


/* Includes */
#include <Arch.h>
#include <crtdefs.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Prototypes */
_CRT_EXTERN void TimerManagerInit(void);

/* Sleep, Stall, etc */
_CRT_EXTERN void SleepMs(uint32_t MilliSeconds);
_CRT_EXTERN void StallMs(uint32_t MilliSeconds);
_CRT_EXTERN void SleepNs(uint32_t NanoSeconds);
_CRT_EXTERN void StallNs(uint32_t NanoSeconds);

/* Stall-No-Int */
_CRT_EXTERN void DelayMs(uint32_t MilliSeconds);

/* Tools */
#define WaitForCondition(condition, runs, wait, message, ...)\
    for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
        if (timeout_ >= runs) {\
             printf(message, __VA_ARGS__);\
             break;\
								        }\
        StallMs(wait);\
				    }
#define WaitForConditionWithFault(fault, condition, runs, wait)\
	fault = 0; \
    for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
        if (timeout_ >= runs) {\
			 fault = 1; \
             break;\
						        }\
        StallMs(wait);\
			    }

#endif