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
* MollenOS MCORE - Timer Manager
*/

#ifndef _MCORE_TIMERS_H_
#define _MCORE_TIMERS_H_

/* Include */
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */
typedef unsigned int TmId_t;
typedef void(*TimerHandler_t)(void*);

/* Timer Type */
typedef enum _MCoreTimerType
{
	TimerSingleShot,
	TimerPeriodic
} MCoreTimerType_t;

/* Structures */
typedef struct _MCoreTimer
{
	/* Callback */
	TimerHandler_t Callback;

	/* Argument for callback */
	void *Args;

	/* Type */
	MCoreTimerType_t Type;

	/* Periode MS */
	uint32_t PeriodicMs;

	/* Counter */
	volatile int32_t MsLeft;

} MCoreTimer_t;

/* Prototypes */
_CRT_EXTERN TmId_t TimersCreateTimer(TimerHandler_t Callback, 
	void *Args, MCoreTimerType_t Type, uint32_t Timeout);

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

/* Should be called by a periodic timer, but only one! */
_CRT_EXTERN void TimersApplyMs(uint32_t Ms);

#endif // !_MCORE_TIMERS_H_


