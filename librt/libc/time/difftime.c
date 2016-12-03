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
* MollenOS C Library - Get Time Difference
*/

/* Includes */
#include <os/MollenOS.h>
#include <os/Syscall.h>
#include <time.h>
#include <stddef.h>
#include <string.h>

/* difftime 
 * Simply takes the difference in 
 * time and returns, diff is in secs */
double difftime(time_t end, time_t start)
{
	/* Done! */
	return (double)(end - start);
}