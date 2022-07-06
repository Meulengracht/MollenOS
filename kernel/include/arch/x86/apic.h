/**
 * Copyright 2017, Philip Meulengracht
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
 * MollenOS x86 Advanced Programmable Interrupt Controller Driver
 * - General APIC header for local apic functionality
 */

#ifndef __APIC_H__
#define __APIC_H__

#include <os/osdefs.h>
#include <ddk/interrupt.h>
#include <component/ic.h>

/* Local Apic Registers Definitions
 * Register offsets from the LOCAL_APIC_BASE. */
#define APIC_PROCESSOR_ID		0x20
#define APIC_VERSION			0x30
#define APIC_TASK_PRIORITY		0x80
#define APIC_INTERRUPT_ACK		0xB0
#define APIC_LOGICAL_DEST		0xD0
#define APIC_DEST_FORMAT		0xE0
#define APIC_SPURIOUS_REG		0xF0
#define APIC_ESR				0x280
#define APIC_CMCI               0x2F0
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

/* Interrupt Registers (Common) 
 * Common bit definitions used by the Local Apic register to configure. */
#define APIC_VECTOR(Vector)         ((Vector) & 0xFF)
#define APIC_DESTINATION_PHYSICAL   0
#define APIC_DESTINATION_LOGICAL    (1 << 11)
#define APIC_DELIVERY_BUSY          (1 << 12)
#define APIC_LEVEL_ASSERT           (1 << 14)
#define APIC_TRIGGER_EDGE           0
#define APIC_TRIGGER_LEVEL          (1 << 15)
#define APIC_DESTINATION(Dest)      (((Dest) & 0x7F) << 24)
#define APIC_BROADCAST              0xFF000000

#define APIC_DELIVERY_MODE(Mode)    (((Mode) & 0x7) << 8)
#define APIC_MODE_FIXED             0x0
#define APIC_MODE_LOWEST_PRIORITY   0x1
#define APIC_MODE_SMI               0x2
#define APIC_MODE_NMI               0x4
#define APIC_MODE_INIT              0x5
#define APIC_MODE_SIPI              0x6
#define APIC_MODE_EXTINT            0x7

/* Interrupt Command Register (ICR)
 * Bit definitions and numbers for the ICR register. */
#define APIC_ICR_SH_NONE            0
#define APIC_ICR_SH_SELF            (1 << 18)
#define APIC_ICR_SH_ALL             (1 << 19)
#define APIC_ICR_SH_ALL_OTHERS      ((1 << 18) | (1 << 19))

/* Local apic timer definitions */
#define APIC_TIMER_DIVIDER_1	0xB
#define APIC_TIMER_DIVIDER_16	0x3
#define APIC_TIMER_DIVIDER_128	0xA
#define APIC_TIMER_ONESHOT		0x0
#define APIC_TIMER_PERIODIC		0x20000

/* Helper definitions for the utility
 * and support functions */
#define APIC_PRIORITY_MASK		0xFF
#define APIC_NO_GSI				((int)-1)

/* Local apic flags for some of the
 * below registers, this is also io-apic
 * entry flags */
#define APIC_SMI_ROUTE			0x200
#define APIC_NMI_ROUTE			0x400
#define APIC_EXTINT_ROUTE		0x700
#define APIC_ACTIVE_LOW			0x2000
#define APIC_LEVEL_TRIGGER		0x8000
#define APIC_MASKED				0x10000

/* This only is something we need to check on 
 * 32-bit processors, all 64-bit cpus must use
 * the integrated APIC */
#if defined(_X86_32) || defined(i386)
#define APIC_INTEGRATED(x) ((x) & 0xF0)
#else
#define APIC_INTEGRATED(x) (1)
#endif

/* This is a configurable default quantum for the
 * local apic timer, this is used untill it's possible
 * for the cpu to more accurately calculate a quantum */
#define APIC_DEFAULT_QUANTUM 15000

typedef enum InterruptTarget {
    InterruptTarget_SPECIFIC,
    InterruptTarget_SELF,
    InterruptTarget_ALL,
    InterruptTarget_ALLBUTSELF
} InterruptTarget_t;

typedef enum InterruptMode {
    InterruptMode_PIC,
    InterruptMode_APIC,
    InterruptMode_APICX2
} InterruptMode_t;

/**
 * @brief Initialize the local APIC controller and install default interrupts.
 */
KERNELAPI void KERNELABI
ApicInitialize(void);

/**
 * @brief Returns OsOK if the local apic is initialized and memory mapped.
 * @return OsOK if the Local Apic is available.
 */
KERNELAPI oscode_t KERNELABI
ApicIsInitialized(void);

/**
 * @brief Returns the current system interrupt mode. This is determined by ApicInitialize().
 * @return The current interrupt mode.
 */
KERNELAPI InterruptMode_t KERNELABI
GetApicInterruptMode(void);

/**
 * @brief Enables the local apic and sets its default state. Also initializes the
 * local apic timer, but does not start it.
 */
KERNELAPI void KERNELABI
ApicInitializeForApplicationCore(void);

