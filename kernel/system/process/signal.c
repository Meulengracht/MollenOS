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
 * MollenOS MCore - Signal Implementation
 */

/* Includes 
 * - System */
#include <process/phoenix.h>
#include <system/utils.h>
#include <threading.h>
#include <heap.h>

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>

/* Globals 
 * Keep track of signal-consequences */
char GlbSignalIsDeadly[] = {
	0, /* 0? */
	1, /* SIGHUP     */
	1, /* SIGINT     */
	2, /* SIGQUIT    */
	2, /* SIGILL     */
	2, /* SIGTRAP    */
	2, /* SIGABRT    */
	2, /* SIGEMT     */
	2, /* SIGFPE     */
	1, /* SIGKILL    */
	2, /* SIGBUS     */
	2, /* SIGSEGV    */
	2, /* SIGSYS     */
	1, /* SIGPIPE    */
	1, /* SIGALRM    */
	1, /* SIGTERM    */
	1, /* SIGUSR1    */
	1, /* SIGUSR2    */
	0, /* SIGCHLD    */
	0, /* SIGPWR     */
	0, /* SIGWINCH   */
	0, /* SIGURG     */
	0, /* SIGPOLL    */
	3, /* SIGSTOP    */
	3, /* SIGTSTP    */
	0, /* SIGCONT    */
	3, /* SIGTTIN    */
	3, /* SIGTTOUT   */
	1, /* SIGVTALRM  */
	1, /* SIGPROF    */
	2, /* SIGXCPU    */
	2, /* SIGXFSZ    */
	0, /* SIGWAITING */
	1, /* SIGDIAF    */
	0, /* SIGHATE    */
	0, /* SIGWINEVENT*/
	0, /* SIGCAT     */
	0  /* SIGEND	 */
};

/* Create Signal 
 * Dispatches a signal to a thread in the system. If the thread is sleeping
 * and the signal is not masked, then it will be woken up. */
OsStatus_t
SignalCreate(
    _In_ UUId_t     ThreadId,
    _In_ int        Signal)
{
	// Variables
	MCoreThread_t *Target   = ThreadingGetThread(ThreadId);
	MCoreSignal_t *Sig      = NULL;
	DataKey_t sKey;

    // Sanitize input, and then sanitize if we have a handler
	if (Target == NULL || Signal >= NUMSIGNALS) {
		return OsError; // Invalid
	}
    if (Target->SignalInformation[Signal] == 1) {
        return OsError; // Ignored
    }

	// Create a new signal instance for process.
	Sig = (MCoreSignal_t*)kmalloc(sizeof(MCoreSignal_t));
    Sig->Ignorable = GlbSignalIsDeadly[Signal];
	Sig->Signal = Signal;
	memset(&Sig->Context, 0, sizeof(Context_t));

	// Add to signal-list
	sKey.Value = Signal;
	CollectionAppend(Target->SignalQueue, CollectionCreateNode(sKey, Sig));

    // Wake up thread if neccessary
    if (THREADING_RUNMODE(Target->Flags) == THREADING_INACTIVE) {
        // @todo
    }
    return OsSuccess;
}

/* SignalReturn
 * Call upon returning from a signal, this will finish
 * the signal-call and enter a new signal if any is queued up */
OsStatus_t
SignalReturn(void)
{
	// Variables
	MCoreThread_t *Thread   = NULL;
	MCoreSignal_t *Signal   = NULL;
	UUId_t Cpu;

	// Oh oh, someone has done the dirty signal
	Cpu         = CpuGetCurrentId();
	Thread      = ThreadingGetCurrentThread(Cpu);

	// Now.. get active signal
	Signal = Thread->ActiveSignal;

	// Restore context
	// @todo

	// Cleanup signal
	Thread->ActiveSignal = NULL;
	kfree(Signal);

	// Continue into next signal?
	return SignalHandle(Thread->Id);
}

/* Handle Signal 
 * This checks if the process has any waiting signals
 * and if it has, it executes the first in list */
OsStatus_t
SignalHandle(
	_In_ UUId_t ThreadId)
{
	// Variables
	MCoreThread_t *Thread   = NULL;
	MCoreSignal_t *Signal   = NULL;
	CollectionItem_t *sNode = NULL;
	
	// Lookup variables
	Thread                  = ThreadingGetThread(ThreadId);

	// Sanitize, we might not have an Ash
	if (Thread == NULL) {
		return OsError;
	}

	// Even if there is a Ash, we might want not
	// to Ash any signals ATM if there is already 
	// one active
	if (Thread->ActiveSignal != NULL) {
		return OsError;
	}

	// Ok.. pop off first signal
	sNode = CollectionPopFront(Thread->SignalQueue);

	// Sanitize the node, no more signals?
	if (sNode != NULL) {
		Signal = (MCoreSignal_t*)sNode->Data;
		CollectionDestroyNode(Thread->SignalQueue, sNode);
		SignalExecute(Thread, Signal);
	}

	// No more signals
	return OsSuccess;
}

/* SignalExecute
 * This function does preliminary checks before actually
 * dispatching the signal to the process */
void
SignalExecute(
    _In_ MCoreThread_t *Thread,
    _In_ MCoreSignal_t *Signal)
{
    // Variables
    MCoreAsh_t *Process = NULL;

    // Instantiate the process
    Process = PhoenixGetAsh(Thread->AshId);
    if (Process == NULL) {
        return;
    }

	// If there is no handler for the process and we
    // can't ignore signal, we must kill
	if (Process->SignalHandler == 0) {
		char Action = GlbSignalIsDeadly[Signal->Signal];
		if (Action == 1 || Action == 2) {
			PhoenixTerminateAsh(Process);
		}
		return;
	}

	// Update active and dispatch
	Thread->ActiveSignal = Signal;
	SignalDispatch(Thread, Signal);
}
