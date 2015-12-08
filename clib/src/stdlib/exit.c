/* MollenOS Exit Function
 * Terminate normally with cleanup, call all functions in atexit stack
 */

#include <stddef.h>
#include <stdlib.h>

extern void MollenOSCleanup(int RetCode);

void exit(int status)
{
	/* Cleanup windows if any  */
	uint32_t Result = 0;
	_syscall4(SYSCALL_SEND_MESSAGE,	0x8000, 0x3, 0, 0, &Result);

	/* End App Life */
	_asm {
		
		/* Do CRT Cleanup */
		call _DoExit;

		/* Terminate Us */
		mov eax, SYSCALL_PROC_TERMINATE;
		mov ebx, [status];
		int 0x80;

		/* Yield */
		int 0x81;
	}

	/* Safety Catcher */
	for(;;);
}