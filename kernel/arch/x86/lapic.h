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
* MollenOS x86-32 Local APIC Driver
*/

#ifndef _X86_LAPIC_H_
#define _X86_LAPIC_H_

/* LAPIC Includes */
#include <crtdefs.h>
#include <stdint.h>

//Local Apic is located at 0xFEE00000 
//The local apic is not shared between processors.
//However addr is same for all processors.

//I / O Apic is located at 0xFEC00000
//This IO APIC is shared.

//Two APIC Types:
//82489DX, Version: 0x0X (X = 4 bit hex version num)
//Integrated, Version: 0x1X (X = 4 bit hex version num)
//The intergrated one has the STARTUP IPI feature
//and: The I/O APIC interrupt input signal polarity can be programmable.

//82489DX
//Has an 8 bit ID field, supports up to 255 APIC devices and 32
//logical destination devices.

/* Local APIC Registers:
FEE0 0020H: Local APIC ID Register Read/Write.
FEE0 0030H: Local APIC Version Register Read Only.
FEE0 0080H: Task Priority Register (TPR) Read/Write.
FEE0 0090H: Arbitration Priority Register	Read only
FEE0 00A0H: Processor Priority Register (PPR) Read Only.
FEE0 00B0H: EOI Register Write Only.
FEE0 00D0H: Logical Destination Register Read/Write.
FEE0 00E0H: Destination Format Register Read/Write
FEE0 00F0H: Spurious Interrupt Vector Register Read/Write

FEE0 0100H - 0170H:	ISR 0-255	Read only
FEE0 0180H - 01F0H:	TMR 0-255	Read only
FEE0 0200H - 0270H:	IRR 0-255	Read only

FEE0 0280h	Error Status Register	Read only
FEE0 02F0h  LVT CMCI Register       Read/Write.
FEE0 0300h	Interrupt Command Register 0-31	Read/Write
FEE0 0310h	Interrupt Command Register 32-63	Read/Write
FEE0 0320h	Local Vector Table (Timer)	Read/Write
FEE0 0330h  LVT Thermal Sensor Register Read/Write.
FEE0 0340h	Performance Counter LVT	Read/Write
FEE0 0350h	Local Vector Table (LINT0)	Read/Write
FEE0 0360h	Local Vector Table (LINT1)	Read/Write
FEE0 0370h	Local Vector Table (Error)	Read/Write
FEE0 0380h	Initial Count Register for Timer	Read/Write
FEE0 0390h	Current Count Register for Timer	Read only
FEE0 03E0h	Timer Divide Configuration Register	Read/Write


10.5.2 Valid Interrupt Vectors
The Intel 64 and IA-32 architectures define 256 vector numbers, ranging from 0
through 255 (see Section 6.2, “Exception and Interrupt Vectors”). Local and I/O
APICs support 240 of these vectors (in the range of 16 to 255) as valid interrupts.
When an interrupt vector in the range of 0 to 15 is sent or received through the local
APIC, the APIC indicates an illegal vector in its Error Status Register (see Section
10.5.3, “Error Handling”). The Intel 64 and IA-32 architectures reserve vectors 16
through 31 for predefined interrupts, exceptions, and Intel-reserved encodings (see
Table 6-1). However, the local APIC does not treat vectors in this range as illegal.
When an illegal vector value (0 to 15) is written to an LVT entry and the delivery
mode is Fixed (bits 8-11 equal 0), the APIC may signal an illegal vector error, without
regard to whether the mask bit is set or whether an interrupt is actually seen on the
input.
*/

/* Defines */
#define LAPIC_DIVIDER_1		0xB
#define LAPIC_DIVIDER_16	0x3
#define LAPIC_DIVIDER_128	0xA

#define X86_MAX_HANDLERS_PER_INTERRUPT	4

#define LAPIC_PROCESSOR_ID		0x20
#define LAPIC_INTERRUPT_ACK		0xB0
#define LAPIC_LOGICAL_DEST		0xD0
#define LAPIC_DEST_FORMAT		0xE0
#define LAPIC_SPURIOUS_REG		0xF0
#define LAPIC_TIMER_VECTOR		0x320
#define LAPIC_THERMAL_SENSOR	0x330
#define LAPIC_PERF_MONITOR		0x340
#define LAPIC_LINT0_REGISTER	0x350
#define LAPIC_LINT1_REGISTER	0x360
#define LAPIC_ERROR_REGISTER	0x370
#define LAPIC_INITIAL_COUNT		0x380
#define LAPIC_CURRENT_COUNT		0x390
#define LAPIC_DIVIDE_REGISTER	0x3E0

/* LAPIC Prototypes */

/* Initialisers */
_CRT_EXTERN void apic_init(void);
_CRT_EXTERN void apic_timer_init(void);

_CRT_EXTERN void acpi_init_stage1(void);
_CRT_EXTERN void acpi_init_stage2(void);

/* Read/Write to local apic */
_CRT_EXTERN uint32_t apic_read_local(uint32_t reg);
_CRT_EXTERN void apic_write_local(uint32_t reg, uint32_t value);

/* Read/Write to io apic */
_CRT_EXTERN uint32_t apic_read_io(uint8_t ioapic, uint32_t reg);
_CRT_EXTERN void apic_write_io(uint8_t ioapic, uint32_t reg, uint32_t data);
_CRT_EXTERN void apic_write_entry_io(uint8_t ioapic, uint32_t reg, uint64_t data);
_CRT_EXTERN uint64_t apic_read_entry_io(uint8_t ioapic, uint32_t reg);

/* Set Task Priority */
_CRT_EXTERN void apic_set_task_priority(uint32_t priority);
_CRT_EXTERN uint32_t apic_get_task_priority(void);

/* Send EOI */
_CRT_EXTERN void apic_send_eoi(void);

/* Send APIC Interrupt to a core */
_CRT_EXTERN void lapic_send_ipi(uint8_t cpu_destination, uint8_t irq_vector);

/* Call this on AP */
_CRT_EXTERN void lapic_ap_init(void);
_CRT_EXTERN void lapic_init(void);

#endif // !_X86_LAPIC_H_


