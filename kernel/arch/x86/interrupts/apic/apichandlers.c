/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * x86 Advanced Programmable Interrupt Controller Driver
 * - Interrupt Handlers specific for the APIC
 */

#include <arch/utils.h>
#include <threading.h>
#include <interrupts.h>
#include <threading.h>
#include <acpi.h>
#include <apic.h>

extern size_t GlbTimerQuantum;

InterruptStatus_t
ApicTimerHandler(
        _In_ InterruptFunctionTable_t* NotUsed,
        _In_ void*                     Context)
{
    _CRT_UNUSED(NotUsed);
    _CRT_UNUSED(Context);
    
    uint32_t Count        = ApicReadLocal(APIC_CURRENT_COUNT);
    uint32_t Passed       = ApicReadLocal(APIC_INITIAL_COUNT) - Count;
    size_t   NextDeadline = 20;
    if (Count != 0) {
        ApicWriteLocal(APIC_INITIAL_COUNT, 0);
    }
    
    (void)ThreadingAdvance(Count == 0 ? 1 : 0, DIVUP(Passed, GlbTimerQuantum), &NextDeadline);
    ApicWriteLocal(APIC_INITIAL_COUNT, GlbTimerQuantum * NextDeadline);
    return InterruptHandled;
}

InterruptStatus_t
ApicErrorHandler(
        _In_ InterruptFunctionTable_t*  NotUsed,
        _In_ void*                      Context)
{
    _CRT_UNUSED(Context);
    _CRT_UNUSED(NotUsed);
    return InterruptHandled;
}
