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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * x86 Advanced Programmable Interrupt Controller Driver
 * - Interrupt Handlers specific for the APIC
 */

#include <arch/utils.h>
#include <arch/x86/apic.h>
#include <interrupts.h>

irqstatus_t
ApicErrorHandler(
        _In_ InterruptFunctionTable_t*  NotUsed,
        _In_ void*                      Context)
{
    _CRT_UNUSED(Context);
    _CRT_UNUSED(NotUsed);
    return IRQSTATUS_HANDLED;
}
