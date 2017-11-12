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
 * MollenOS Garbage Collector
 * - Makes it possible for regular cleanup in the kernel
 *   or regular maintiance.
 */

/* Includes
 * - System */
#include <garbagecollector.h>
#include <semaphore.h>
#include <threading.h>
#include <log.h>

/* Includes
 * - Library */
#include <ds/list.h>
#include <stddef.h>

/* Prototypes 
 * Defines the GC worker thread */
void GcWorker(void *Args);

/* Globals 
 * Needed for state-keeping */
static Semaphore_t *GlbGcEventLock = NULL;
static List_t *GlbGcHandlers = NULL;
static List_t *GlbGcEvents = NULL;
static int GlbGcInitialized = 0;
static UUId_t GlbGcId = 0;

/* GcConstruct
 * Constructs the gc data-systems, but does
 * not start the actual collection */
void
GcConstruct(void)
{
	// Create data-structures
	GlbGcEventLock = SemaphoreCreate(0);
	GlbGcHandlers = ListCreate(KeyInteger);
	GlbGcEvents = ListCreate(KeyInteger);
	GlbGcId = 0;

	// Set initialized
	GlbGcInitialized = 1;
}

/* GcInitialize
 * Initializes the garbage-collector system */
void
GcInitialize(void)
{
	// Debug information
	LogInformation("GCLL", "Initializing Garbage Collector");

	// Sanitize list status
	if (GlbGcInitialized != 1) {
		GcConstruct();
	}

	// Start the worker thread, no arguments needed
	ThreadingCreateThread("gc-worker", GcWorker, NULL, 0);
}

/* GcRegister
 * Registers a new gc-handler that will be run
 * when new work is available, returns the unique id
 * for the new handler */
UUId_t
GcRegister(
	_In_ GcHandler_t Handler)
{
	// Variables
	DataKey_t Key;

	// Sanitize initialization status
	if (GlbGcInitialized != 1) {
		GcConstruct();
	}

	// Generate a uuid for it
	Key.Value = (int)GlbGcId++;

	// Cool thing - add the handler
	ListAppend(GlbGcHandlers, ListCreateNode(Key, Key, (void*)Handler));

	// Done - no errors
	return (UUId_t)Key.Value;
}

/* GcUnregister
 * Removes a previously registed handler by its id */
OsStatus_t
GcUnregister(
	_In_ UUId_t Handler)
{
	// Variables
	DataKey_t Key;

	// Setup the key
	Key.Value = (int)Handler;

	// Sanitize the status of the gc
	if (GlbGcInitialized != 1
		|| ListGetDataByKey(GlbGcHandlers, Key, 0) == NULL) {
		return OsError;
	}

	// Remove the entry
	ListRemoveByKey(GlbGcHandlers, Key);

	// Done - no further cleanup needed
	return OsSuccess;
}

/* GcSignal
 * Signals new garbage for the specified handler */
OsStatus_t
GcSignal(
	_In_ UUId_t Handler,
	_In_ void *Data)
{
	// Variables
	DataKey_t Key;

	// Setup the key
	Key.Value = (int)Handler;

	// Sanitize the status of the gc
	if (GlbGcInitialized != 1
		|| ListGetDataByKey(GlbGcHandlers, Key, 0) == NULL) {
		return OsError;
	}

	// Create a new event
	ListAppend(GlbGcEvents, ListCreateNode(Key, Key, Data));
	SemaphoreV(GlbGcEventLock, 1);

	// Done - no errors
	return OsSuccess;
}

/* GcWorker
 * The event-handler thread */
void GcWorker(void *Args)
{
	// Variables
	ListNode_t *eNode = NULL;
	GcHandler_t Handler;
	int Run = 1;

	// Unused arg
	_CRT_UNUSED(Args);

	// Run as long as the OS runs
	while (Run)
	{
		// Wait for next event
		SemaphoreP(GlbGcEventLock, 0);

		// Pop event
		eNode = ListPopFront(GlbGcEvents);

		// Sanitize the node
		if (eNode == NULL) {
			continue;
		}

		// Sanitize the handler
		if ((Handler = (GcHandler_t)ListGetDataByKey(GlbGcHandlers, eNode->Key, 0)) == NULL) {
			ListDestroyNode(GlbGcEvents, eNode);
			continue;
		}

		// Call the handler
		Handler(eNode->Data);

		// Free the node
		ListDestroyNode(GlbGcEvents, eNode);
	}
}