/**
 * @brief Initializes the the local apic timer, using an external timer source
 * to accurately have the local apic tick at 1ms
 */
KERNELAPI void KERNELABI
ApicTimerInitialize(void);

/**
 * @brief Reloads the local apic timer with a default divisor and the timer set to the given quantum
 * the timer is immediately started.
 *
 * @param[In] quantumValue The value that should be loaded into the local apic timer.
 */
KERNELAPI void KERNELABI
ApicTimerStart(
    _In_ size_t quantumValue);

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
__EXTERN uint32_t ApicIoRead(SystemInterruptController_t *IoApic, uint32_t Register);

/* Write to the io-apic registers
 * Reads and writes from and to the io apic
 * registers must always be 32 bit */
__EXTERN void ApicIoWrite(SystemInterruptController_t *IoApic, uint32_t Register, uint32_t Data);

/* Reads interrupt data from the io-apic
 * interrupt register. It reads the data from
 * the given Pin (io-apic entry) offset. */
__EXTERN uint64_t ApicReadIoEntry(SystemInterruptController_t *IoApic, int Pin);

/* Writes interrupt data to the io-apic
 * interrupt register. It writes the data to
 * the given Pin (io-apic entry) offset. */
__EXTERN void ApicWriteIoEntry(SystemInterruptController_t *IoApic, int Pin, uint64_t Data);

/**
 * @brief Acknowledge the reception of an interrupt
 *
 * @param[In] Gsi
 * @param[In] Vector
 */
KERNELAPI void KERNELABI
ApicSendEoi(
        _In_ int      Gsi,
        _In_ uint32_t Vector);

/**
 * @brief Unmasks the GSI provided, allowing the interrupt line to trigger
 *
 * @param[In] Gsi The Global Source Interrupt that should be unmasked.
 */
KERNELAPI void KERNELABI
ApicUnmaskGsi(
        _In_ int Gsi);

/**
 * @brief Masks the GSI provided, disallowing the interrupt line from triggering
 *
 * @param[In] Gsi The Global Source Interrupt that should be masked.
 */
KERNELAPI void KERNELABI
ApicMaskGsi(
        _In_ int Gsi);

/**
 * @brief Sends an interrupt vector-request to a given cpu-id.
 *
 * @param[In] type
 * @param[In] specific
 * @param[In] vector
 * @return
 */
KERNELAPI oscode_t KERNELABI
ApicSendInterrupt(
    _In_ InterruptTarget_t type,
    _In_ UUId_t            specific,
    _In_ uint32_t          vector);

/**
 * @brief Sends an ipi request for the specified cpu
 * @param[In] coreId
 * @param[In] assert
 * @return
 */
KERNELAPI oscode_t KERNELABI
ApicPerformIPI(
    _In_ UUId_t coreId,
    _In_ int    assert);

/**
 * @brief Sends an sipi request for the specified cpu, to start executing code at the given vector
 *
 * @param[In] coreId
 * @param[In] address
 * @return
 */
KERNELAPI oscode_t KERNELABI
ApicPerformSIPI(
    _In_ UUId_t    coreId,
    _In_ uintptr_t address);

/**
 * @brief On 32-bit processors the local apic might not be integrated onto the chip,
 * but rather be an external chip. This is not relevant for 64 bit cpus.
 *
 * @return 1 If the local apic is integrated, otherwise 0.
 */
KERNELAPI int KERNELABI
ApicIsIntegrated(void);

/**
 * @brief Retrieve the max supported LVT for the onboard local apic chip, this is used
 * for the ESR among others.
 *
 * @return The maximum supported LVT.
 */
KERNELAPI int KERNELABI
ApicGetMaxLvt(void);

/**
 * @brief Divides all cpu into different groups. All cpu's get their first bit set which means
 * that group 0 is targetting ALL cpus.
 *
 * @param[In] coreId The cpu core id
 * @return           The groups the cpu belongs to
 */
KERNELAPI uint32_t KERNELABI
ApicComputeLogicalDestination(
        _In_ UUId_t coreId);

/**
 * @brief Sets the current task priority
 *
 * @param[In] priority The value that should be loaded into the task priority register
 */
KERNELAPI void KERNELABI
ApicSetTaskPriority(
        _In_ uint32_t priority);

/**
 * @brief Get the current task priority set
 *
 * @return The value of the task priority register
 */
KERNELAPI uint32_t KERNELABI
ApicGetTaskPriority(void);

/* ApicTimerHandler
 * The scheduler interrupt handler. The only functionality this handler has is
 * to switch threads. */
KERNELAPI irqstatus_t KERNELABI
ApicTimerHandler(
        _In_ InterruptFunctionTable_t*  NotUsed,
        _In_ void*                      Context);

/* ApicErrorHandler
 * Handles any internally errors that the apic encounters. Most of these
 * don't have any resolution. */
KERNELAPI irqstatus_t KERNELABI
ApicErrorHandler(
        _In_ InterruptFunctionTable_t*  NotUsed,
        _In_ void*                      Context);

#endif //!__APIC_H__
