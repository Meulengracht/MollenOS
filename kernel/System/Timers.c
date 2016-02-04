/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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

/* Includes */
#include <DeviceManager.h>
#include <Devices\Timer.h>
#include <Arch.h>
#include <Timers.h>
#include <Heap.h>
#include <List.h>

/* Globals */
list_t *GlbTimers = NULL;
TmId_t GlbTimerIds = 0;
int GlbTimersInitialized = 0;

/* Init */
void TimersInit(void)
{
	/* Create list */
	GlbTimers = list_create(LIST_SAFE);
	GlbTimersInitialized = 1;
	GlbTimerIds = 0;
}

TmId_t TimersCreateTimer(TimerHandler_t Callback,
	void *Args, MCoreTimerType_t Type, size_t Timeout)
{
	MCoreTimer_t *TimerInfo;
	TmId_t Id;

	/* Sanity */
	if (GlbTimersInitialized != 1)
		TimersInit();

	/* Allocate */
	TimerInfo = (MCoreTimer_t*)kmalloc(sizeof(MCoreTimer_t));
	TimerInfo->Callback = Callback;
	TimerInfo->Args = Args;
	TimerInfo->Type = Type;
	TimerInfo->PeriodicMs = Timeout;
	TimerInfo->MsLeft = (ssize_t)Timeout;

	/* Append to list */
	list_append(GlbTimers, list_create_node(GlbTimerIds, TimerInfo));

	/* Increase */
	Id = GlbTimerIds;
	GlbTimerIds++;

	return Id;
}

/* Sleep function */
void SleepMs(size_t MilliSeconds)
{
	/* Lookup */
	MCoreDevice_t *tDevice = DmGetDevice(DeviceTimer);
	MCoreTimerDevice_t *Timer = NULL;

	/* Sanity */
	if (tDevice == NULL)
	{
		DelayMs(MilliSeconds);
		return;
	}
	
	/* Cast */
	Timer = (MCoreTimerDevice_t*)tDevice->Data;

	/* Go */
	Timer->Sleep(tDevice, MilliSeconds);
}

void SleepNs(size_t NanoSeconds)
{
	/* Lookup */
	MCoreDevice_t *tDevice = DmGetDevice(DevicePerfTimer);
	MCoreTimerDevice_t *Timer = NULL;

	/* Sanity */
	if (tDevice == NULL)
	{
		DelayMs((NanoSeconds / 1000) + 1);
		return;
	}

	/* Cast */
	Timer = (MCoreTimerDevice_t*)tDevice->Data;

	/* Go */
	Timer->Sleep(tDevice, NanoSeconds);
}

/* Stall functions */
void StallMs(size_t MilliSeconds)
{
	/* Lookup */
	MCoreDevice_t *tDevice = DmGetDevice(DeviceTimer);
	MCoreTimerDevice_t *Timer = NULL;

	/* Sanity */
	if (tDevice == NULL)
	{
		DelayMs(MilliSeconds);
		return;
	}

	/* Cast */
	Timer = (MCoreTimerDevice_t*)tDevice->Data;

	/* Go */
	Timer->Stall(tDevice, MilliSeconds);
}

void StallNs(size_t NanoSeconds)
{
	/* Lookup */
	MCoreDevice_t *tDevice = DmGetDevice(DevicePerfTimer);
	MCoreTimerDevice_t *Timer = NULL;

	/* Sanity */
	if (tDevice == NULL)
	{
		DelayMs((NanoSeconds / 1000) + 1);
		return;
	}

	/* Cast */
	Timer = (MCoreTimerDevice_t*)tDevice->Data;

	/* Go */
	Timer->Stall(tDevice, NanoSeconds);
}

/* This should be called by only ONE periodic irq */
void TimersApplyMs(size_t Ms)
{
	list_node_t *i;

	/* Sanity */
	if (GlbTimersInitialized != 1)
		return;

	_foreach(i, GlbTimers)
	{
		/* Cast */
		MCoreTimer_t *Timer = (MCoreTimer_t*)i->data;

		/* Decreament */
		Timer->MsLeft -= Ms;

		/* Pop timer? */
		if (Timer->MsLeft <= 0)
		{
			/* Yay! Pop! */
			ThreadingCreateThread("Timer Callback", Timer->Callback, Timer->Args, 0);

			/* Restart? */
			if (Timer->Type == TimerPeriodic)
				Timer->MsLeft = (ssize_t)Timer->PeriodicMs;
			else
			{
				/* Remove */
				list_remove_by_node(GlbTimers, i);

				/* Free timer */
				kfree(Timer);

				/* Free node */
				kfree(i);
			}
		}
	}
}