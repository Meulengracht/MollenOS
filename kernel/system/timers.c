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
#define __MODULE		"TMIF"
//#define __TRACE

/* Includes 
 * - System */
#include <process/ash.h>
#include <interrupts.h>
#include <scheduler.h>
#include <ds/list.h>
#include <timers.h>
#include <debug.h>
#include <heap.h>

/* Includes
 * - Library */
#include <stdlib.h>
#include <stddef.h>

/* Globals */
static MCoreSystemTimer_t *GlbActiveSystemTimer = NULL;
static List_t *GlbSystemTimers = NULL;
static List_t *GlbTimers = NULL;
static UUId_t GlbTimerIds = 0;
static int GlbTimersInitialized = 0;

/* TimersInitialize
 * Initializes the timer sub-system that supports
 * registering of system timers and callback timers */
void
TimersInitialize(void)
{
	// Initialize all the globals 
	// including lists etc
	GlbActiveSystemTimer = NULL;
	GlbSystemTimers = ListCreate(KeyInteger, LIST_SAFE);
	GlbTimers = ListCreate(KeyInteger, LIST_SAFE);
	GlbTimersInitialized = 1;
	GlbTimerIds = 0;
}

/* TimersStart 
 * Creates a new standard timer for the requesting process. */
KERNELAPI
UUId_t
KERNELABI
TimersStart(
    _In_ size_t IntervalNs,
    _In_ int Periodic,
    _In_ __CONST void *Data)
{
    // Variables
    MCoreTimer_t *Timer = NULL;
    DataKey_t Key;

    // Sanity
    if (PhoenixGetCurrentAsh() == NULL) {
        return UUID_INVALID;
    }

    // Allocate a new instance and initialize
    Timer = (MCoreTimer_t*)kmalloc(sizeof(MCoreTimer_t));
    Timer->Id = GlbTimerIds++;
    Timer->AshId = PhoenixGetCurrentAsh()->Id;
    Timer->Data = Data;
    Timer->Interval = IntervalNs;
    Timer->Current = 0;
    Timer->Periodic = Periodic;
    Key.Value = (int)Timer->Id;

    // Add to list of timers
    ListAppend(GlbTimers, ListCreateNode(Key, Key, Timer));
    return Timer->Id;
}

/* TimersStop
 * Destroys a existing standard timer, owner must be the requesting
 * process. Otherwise access fault. */
KERNELAPI
OsStatus_t
KERNELABI
TimersStop(
    _In_ UUId_t TimerId)
{
    // Now loop through timers registered
	foreach(tNode, GlbTimers) {
        // Initiate pointer
        MCoreTimer_t *Timer = (MCoreTimer_t*)tNode->Data;
        
        // Does it match the id? + Owner must match
        if (Timer->Id == TimerId
            && Timer->AshId == PhoenixGetCurrentAsh()->Id) {
			ListRemoveByNode(GlbTimers, tNode);
            kfree(Timer);
            kfree(tNode);
            return OsSuccess;
		}
    }
    return OsError;
}

/* TimersTick
 * This method actually applies the new tick-delta to all
 * active timers registered, and decreases the total */
void
TimersTick(
	_In_ size_t Tick)
{
	// Variables
	ListNode_t *i = NULL;
	size_t MilliTicks = 0;

	// Calculate how many milliseconds
	MilliTicks = DIVUP(Tick, NSEC_PER_MSEC);

	// Update scheduler with the milliticks
	SchedulerTick(MilliTicks);

	// Now loop through timers registered
	_foreach(i, GlbTimers) {
        // Initiate pointer
        MCoreTimer_t *Timer = (MCoreTimer_t*)i->Data;
        
        // Reduce
		Timer->Current -= MIN(Timer->Current, Tick);
		if (Timer->Current == 0) {
			__KernelTimeoutDriver(Timer->AshId, Timer->Id, (void*)Timer->Data);
			if (Timer->Periodic) {
				Timer->Current = Timer->Interval;
			}
			else {
				ListRemoveByNode(GlbTimers, i);
				kfree(Timer);
				kfree(i);
			}
		}
	}
}

/* TimersRegistrate
 * Registrates a interrupt timer source with the
 * timer management, which keeps track of which interrupts
 * are available for time-keeping */
OsStatus_t
TimersRegister(
	_In_ UUId_t Source, 
	_In_ size_t TickNs)
{
	// Variables
	MCoreInterruptDescriptor_t *Interrupt = NULL;
	MCoreSystemTimer_t *SystemTimer = NULL;
	DataKey_t tKey;
	int Delta = abs(1000 - (int)TickNs);

	// Trace
	TRACE("TimersRegister()");

	// Do some validation about the timer source 
	// the only system timers we want are fast_interrupts
	Interrupt = InterruptGet(Source);
	if (Interrupt == NULL) {
		TRACE("Interrupt was not found for source %u", Source);
		return OsError;
	}

	// Create a new instance of a system timer
	SystemTimer = (MCoreSystemTimer_t*)kmalloc(sizeof(MCoreSystemTimer_t));
	SystemTimer->Source = Source;
	SystemTimer->Tick = TickNs;
	SystemTimer->Ticks = 0;

	// Add the new timer to the list
	tKey.Value = 0;
	ListAppend(GlbSystemTimers, ListCreateNode(
		tKey, tKey, SystemTimer));

	// Ok, for a system timer we want something optimum
	// of 1 ms per interrupt
	if (GlbActiveSystemTimer != NULL) {
		int ActiveDelta = abs(1000 - GlbActiveSystemTimer->Tick);
		if (ActiveDelta > Delta) {
			GlbActiveSystemTimer = SystemTimer;
		}
	}
	else {
		GlbActiveSystemTimer = SystemTimer;
	}

	// Trace
	TRACE("New system timer: %u", GlbActiveSystemTimer->Source);

	// Done
	return OsSuccess;
}

/* TimersInterrupt
 * Called by the interrupt-code to tell the timer-management system
 * a new interrupt has occured from the given source. This allows
 * the timer-management system to tell us if that was the active
 * timer-source */
OsStatus_t
TimersInterrupt(
	_In_ UUId_t Source)
{
	// Sanitize if the source is ok
	if (GlbActiveSystemTimer != NULL) {
		if (GlbActiveSystemTimer->Source == Source) {
			TimersTick(GlbActiveSystemTimer->Tick);
			return OsSuccess;
		}
	}

	// Return error
	return OsError;
}
