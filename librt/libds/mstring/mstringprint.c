/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * Generic String Library
 *    - Managed string library for manipulating of strings in a managed format and to support
 *      conversions from different formats to UTF-8
 */

/* Includes 
 * - System */
#include "mstringprivate.h"
#ifdef LIBC_KERNEL
#include <log.h>
#endif

/* MStringPrint
 * Writes the string to stdout. */
void
MStringPrint(
    _In_ MString_t *String)
{
#ifdef LIBC_KERNEL
	LogAppendMessage(LogTrace, "MSTR", "%s", (char*)String->Data);
#else
	printf("%s\n", (char*)String->Data);
#endif
}
