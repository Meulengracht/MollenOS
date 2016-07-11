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
* MollenOS Visual C++ Implementation
*/

/* Includes */
#include "../mvcxx.h"
#include "../mscxx.h"

#include <stddef.h>
#include <string.h>
#include <assert.h>

/* OS Includes */
#include <os/Thread.h>

uint32_t NtRaiseException(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT Context, int FirstChance)
{
	NTSTATUS Status;
	PKTHREAD Thread;
	PKTRAP_FRAME TrapFrame;

	/* Get trap frame and link previous one*/
	Thread = KeGetCurrentThread();
	TrapFrame = Thread->TrapFrame;
	Thread->TrapFrame = KiGetLinkedTrapFrame(TrapFrame);

	/* Set exception list */
#ifdef _M_IX86
	KeGetPcr()->NtTib.ExceptionList = TrapFrame->ExceptionList;
#endif

	/* Raise the exception */
	Status = KiRaiseException(ExceptionRecord,
		Context,
		NULL,
		TrapFrame,
		FirstChance);
	if (NT_SUCCESS(Status))
	{
		/* It was handled, so exit restoring all state */
		KiServiceExit2(TrapFrame);
	}
	else
	{
		/* Exit with error */
		KiServiceExit(TrapFrame, Status);
	}

	/* We don't actually make it here */
	return Status;
}

uint32_t NtContinue(PCONTEXT Context, int TestAlert)
{
	PKTHREAD Thread;
	NTSTATUS Status;
	PKTRAP_FRAME TrapFrame;

	/* Get trap frame and link previous one*/
	Thread = KeGetCurrentThread();
	TrapFrame = Thread->TrapFrame;
	Thread->TrapFrame = KiGetLinkedTrapFrame(TrapFrame);

	/* Continue from this point on */
	Status = KiContinue(Context, NULL, TrapFrame);
	if (NT_SUCCESS(Status))
	{
		/* Check if alert was requested */
		if (TestAlert) KeTestAlertThread(Thread->PreviousMode);

		/* Exit to new trap frame */
		KiServiceExit2(TrapFrame);
	}
	else
	{
		/* Exit with an error */
		KiServiceExit(TrapFrame, Status);
	}

	/* We don't actually make it here */
	return Status;
}