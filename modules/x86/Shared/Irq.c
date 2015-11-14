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
* MollenOS Module Shared Library (Interrupts)
*/

/* Includes */
#include <stddef.h>
#include <Arch.h>
#include <Module.h>

/* Internal Defines */
#define EFLAGS_INTERRUPT_FLAG (1 << 9)

/* Externs */
extern void __cli(void);
extern void __sti(void);
extern uint32_t __getflags(void);

/* Disables interrupts and returns
* the state before disabling */
IntStatus_t InterruptDisable(void)
{
	IntStatus_t cur_state = InterruptSaveState();
	__cli();
	return cur_state;
}

/* Enables interrupts and returns
* the state before enabling */
IntStatus_t InterruptEnable(void)
{
	IntStatus_t cur_state = InterruptSaveState();
	__sti();
	return cur_state;
}

/* Restores the state to the given
* state */
IntStatus_t InterruptRestoreState(IntStatus_t state)
{
	if (state != 0)
		return InterruptEnable();
	else
		return InterruptDisable();

}

/* Gets the current interrupt state */
IntStatus_t InterruptSaveState(void)
{
	if (__getflags() & EFLAGS_INTERRUPT_FLAG)
		return 1;
	else
		return 0;
}

/* Returns whether or not interrupts are
* disabled */
IntStatus_t InterruptIsDisabled(void)
{
	return !InterruptSaveState();
}

/* Typedefs */
#include <x86\Pci.h>

typedef void (*__irqinstallisa)(uint32_t Irq, uint32_t IdtEntry, IrqHandler_t Callback, void *Args);
typedef void (*__irqinstallpci)(PciDevice_t *PciDevice, IrqHandler_t Callback, void *Args);
typedef void (*__irqinstallidt)(uint32_t Gsi, uint32_t IdtEntry, IrqHandler_t Callback, void *Args);
typedef void (*__irqinstallshared)(uint32_t Irq, uint32_t IdtEntry, IrqHandler_t Callback, void *Args);
typedef OsStatus_t (*__irqallocisa)(uint32_t Irq);

/* Install Irq for Legacy device */
void InterruptInstallISA(uint32_t Irq, uint32_t IdtEntry, IrqHandler_t Callback, void *Args)
{
	((__irqinstallisa)GlbFunctionTable[kFuncInstallIrqISA])(Irq, IdtEntry, Callback, Args);
}

void InterruptInstallPci(PciDevice_t *PciDevice, IrqHandler_t Callback, void *Args)
{
	((__irqinstallpci)GlbFunctionTable[kFuncInstallIrqPci])(PciDevice, Callback, Args);
}

void InterruptInstallIdtOnly(uint32_t Gsi, uint32_t IdtEntry, IrqHandler_t Callback, void *Args)
{
	((__irqinstallidt)GlbFunctionTable[kFuncInstallIrqIdt])(Gsi, IdtEntry, Callback, Args);
}

void InterruptInstallShared(uint32_t Irq, uint32_t IdtEntry, IrqHandler_t Callback, void *Args)
{
	((__irqinstallshared)GlbFunctionTable[kFuncInstallIrqShared])(Irq, IdtEntry, Callback, Args);
}

OsStatus_t InterruptAllocateISA(uint32_t Irq)
{
	((__irqallocisa)GlbFunctionTable[kFuncAllocateIrqISA])(Irq);
}