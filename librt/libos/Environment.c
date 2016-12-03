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
* MollenOS - Environment Functions
*/

/* Includes */
#include <os/MollenOS.h>
#include <os/Syscall.h>

/* Kernel Guard */
#ifdef LIBC_KERNEL
void __EnvironmentLibCEmpty(void)
{
}
#else

/* Resolve Environmental Path
 * Resolves the environmental type
 * to an valid absolute path */
void EnvironmentResolve(EnvironmentPath_t SpecialPath, char *StringBuffer)
{
	/* Just deep call, we have
	* all neccessary functionlity and validation already in place */
	Syscall2(MOLLENOS_SYSCALL_VFSPATH,
		MOLLENOS_SYSCALL_PARAM(SpecialPath), 
		MOLLENOS_SYSCALL_PARAM(StringBuffer));
}

/* Environment Query
 * Query the system environment
 * for information, this could be
 * memory, cpu, etc information */
int EnvironmentQuery(EnvironmentQueryFunction_t Function, void *Buffer, size_t Length)
{
	/* Just deep call, syscall does all
	 * validation checks */
	return Syscall3(MOLLENOS_SYSCALL_SYSTEMQUERY, MOLLENOS_SYSCALL_PARAM(Function),
		MOLLENOS_SYSCALL_PARAM(Buffer), MOLLENOS_SYSCALL_PARAM(Length));
}

#endif