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
 * MollenOS MCore - Timer Mangement Interface
 * - Contains the timer interface system and its implementation
 *   keeps track of system timers
 */

#ifndef _MCORE_TIMERS_H_
#define _MCORE_TIMERS_H_

/* Includes
 * - System */
#include <os/osdefs.h>
#include <os/timers.h>

/* Timer Type */
typedef enum _MCoreTimerType
{
	TimerSingleShot,
	TimerPeriodic
} MCoreTimerType_t;

/* Structures */
typedef struct _MCoreTimer {
	TimerHandler_t		Callback;
	void				*Args;
	MCoreTimerType_t	Type;
	size_t				PeriodicMs;
	volatile ssize_t	MsLeft;
} MCoreTimer_t;

/* Prototypes */
KERNELAPI UUId_t TimersCreateTimer(TimerHandler_t Callback,
	void *Args, MCoreTimerType_t Type, size_t Timeout);
KERNELAPI void TimersDestroyTimer(UUId_t TimerId);

/* Sleep, Stall, etc */
KERNELAPI void SleepMs(size_t MilliSeconds);
KERNELAPI void StallMs(size_t MilliSeconds);

/* Stall-No-Int */
KERNELAPI void DelayMs(size_t MilliSeconds);

/* Tools */
#define WaitForCondition(condition, runs, wait, message, ...)\
    for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
        if (timeout_ >= runs) {\
             Log(message, __VA_ARGS__);\
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

#define DelayForConditionWithFault(fault, condition, runs, wait)\
	fault = 0; \
    for (unsigned int timeout_ = 0; !(condition); timeout_++) {\
        if (timeout_ >= runs) {\
			 fault = 1; \
             break;\
												        }\
        DelayMs(wait);\
						    }

/* The system timer structure
 * Contains information related to the
 * registered system timers */
typedef struct _MCoreSystemTimer {
	UUId_t					Source;
	size_t					Tick;
	size_t					Ticks;
} MCoreSystemTimer_t;

/* TimersInitialize
 * Initializes the timer sub-system that supports
 * registering of system timers and callback timers */
__EXTERN void TimersInitialize(void);

/* TimersRegistrate 
 * Registrates a interrupt timer source with the
 * timer management, which keeps track of which interrupts
 * are available for time-keeping */
__EXTERN OsStatus_t TimersRegister(UUId_t Source, size_t TickNs);

/* TimersInterrupt
 * Called by the interrupt-code to tell the timer-management system
 * a new interrupt has occured from the given source. This allows
 * the timer-management system to tell us if that was the active
 * timer-source */
__EXTERN OsStatus_t TimersInterrupt(UUId_t Source);

#endif // !_MCORE_TIMERS_H_


