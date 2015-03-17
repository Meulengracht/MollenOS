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
	void *parent;
	void *children;

} pci_driver_t;

/* Internal Use */
typedef struct _pci_irq_res
{
	/* Double Voids */
	void *device;
	void *table;

} pci_irq_resource_t;

#pragma pack(push, 1)
typedef struct _pci_routings
{
	/* Just a lot of ints */
	int interrupts[128];
	uint8_t trigger[128];
	uint8_t shareable[128];
	uint8_t polarity[128];
	uint8_t fixed[128];

} pci_routing_table_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct _pci_device
{
	/* Type */
	uint32_t type;

	/* ACPI_HANDLE */
	void *handle;

	/* Irq Routings */
	struct _pci_routings *routings;

	/* Bus Id */
	char bus_id[8];

	/* PCI Location */
	uint32_t bus;
	uint32_t dev;
	uint32_t func;
	uint32_t seg;

	/* Supported NS Functions */
	uint32_t features;

	/* Current Status */
	uint32_t status;

	/* Bus Address */
	uint64_t address;

	/* Hardware Id */
	char hid[16];

	/* Unique Id */
	char uid[16];

	/* Compatible Id's */
	void *cid;

	/* Type Features */
	uint64_t xfeatures;

} pci_device_t;
#pragma pack(pop)


/* Feature Flags */
#define X86_ACPI_FEATURE_STA	0x1
#define X86_ACPI_FEATURE_CID	0x2
#define X86_ACPI_FEATURE_RMV	0x4
#define X86_ACPI_FEATURE_EJD	0x8
#define X86_ACPI_FEATURE_LCK	0x10
#define X86_ACPI_FEATURE_PS0	0x20
#define X86_ACPI_FEATURE_PRW	0x40
#define X86_ACPI_FEATURE_ADR	0x80
#define X86_ACPI_FEATURE_HID	0x100
#define X86_ACPI_FEATURE_UID	0x200
#define X86_ACPI_FEATURE_PRT	0x400
#define X86_ACPI_FEATURE_BBN	0x800
#define X86_ACPI_FEATURE_SEG	0x1000
#define X86_ACPI_FEATURE_REG	0x2000
#define X86_ACPI_FEATURE_CRS	0x4000

/* Type Definitions */
#define ACPI_BUS_SYSTEM			0x0
#define ACPI_BUS_TYPE_DEVICE	0x1
#define ACPI_BUS_TYPE_PROCESSOR	0x2
#define ACPI_BUS_TYPE_THERMAL	0x3
#define ACPI_BUS_TYPE_POWER		0x4
#define ACPI_BUS_TYPE_SLEEP		0x5
#define ACPI_BUS_TYPE_PWM		0x6
#define ACPI_BUS_ROOT_BRIDGE	0x7


/* Video Features */
#define ACPI_VIDEO_SWITCHING	0x1
#define ACPI_VIDEO_ROM			0x2
#define ACPI_VIDEO_POSTING		0x4
#define ACPI_VIDEO_BACKLIGHT	0x8
#define ACPI_VIDEO_BRIGHTNESS	0x10

/* Types */
#define X86_PCI_TYPE_BRIDGE		0x1
#define X86_PCI_TYPE_DEVICE		0x2


#define X86_PCI_TYPE_IDE		0x2
#define X86_PCI_TYPE_AHCI		0x3
#define X86_PCI_TYPE_UHCI		0x4
#define X86_PCI_TYPE_OHCI		0x5
#define X86_PCI_TYPE_XHCI		0x6

/* This is information */
typedef struct _pci_device_information
{
	/* Pci Vendor Id */
	uint32_t device_id;

	/* String */
	char *string;

} pci_dev_info_t;


/* Prototypes */

/* Read I/O */
_CRT_EXTERN uint8_t pci_read_byte(const uint16_t bus, const uint16_t dev,
									const uint16_t func, const uint32_t reg);
_CRT_EXTERN uint16_t pci_read_word(const uint16_t bus, const uint16_t dev,
									const uint16_t func, const uint32_t reg);
_CRT_EXTERN uint32_t pci_read_dword(const uint16_t bus, const uint16_t dev,
									const uint16_t func, const uint32_t reg);

/* Write I/O */
_CRT_EXTERN void pci_write_byte(const uint16_t bus, const uint16_t dev,
								 const uint16_t func, const uint32_t reg, uint8_t value);
_CRT_EXTERN void pci_write_word(const uint16_t bus, const uint16_t dev,
								 const uint16_t func, const uint32_t reg, uint16_t value);
_CRT_EXTERN void pci_write_dword(const uint16_t bus, const uint16_t dev,
								 const uint16_t func, const uint32_t reg, uint32_t value);

/* Install PCI Interrupt */
_CRT_EXTERN void InterruptInstallPci(pci_driver_t *PciDevice, IrqHandler_t Callback, void *Args);

/* Get Irq by Bus / Dev / Pin
* Returns -1 if no overrides exists */
_CRT_EXTERN int pci_device_get_irq(uint32_t bus, uint32_t device, uint32_t pin,
									uint8_t *trigger_mode, uint8_t *polarity, uint8_t *shareable,
									uint8_t *fixed);

/* Decode PCI Device to String */
_CRT_EXTERN char *pci_to_string(uint8_t class, uint8_t sub_class, uint8_t prog_if);

/* Reads the vendor id at given location */
_CRT_EXTERN uint16_t pci_read_vendor_id(const uint16_t bus, const uint16_t device, const uint16_t function);

/* Reads a PCI header at given location */
_CRT_EXTERN void pci_read_function(pci_device_header_t *pcs, const uint16_t bus, const uint16_t device, const uint16_t function);

/* Reads the base class at given location */
_CRT_EXTERN uint8_t pci_read_base_class(const uint16_t bus, const uint16_t device, const uint16_t function);

/* Reads the sub class at given location */
_CRT_EXTERN uint8_t pci_read_sub_class(const uint16_t bus, const uint16_t device, const uint16_t function);

/* Reads the secondary bus number at given location */
_CRT_EXTERN uint8_t pci_read_secondary_bus_number(const uint16_t bus, const uint16_t device, const uint16_t function);

/* Reads the sub class at given location 
 * Bit 7 - MultiFunction, Lower 4 bits is type.
 * Type 0 is standard, Type 1 is PCI-PCI Bridge,
 * Type 2 is CardBus Bridge */
_CRT_EXTERN uint8_t pci_read_header_type(const uint16_t bus, const uint16_t device, const uint16_t function);

#endif // !_X86_PCI_H_
