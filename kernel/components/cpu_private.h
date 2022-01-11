/**
 * Copyright 2018, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * System Component Infrastructure
 * - The Central Processing Unit Component is one of the primary actors
 *   in a domain.
 *
 * The processor object is the primary object and represents a physical
 * processor. A processor contains multiple core objects, and each core object
 * represents a physical core inside a processor. Each core object can contain
 * a number of TXU (Thread eXecution Units). The usual number would be one
 * or two (HT). Each of these TXU's has the ability to schedule threads, handle
 * interrupts, send messages etc.
 *
 */

#ifndef __VALI_CPU_PRIVATE_H__
#define __VALI_CPU_PRIVATE_H__

#include <component/cpu.h>
#include "../scheduling/threading_private.h"

typedef struct SystemCpuCore {
    UUId_t           Id;
    SystemCpuState_t State;
    int              External;

    // Static resources
    Thread_t         IdleThread;
    Scheduler_t      Scheduler;

    // State resources
    queue_t          FunctionQueue[CpuFunctionCount];
    Thread_t*        CurrentThread;
    Context_t*       InterruptRegisters;
    int              InterruptNesting;
    uint32_t         InterruptPriority;

    struct SystemCpuCore* Link;

    PlatformCpuCoreBlock_t PlatformData;
} SystemCpuCore_t;

#endif //__VALI_CPU_PRIVATE_H__
