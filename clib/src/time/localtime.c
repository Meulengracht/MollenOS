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
* MollenOS C Library - Convert to localtime struct
*/

/* Includes */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <internal/_time.h>
#include <os/Syscall.h>
#include <os/Thread.h>

/* Re-entrency */
tm *localtime_r(const time_t *timer, tm *tmbuf) 
{
	/* Variables */
	time_t t;
	t = *timer - _timezone;
	return gmtime_r(&t, tmbuf);
}

/* localtime
 * converts a time_t to the 
 * timestructure with localtime 
 * format */
tm *localtime(const time_t *timer)
{
	tm *buf = &TLSGetCurrent()->TmBuffer;
	return localtime_r(timer, buf);
}