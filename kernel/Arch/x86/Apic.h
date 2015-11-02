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
* MollenOS x86-32 Advanced Programmable Interrupt Controller Driver
*/
#ifndef _X86_APIC_H_
#define _X86_APIC_H_

/* Includes */
#include <Arch.h>
#include <stdint.h>
#include <crtdefs.h>

/* Definitions */
#define APIC_TIMER_DIVIDER_1	0xB
#define APIC_TIMER_DIVIDER_16	0x3
#define APIC_TIMER_DIVIDER_128	0xA

#define APIC_TIMER_ONESHOT		0x0
#define APIC_TIMER_PERIODIC		0x20000

#define APIC_PRIORITY_MASK		0xFF

#define APIC_NMI_ROUTE			0x400
#define APIC_EXTINT_ROUTE		0x700
#define APIC_MASKED				0x10000
#define APIC_ICR_BUSY			0x1000

#define APIC_PROCESSOR_ID		0x20
#define APIC_VERSION			0x30
#define APIC_TASK_PRIORITY		0x80
#define APIC_INTERRUPT_ACK		0xB0
#define APIC_LOGICAL_DEST		0xD0
#define APIC_DEST_FORMAT		0xE0
#define APIC_SPURIOUS_REG		0xF0
#define APIC_ESR				0x280
#define APIC_ICR_LOW			0x300
#define APIC_ICR_HIGH			0x310
#define APIC_TIMER_VECTOR		0x320
#define APIC_THERMAL_SENSOR		0x330
#define APIC_PERF_MONITOR		0x340
#define APIC_LINT0_REGISTER		0x350
#define APIC_LINT1_REGISTER		0x360
#define APIC_ERROR_REGISTER		0x370
#define APIC_INITIAL_COUNT		0x380
#define APIC_CURRENT_COUNT		0x390
#define APIC_DIVIDE_REGISTER	0x3E0

#ifdef _X86_32
#define APIC_INTEGRATED(x)      ((x) & 0xF0)
#else
#define APIC_INTEGRATED(x)      (1)
#endif

/* Structures */
typedef struct _IoApic
{
	/* Io Apic Id */
	uint32_t Id;

	/* Io Apic Version */
	uint32_t Version;

	/* Gsi Start */
	uint32_t GsiStart;

	/* Pin Count */
	uint32_t PinCount;

	/* Base Address */
	volatile uint32_t BaseAddress;

} IoApic_t;

/* Init Functions */
_CRT_EXTERN void ApicInitBoot(void);
_CRT_EXTERN void ApicInitAp(void);
_CRT_EXTERN void ApicInitBootFinalize(void);
_CRT_EXTERN void ApicTimerInit(void);
_CRT_EXTERN void ApicReloadTimer(uint32_t Quantum);

/* IO Functions */

/* Local Apic */
_CRT_EXTERN uint32_t ApicReadLocal(uint32_t Register);
_CRT_EXTERN void ApicWriteLocal(uint32_t Register, uint32_t Value);

/* IO Apic */
_CRT_EXTERN uint32_t ApicIoRead(IoApic_t *IoApic, uint32_t Register);
_CRT_EXTERN void ApicIoWrite(IoApic_t *IoApic, uint32_t Register, uint32_t Data);
_CRT_EXTERN uint64_t ApicReadIoEntry(IoApic_t *IoApic, uint32_t Register);
_CRT_EXTERN void ApicWriteIoEntry(IoApic_t *IoApic, uint32_t Pin, uint64_t Data);

/* Helper Functions */
_CRT_EXTERN void ApicSendEoi(uint32_t Gsi, uint32_t Vector);
_CRT_EXTERN void ApicSendIpi(uint8_t CpuTarget, uint8_t IrqVector);

_CRT_EXTERN IoApic_t *ApicGetIoFromGsi(uint32_t Gsi);
_CRT_EXTERN uint32_t ApicGetPinFromGsi(uint32_t Gsi);

_CRT_EXTERN int ApicIsIntegrated(void);
_CRT_EXTERN int ApicGetMaxLvt(void);
_CRT_EXTERN Cpu_t ApicGetCpu(void);
_CRT_EXTERN uint32_t ApicGetCpuMask(uint32_t Cpu);

_CRT_EXTERN void ApicSetTaskPriority(uint32_t Priority);
_CRT_EXTERN uint32_t ApicGetTaskPriority(void);

#endif //!_X86_APIC_H_