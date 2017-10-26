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
 * MollenOS x86 Advanced Programmable Interrupt Controller Driver
 * - General APIC header for local apic functionality
 */

#ifndef _X86_APIC_H_
#define _X86_APIC_H_

/* Includes */
#include <arch.h>
#include <os/osdefs.h>

/* Local apic timer definitions */
#define APIC_TIMER_DIVIDER_1	0xB
#define APIC_TIMER_DIVIDER_16	0x3
#define APIC_TIMER_DIVIDER_128	0xA
#define APIC_TIMER_ONESHOT		0x0
#define APIC_TIMER_PERIODIC		0x20000

/* Helper definitions for the utility
 * and support functions */
#define APIC_PRIORITY_MASK		0xFF
#define APIC_NO_GSI				-1

/* Local apic flags for some of the
 * below registers, this is also io-apic
 * entry flags */
#define APIC_SMI_ROUTE			0x200
#define APIC_NMI_ROUTE			0x400
#define APIC_EXTINT_ROUTE		0x700
#define APIC_ACTIVE_LOW			0x2000
#define APIC_LEVEL_TRIGGER		0x8000
#define APIC_MASKED				0x10000
#define APIC_ICR_BUSY			0x1000

/* This is the list of local apic
 * registers and their offsets */
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

/* This only is something we need to check on 
 * 32-bit processors, all 64 bit cpus must use
 * the integrated APIC */
#ifdef _X86_32
#define APIC_INTEGRATED(x)      ((x) & 0xF0)
#else
#define APIC_INTEGRATED(x)      (1)
#endif

/* This is a configurable default quantum for the
 * local apic timer, this is used untill it's possible
 * for the cpu to more accurately calculate a quantum */
#define APIC_DEFAULT_QUANTUM	8000

/* The struture of an io-apic entry in
 * the apic code, we keep track of id,
 * version and gsi information */
typedef struct _IoApic {
	int					Id;
	int					Version;
	int					GsiStart;
	int					PinCount;
	volatile uintptr_t		BaseAddress;
} IoApic_t;

/* Initialize the local APIC controller
 * and install default interrupts. This
 * code also sets up the local APIC timer
 * with a default Quantum which is recalibrated
 * for accuracy once a timer is available */
__EXTERN void ApicInitBoot(void);
__EXTERN void ApicInitAp(void);

/* Recalibrates the the local apic 
 * timer, using an external timer source
 * this is to make the local apic timer more
 * accurate to make sure the quantum is 1 ms */
__EXTERN void ApicTimerRecalibrate(void);

/* Reloads the local apic timer with a default
 * divisor and the timer set to the given quantum
 * the timer is immediately started */
__EXTERN void ApicReloadTimer(size_t Quantum);

/* Reads from the local apic registers 
 * Reads and writes from and to the local apic
 * registers must always be 32 bit */
__EXTERN uint32_t ApicReadLocal(size_t Register);

/* Write to the local apic registers 
 * Reads and writes from and to the local apic
 * registers must always be 32 bit */
__EXTERN void ApicWriteLocal(size_t Register, uint32_t Value);

/* Read from io-apic registers
 * Reads and writes from and to the io apic
 * registers must always be 32 bit */
__EXTERN uint32_t ApicIoRead(IoApic_t *IoApic, uint32_t Register);

/* Write to the io-apic registers
 * Reads and writes from and to the io apic
 * registers must always be 32 bit */
__EXTERN void ApicIoWrite(IoApic_t *IoApic, uint32_t Register, uint32_t Data);

/* Reads interrupt data from the io-apic
 * interrupt register. It reads the data from
 * the given Pin (io-apic entry) offset. */
__EXTERN uint64_t ApicReadIoEntry(IoApic_t *IoApic, int Pin);

/* Writes interrupt data to the io-apic
 * interrupt register. It writes the data to
 * the given Pin (io-apic entry) offset. */
__EXTERN void ApicWriteIoEntry(IoApic_t *IoApic, int Pin, uint64_t Data);

/* Sends end of interrupt to the local
 * apic chip, and enables for a new interrupt
 * on that irq line to occur */
__EXTERN void ApicSendEoi(int Gsi, uint32_t Vector);

/* ApicUnmaskGsi
 * Unmasks the given gsi if possible by deriving
 * the io-apic and pin from it. This allows the
 * io-apic to deliver interrupts again */
__EXTERN void ApicUnmaskGsi(int Gsi);

/* ApicMaskGsi 
 * Masks the given gsi if possible by deriving
 * the io-apic and pin from it. This makes sure
 * the io-apic delivers no interrupts */
__EXTERN void ApicMaskGsi(int Gsi);

/* Invoke an IPI request on either target
 * cpu, or on all cpu cores if a broadcast
 * has been requested. The supplied vector will
 * be the invoked interrupt */
__EXTERN void ApicSendIpi(UUId_t CpuTarget, uint32_t Vector);

/* This function derives an io-apic from
 * the given gsi index, by locating which
 * io-apic owns the gsi and returns it.
 * Returns NULL if gsi is invalid */
__EXTERN IoApic_t *ApicGetIoFromGsi(int Gsi);

/* Calculates the pin from the 
 * given gsi, it tries to locate the
 * relevenat io-apic, if not found 
 * it returns -1, otherwise the pin */
__EXTERN int ApicGetPinFromGsi(int Gsi);

/* This only is something we need to check on 
 * 32-bit processors, all 64 bit cpus must use
 * the integrated APIC */
__EXTERN int ApicIsIntegrated(void);

/* Retrieve the max supported LVT for the
 * onboard local apic chip, this is used 
 * for the ESR among others */
__EXTERN int ApicGetMaxLvt(void);

/* Retrieve the cpu id for the current cpu
 * can be used as an identifier when running
 * multicore */
__EXTERN UUId_t ApicGetCpu(void);

/* Creates the correct bit index for
 * the given cpu id, and converts the type
 * to uint, since thats what the apic needs */
__EXTERN uint32_t ApicGetCpuMask(UUId_t Cpu);

/* Helper for updating the task priority register
 * this register helps us using Lowest-Priority
 * delivery mode, as this controls which cpu to
 * interrupt */
__EXTERN void ApicSetTaskPriority(uint32_t Priority);

/* Retrives the current task priority
 * for the current cpu */
__EXTERN uint32_t ApicGetTaskPriority(void);

#endif //!_X86_APIC_H_