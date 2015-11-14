/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS Module Shared Library
*/

/* Includes */
#include <stddef.h>
#include <Threading.h>
#include <Module.h>

/* Typedefs */
typedef TId_t (*__createthread)(char *Name, ThreadEntry_t Function, void *Args, int Flags);
typedef void (*__threadyield)(void);

TId_t ThreadingCreateThread(char *Name, ThreadEntry_t Function, void *Args, int Flags)
{
	return ((__createthread)GlbFunctionTable[kFuncCreateThread])(Name, Function, Args, Flags);
}

void _ThreadYield(void)
{
	((__threadyield)GlbFunctionTable[kFuncYield])();
}

/* Scheduler */
typedef void (*__schedsleepthread)(Addr_t *Resource);
typedef int (*__schedwakeone)(Addr_t *Resource);

/* Put a thread to sleep untill resource is free */
void SchedulerSleepThread(Addr_t *Resource)
{
	((__schedsleepthread)GlbFunctionTable[kFuncSleepThread])(Resource);
}

/* Wake a thread on a resource */
int SchedulerWakeupOneThread(Addr_t *Resource)
{
	return ((__schedwakeone)GlbFunctionTable[kFuncWakeThread])(Resource);
}