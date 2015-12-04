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
* MollenOS MCore Threading
*
*/

#ifndef _MCORE_THREADING_H_
#define _MCORE_THREADING_H_

/* Includes */
#include <MollenOS.h>
#include <Arch.h>
#include <MString.h>
#include <List.h>
#include <stdint.h>

/* Definitions */
typedef void(*ThreadEntry_t)(void*);
typedef unsigned int TId_t;

/* Threading Flags */
#define THREADING_USERMODE		0x1
#define THREADING_CPUBOUND		0x2
#define THREADING_SYSTEMTHREAD	0x4
#define THREADING_IDLE			0x8
#define THREAIDNG_ENTER_SLEEP	0x10
#define THREADING_FINISHED		0x20
#define THREADING_TRANSITION	0x40

/* Structures */
typedef struct _MCoreThread
{
	/* Name */
	char *Name;

	/* Thread Attributes */
	uint32_t Flags;

	/* Scheduler Information */
	uint32_t TimeSlice;
	int32_t Priority;
	
	/* Synchronization */
	Addr_t *SleepResource;

	/* Ids */
	TId_t ThreadId;
	TId_t ParentId;
	uint32_t ProcessId;
	Cpu_t CpuId;

	/* Address Space */
	AddressSpace_t *AddrSpace;

	/* Architecture Data */
	void *ThreadData;

	/* Entry point */
	ThreadEntry_t Func;
	void *Args;

} MCoreThread_t;

/* Prototypes */
_CRT_EXTERN void ThreadingInit(void);
_CRT_EXTERN void ThreadingApInit(Cpu_t Cpu);

_CRT_EXPORT TId_t ThreadingCreateThread(char *Name, ThreadEntry_t Function, void *Args, int Flags);
_CRT_EXTERN void threading_kill_thread(TId_t thread_id);

/* Processes */
_CRT_EXTERN void ThreadingEnterUserMode(void *ProcessInfo);

/* Sleep, Wake, etc */
_CRT_EXPORT void *ThreadingEnterSleep(void);
_CRT_EXTERN int ThreadingYield(void *Args);
_CRT_EXTERN void ThreadingWakeCpu(Cpu_t Cpu);
_CRT_EXTERN MCoreThread_t *ThreadingSwitch(Cpu_t Cpu, MCoreThread_t *Current, uint8_t PreEmptive);

/* Gets */
_CRT_EXTERN TId_t ThreadingGetCurrentThreadId(void);
_CRT_EXTERN MCoreThread_t *ThreadingGetCurrentThread(Cpu_t Cpu);
_CRT_EXTERN list_node_t *ThreadingGetCurrentNode(Cpu_t Cpu);
_CRT_EXTERN int ThreadingIsCurrentTaskIdle(Cpu_t Cpu);
_CRT_EXTERN int ThreadingIsEnabled(void);

#endif