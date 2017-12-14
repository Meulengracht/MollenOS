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
//#include "../libmsc++/mvcxx.h"
#include <stddef.h>
#include <string.h>
#include <assert.h>

#ifdef LIBC_KERNEL
void __ExceptionLibCEmpty(void)
{
}
#else

/* LowLevel Exception Functions */
uint32_t ZwContinue(/* PCONTEXT */ void * Context, int TestAlert)
{
	_CRT_UNUSED(Context);
	_CRT_UNUSED(TestAlert);
	return 0;
}

uint32_t ZwRaiseException(
    /* PEXCEPTION_RECORD */ void * ExceptionRecord, 
    /* PCONTEXT */ void * Context, int FirstChance)
{
	_CRT_UNUSED(ExceptionRecord);
	_CRT_UNUSED(Context);
	_CRT_UNUSED(FirstChance);
	return 0;
}

#endif
