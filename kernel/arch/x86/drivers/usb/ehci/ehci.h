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
* MollenOS X86-32 USB EHCI Controller Driver
*/

#ifndef _X86_USB_EHCI_H_
#define _X86_USB_EHCI_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>
#include <pci.h>

/* Definitions */
#define X86_EHCI_MAX_PORTS		15

/* Structures */

/* EHCI Register Space */

/* Capability Registers */
#pragma pack(push, 1)
typedef struct _EchiCapabilityRegisters
{
	/* Capability Registers Length 
	 * We add this offset to the Usb Base
	 * to get operational registers */
	uint8_t Length;

	/* Reserved */
	uint8_t Reserved;

	/* Interface Version Number */
	uint16_t Version;

	/* Structural Parameters 
	 * Bits 0-3: Port Count (max of 15 ports) 
	 * Bits 4: Port Power Control (If set, ports have power switches) 
	 * Bits 5-6: Reserved
	 * Bits 7: Port Routing Rules. 
	 * Bits 8-11: Number of ports per companion controller 
	 * Bits 12-15: Number of companion controllers 
	 * Bits 16: Port Indicators Support 
	 * Bits 17-19: Reserved
	 * Bits 20-23: Debug Port Number 
	 * Bits 24-31: Reserved */
	uint32_t SParams;

	/* Capability Parameters 
	 * Bits 0: 64 Bit Capability if set (Use 64 bit data structures instead of 32 bit)! 
	 * Bits 1: Frame List Flag, if set we can control how long a frame list is, otherwise it is 1024.
	 * Bits 2: Asynchronous Schedule Park Support, if set it supports park feature for high-speed queue heads. 
	 * Bits 3: Reserved
	 * Bits 4-7: Isochronous Scheduling Threshold
	 * Bits 8-15: Extended Capability Pointer, EECP. If a value is above 0x40 its a valid offset into pci-space. 
	 * Bits 16-31: Reserved */
	uint32_t CParams;

	/* Companion Port Route Description */
	uint64_t PortRouting;

} EchiCapabilityRegisters_t;
#pragma pack(pop)

/* Operational Registers */
#pragma pack(push, 1)
typedef struct _EchiOperationalRegisters
{
	/* USB Command Register */
	uint32_t UsbCommand;

	/* USB Status Register */
	uint32_t UsbStatus;

	/* USB Interrupt Register */
	uint32_t UsbIntr;

	/* Frame Index */
	uint32_t FrameIndex;

	/* 4G Segment Selector */
	uint32_t SegmentSelector;

	/* Frame List Base Address */
	uint32_t FrameListAddr;

	/* Next Asynchronous List Address */
	uint32_t NextAsyncListAddr;

	/* Reserved */
	uint8_t Reserved[(0x40 - 0x1C)];

	/* Configured Flag Register */
	uint32_t ConfigFlag;

	/* Port Status Registers */
	uint32_t Ports[X86_EHCI_MAX_PORTS];

} EchiOperationalRegisters_t;
#pragma pack(pop)

/* Prototypes */
_CRT_EXTERN void EhciInit(PciDevice_t *PciDevice);

#endif