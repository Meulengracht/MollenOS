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
 * MollenOS MCore - String Format
 */

/* Includes 
 * - System */
#include "MStringPrivate.h"

#ifdef LIBC_KERNEL
#include <Log.h>
#endif

/* Prints out a mstring to stdout
 * Switches functionality based on kernel
 * or user-space */
void MStringPrint(MString_t *String)
{
#ifdef LIBC_KERNEL
	LogInformation("MSTR", "%s", String->Data);
#else
	/* Simply just print it out*/
	printf("%s\n", String->Data);
#endif
}
