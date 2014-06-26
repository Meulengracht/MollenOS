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
* MollenOS X86-32 PCI
* Version 1. PCI Support Only (No PCI Express)
*/

#ifndef _X86_PCI_H_
#define _X86_PCI_H_

/* Includes */
#include <crtdefs.h>
#include <stdint.h>

/* Definitions */
#define X86_PCI_SELECT		0xCF8
#define X86_PCI_DATA		0xCFC

/* Structures */
typedef struct _pci_device_header
{
	/* 0x00 */
	uint16_t vendor_id;
	uint16_t device_id;

	/* 0x04 */
	uint16_t command;
	uint16_t status;

	/* 0x08 */
	uint8_t  revision;
	uint8_t  ProgIF;
	uint8_t  subclass;
	uint8_t  class_code;

	/* 0x0C */
	uint8_t  cache_line_size;
	uint8_t  latency_timer;
	uint8_t  header_type;
	uint8_t  bist;

	/* 0x10 */
	uint32_t bar0;
	/* 0x14 */
	uint32_t bar1;
	/* 0x18 */
	uint32_t bar2;
	/* 0x1C */
	uint32_t bar3;
	/* 0x20 */
	uint32_t bar4;
	/* 0x24 */
	uint32_t bar5;

	/* 0x28 */
	uint32_t cardbus_cis_pointer;
	/* 0x2C */
	uint16_t subsystem_vendor_id;
	uint16_t subsystem_id;

	/* 0x30 */
	uint32_t expansion_rom_base_address;

	/* 0x34 */
	uint32_t reserved0;

	/* 0x38 */
	uint32_t reserved1;

	/* 0x3C */
	uint8_t  interrupt_line;
	uint8_t  interrupt_pin;
	uint8_t  min_grant;
	uint8_t  max_latency;

} pci_device_header_t;

/* The Driver Header */
typedef struct _pci_driver
{
	/* Type */
	uint32_t type;

	/* Location */
	uint32_t bus;
	uint32_t device;
	uint32_t function;

	/* Information (Header) */
	struct _pci_device_header *header;

	/* Children (list.h) */
	void *children;

} pci_driver_t;

/* Types */
#define X86_PCI_TYPE_BRIDGE		0x1
#define X86_PCI_TYPE_DEVICE		0x2


#define X86_PCI_TYPE_IDE		0x2
#define X86_PCI_TYPE_AHCI		0x3
#define X86_PCI_TYPE_UHCI		0x4
#define X86_PCI_TYPE_OHCI		0x5
#define X86_PCI_TYPE_XHCI		0x6

#endif // !_X86_PCI_H_
