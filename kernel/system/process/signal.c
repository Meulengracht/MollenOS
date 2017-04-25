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

/* Globals */
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
 * Dispatches a signal to a given ash */
int SignalCreate(UUId_t AshId, int Signal)
{
	/* Variables*/
	MCoreAsh_t *Target = PhoenixGetAsh(AshId);
	MCoreSignal_t *Sig = NULL;
	DataKey_t sKey;

	/* Sanitize the Ash 
	 * and that the signal given is valid */
	if (Target == NULL
		|| (Signal > NUMSIGNALS)) {
		return -1;
	}

	/* Make sure we don't deliver a signal to a handler 
	 * that doesn't exist, unless it's deadly, then it needs to die */
	if (!Target->Signals.Handlers[Signal] && !GlbSignalIsDeadly[Signal]) {
		return 0;
	}

	/* Append signal to list */
	Sig = (MCoreSignal_t*)kmalloc(sizeof(MCoreSignal_t));
	Sig->Handler = (uintptr_t)Target->Signals.Handlers[Signal];
	Sig->Signal = Signal;

	/* Zero the context */
	memset(&Sig->Context, 0, sizeof(Context_t));

	/* Append it */
	sKey.Value = Signal;
	ListAppend(Target->SignalQueue, ListCreateNode(sKey, sKey, Sig));

	/* Done! */
	return 0;
}

/* SignalReturn
 * Call upon returning from a signal, this will finish
 * the signal-call and enter a new signal if any is queued up */
OsStatus_t
SignalReturn(void)
{
	// Variables
	MCoreThread_t *cThread = NULL;
	MCoreSignal_t *Signal = NULL;
	MCoreAsh_t *Ash = NULL;
	UUId_t Cpu;

	// Oh oh, someone has done the dirty signal
	Cpu = CpuGetCurrentId();
	cThread = ThreadingGetCurrentThread(Cpu);
	Ash = PhoenixGetAsh(cThread->AshId);

	// Now.. get active signal
	Signal = Ash->ActiveSignal;

	// Restore context
	// Neccessary?

	// Cleanup signal
	Ash->ActiveSignal = NULL;
	kfree(Signal);

	// Continue into next signal?
	return SignalHandle(cThread->Id);
}

/* Handle Signal 
 * This checks if the process has any waiting signals
 * and if it has, it executes the first in list */
OsStatus_t
SignalHandle(
	_In_ UUId_t ThreadId)
{
	// Variables
	MCoreThread_t *Thread = NULL;
	MCoreSignal_t *Signal = NULL;
	ListNode_t *sNode = NULL;
	MCoreAsh_t *Ash = NULL;
	
	// Lookup variables
	Thread = ThreadingGetThread(ThreadId);
	Ash = PhoenixGetAsh(Thread->AshId);

	// Sanitize, we might not have an Ash
	if (Ash == NULL) {
		return OsError;
	}

	// Even if there is a Ash, we might want not
	// to Ash any signals ATM if there is already 
	// one active
	if (Ash->ActiveSignal != NULL) {
		return OsError;
	}

	// Ok.. pop off first signal
	sNode = ListPopFront(Ash->SignalQueue);

	// Sanitize the node, no more signals?
	if (sNode != NULL) {
		Signal = (MCoreSignal_t*)sNode->Data;
		ListDestroyNode(Ash->SignalQueue, sNode);
		SignalExecute(Ash, Signal);
	}

	// No more signals
	return OsSuccess;
}

/* Execute Signal 
 * This function does preliminary checks before actually
 * dispatching the signal to the process */
void SignalExecute(MCoreAsh_t *Ash, MCoreSignal_t *Signal)
{
	/* Sanitize the thread and signal */
	if ((Signal->Signal == 0 || Signal->Signal >= NUMSIGNALS)
		|| Signal->Handler == 1) {
		return;
	}

	/* Do we have a handler? */
	if (Signal->Handler == 0)
	{
		/* Thing is ... if the signal is deadly
		 * we can't ignore the signal */
		char Action = GlbSignalIsDeadly[Signal->Signal];
		
		/* Kill? */
		if (Action == 1 || Action == 2) {
			PhoenixTerminateAsh(Ash);
		}

		/* Done, return, ignore rest */
		return;
	}

	/* Update some settings */
	Ash->ActiveSignal = Signal;

	/* Handle Signal */
	SignalDispatch(Ash, Signal);
}
