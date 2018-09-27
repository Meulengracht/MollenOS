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
 * MollenOS C-Support Exit Implementation
 * - Definitions, prototypes and information needed.
 */

#include <stdlib.h>

#ifndef LIBC_KERNEL
/* exit
 * Causes normal program termination to occur.
 * Several cleanup steps are performed:
 *  - functions passed to atexit are called, in reverse order of registration all C streams are flushed and closed
 *  - files created by tmpfile are removed
 *  - control is returned to the host environment. If exit_code is zero or EXIT_SUCCESS, 
 *    an implementation-defined status, indicating successful termination is returned. 
 *    If exit_code is EXIT_FAILURE, an implementation-defined status, indicating unsuccessful 
 *    termination is returned. In other cases implementation-defined status value is returned. */
#ifdef __clang__
__EXTERN void __CrtCallExitHandlers(int Status, int Quick, int DoAtExit, int CleanupCrt);
__EXTERN int __cxa_atexit(void (*Function)(void*), void *Argument, void *Dso);
__EXTERN void *__dso_handle;
int
atexit(
    _In_ void (*Function)(void))
{
    return __cxa_atexit((void (*)(void*))Function, NULL, __dso_handle);
}
void
exit(
    _In_ int Status)
{
    __CrtCallExitHandlers(Status, 0, 1, 1);
	_Exit(Status);
}
#else
__EXTERN void __CppFinit(void);
__EXTERN void StdioCleanup(void);
void
exit(
    _In_ int Status)
{
	StdioCleanup();
	__CppFinit();
	_Exit(Status);
}
#endif
#endif //!LIBC_KERNEL
