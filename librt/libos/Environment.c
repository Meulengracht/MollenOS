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

/* Resolve Environmental Path
 * Resolves the environmental type
 * to an valid absolute path */
void EnvironmentResolve(EnvironmentPath_t SpecialPath, char *StringBuffer)
{
	/* Just deep call, we have
	 * all neccessary functionlity and validation already in place */
	Syscall2(SYSCALL_VFSPATH,
		SYSCALL_PARAM(SpecialPath), SYSCALL_PARAM(StringBuffer));
}

/* Environment Query
 * Query the system environment
 * for information, this could be
 * memory, cpu, etc information */
int EnvironmentQuery(EnvironmentQueryFunction_t Function, void *Buffer, size_t Length)
{
	/* Just deep call, syscall does all
	 * validation checks */
	return Syscall3(SYSCALL_SYSTEMQUERY, SYSCALL_PARAM(Function),
		SYSCALL_PARAM(Buffer), SYSCALL_PARAM(Length));
}
