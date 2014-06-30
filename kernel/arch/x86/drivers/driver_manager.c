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
* MollenOS X86-32 Driver Manager
* Version 1. PCI Support Only (No PCI Express)
*/

/* Includes */
#include <arch.h>
#include <assert.h>
#include <acpi.h>
#include <pci.h>
#include <list.h>
#include <heap.h>
#include <stddef.h>
#include <stdio.h>
#include <limits.h>

/* Definitions */
#define ACPI_BUS_TYPE_DEVICE	0x1
#define ACPI_BUS_TYPE_PROCESSOR	0x2
#define ACPI_BUS_TYPE_THERMAL	0x3
#define ACPI_BUS_TYPE_POWER		0x4

#define ACPI_STA_DEFAULT		0x1

/* Drivers */
#include <drivers\usb\ohci\ohci.h>

/* Prototypes */
void pci_check_function(uint8_t bus, uint8_t device, uint8_t function);
void pci_check_device(uint8_t bus, uint8_t device);
void pci_check_bus(uint8_t bus);

/* Globals */
list_t *glb_pci_devices = NULL;


list_t *glb_pci_acpica = NULL;
uint32_t glb_root_count = 0;

/* Externs */

/* Decode PCI Device to String */
char *pci_to_string(uint8_t class, uint8_t sub_class, uint8_t prog_if)
{
	switch (class)
	{
		/* Device was built prior definition of the class code field */
		case 0:
		{
			switch (sub_class)
			{
				case 0:
				{
					return "Non-VGA-Compatible devices";
				} break;

				case 1:
				{
					return "VGA-Compatible Device";
				} break;

				default:
				{
					return "Unclassified Device";
				} break;
			}
		} break;

		/* Mass Storage Controller */
		case 1:
		{
			switch (sub_class)
			{
				case 0:
				{
					return "SCSI Bus Controller";
				} break;

				case 1:
				{
					return "IDE Controller";
				} break;

				case 2:
				{
					return "Floppy Disk Controller";
				} break;

				case 3:
				{
					return "IPI Bus Controller";
				} break;

				case 4:
				{
					return "RAID Controller";
				} break;

				case 5:
				{
					/* ATA */
					if (prog_if == 0x20)
						return "ATA Controller (Single DMA)";
					else
						return "ATA Controller (Chained DMA)";
				} break;

				case 6:
				{
					/* AHCI */
					if (prog_if == 0x0)
						return "Serial ATA (Vendor Specific Interface)";
					else
						return "Serial ATA (AHCI 1.0)";
				} break;

				case 7:
				{
					return "Serial Attached SCSI (SAS)";
				} break;

				case 0x80:
				{
					return "Other Mass Storage Controller";
				} break;

				default:
				{
					return "Unclassified Mass Storage Controller";
				} break;
			}
		} break;

		/* Network Controller */
		case 2:
		{
			switch (sub_class)
			{
				case 0:
				{
					return "Ethernet Controller";
				} break;

				case 1:
				{
					return "Token Ring Controller";
				} break;

				case 2:
				{
					return "FDDI Controller";
				} break;

				case 3:
				{
					return "ATM Controller";
				} break;

				case 4:
				{
					return "ISDN Controller";
				} break;

				case 5:
				{
					return "WorldFip Controller";
				} break;

				case 6:
				{
					return "PICMG 2.14 Multi Computing";
				} break;

				case 0x80:
				{
					return "Other Network Controller";
				} break;

				default:
				{
					return "Unclassified Network Controller";
				} break;
			}
		} break;

		/* Display Controller */
		case 3:
		{
			switch (sub_class)
			{
				case 0:
				{
					if (prog_if == 0x0)
						return "VGA-Compatible Controller";
					else
						return "8512-Compatible Controller";
				} break;

				case 1:
				{
					return "XGA Controller";
				} break;

				case 2:
				{
					return "3D Controller (Not VGA-Compatible)";
				} break;

				case 0x80:
				{
					return "Other Display Controller";
				} break;

				default:
				{
					return "Unclassified Display Controller";
				} break;
			}
		} break;

		/* Multimedia Controller */
		case 4:
		{
			switch (sub_class)
			{
				case 0:
				{
					return "Video Device";
				} break;

				case 1:
				{
					return "Audio Device";
				} break;

				case 2:
				{
					return "Computer Telephony Device";
				} break;

				case 3:
				{
					return "Flash Card Controller";
				} break;

				case 0x80:
				{
					return "Other Multimedia Device";
				} break;

				default:
				{
					return "Unclassified Multimedia Device";
				} break;
			}
		} break;

		/* Memory Controller */
		case 5:
		{
			switch (sub_class)
			{
				case 0:
				{
					return "RAM Controller";
				} break;

				case 1:
				{
					return "Flash Controller";
				} break;

				case 0x80:
				{
					return "Other Memory Controller";
				} break;

				default:
				{
					return "Unclassified Memory Controller";
				} break;
			}
		} break;

		/* Bridge Device */
		case 6:
		{
			switch (sub_class)
			{
				case 0:
				{
					return "Host Bridge";
				} break;

				case 1:
				{
					return "ISA Bridge";
				} break;

				case 2:
				{
					return "EISA Bridge";
				} break;

				case 3:
				{
					return "MCA Bridge";
				} break;

				case 4:
				{
					if (prog_if == 0x0)
						return "PCI-to-PCI Bridge";
					else
						return "PCI-to-PCI Bridge (Subtractive Decode)";
				} break;

				case 5:
				{
					return "PCMCIA Bridge";
				} break;

				case 6:
				{
					return "NuBus Bridge";
				} break;

				case 7:
				{
					return "CardBus Bridge";
				} break;

				case 8:
				{
					return "RACEway Bridge";
				} break;

				case 9:
				{
					if (prog_if == 0x40)
						return "PCI-to-PCI Bridge (Semi-Transparent, Primary)";
					else
						return "PCI-to-PCI Bridge (Semi-Transparent, Secondary)";
				} break;

				case 10:
				{
					return "InfiniBrand-to-PCI Host Bridge";
				} break;

				case 0x80:
				{
					return "Other Bridge Device";
				} break;

				default:
				{
					return "Unclassified Bridge Device";
				} break;
			}
		} break;

		/* Simple Communication Controllers */
		case 7:
		{
			switch (sub_class)
			{
				case 0:
				{
					switch (prog_if)
					{
						case 0:
						{
							return "Generic XT-Compatible Serial Controller";
						} break;

						case 1:
						{
							return "16450-Compatible Serial Controller";
						} break;

						case 2:
						{
							return "16550-Compatible Serial Controller";
						} break;

						case 3:
						{
							return "16650-Compatible Serial Controller";
						} break;

						case 4:
						{
							return "16750-Compatible Serial Controller";
						} break;

						case 5:
						{
							return "16850-Compatible Serial Controller";
						} break;

						case 6:
						{
							return "16950-Compatible Serial Controller";
						} break;

						default:
						{
							return "Generic Serial Controller";
						} break;
					}
				} break;

				case 1:
				{
					switch (prog_if)
					{
						case 0:
						{
							return "Parallel Port";
						} break;

						case 1:
						{
							return "Bi-Directional Parallel Port";
						} break;

						case 2:
						{
							return "ECP 1.X Compliant Parallel Port";
						} break;

						case 3:
						{
							return "IEEE 1284 Controller";
						} break;

						case 0xFE:
						{
							return "IEEE 1284 Target Device";
						} break;

						default:
						{
							return "Unclassified Port";
						} break;
					}
				} break;

				case 2:
				{
					return "Multiport Serial Controller";
				} break;

				case 3:
				{
					switch (prog_if)
					{
						case 0:
						{
							return "Generic Modem";
						} break;

						case 1:
						{
							return "Hayes Compatible Modem (16450-Compatible Interface)";
						} break;

						case 2:
						{
							return "Hayes Compatible Modem (16550-Compatible Interface)";
						} break;

						case 3:
						{
							return "Hayes Compatible Modem (16650-Compatible Interface)";
						} break;

						case 4:
						{
							return "Hayes Compatible Modem (16750-Compatible Interface)";
						} break;

						default:
						{
							return "Unclassified Modem";
						} break;
					}
				} break;

				case 4:
				{
					return "IEEE 488.1/2 (GPIB) Controller";
				} break;

				case 5:
				{
					return "Smart Card";
				} break;

				case 0x80:
				{
					return "Other Communications Device";
				} break;

				default:
				{
					return "Unclassified Communications Device";
				} break;
			}
		} break;

		/* Base System Peripherals */
		case 8:
		{
			switch (sub_class)
			{
				case 0:
				{
					switch (prog_if)
					{
						case 0:
						{
							return "Generic 8259 PIC";
						} break;

						case 1:
						{
							return "ISA PIC";
						} break;

						case 2:
						{
							return "EISA PIC";
						} break;

						case 0x10:
						{
							return "I/O APIC Interrupt Controller";
						} break;

						case 0x20:
						{
							return "I/O(x) APIC Interrupt Controller";
						} break;

						default:
						{
							return "Unclassified ISA Device";
						} break;
					}
				} break;

				case 1:
				{
					switch (prog_if)
					{
						case 0:
						{
							return "Generic 8237 DMA Controller";
						} break;

						case 1:
						{
							return "ISA DMA Controller";
						} break;

						case 2:
						{
							return "EISA DMA Controller";
						} break;

						default:
						{
							return "Unclassified DMA Controller";
						} break;
					}
				} break;

				case 2:
				{
					switch (prog_if)
					{
						case 0:
						{
							return "Generic 8254 System Timer";
						} break;

						case 1:
						{
							return "ISA System Timer";
						} break;

						case 2:
						{
							return "EISA System Timer";
						} break;

						default:
						{
							return "Unclassified System Timer";
						} break;
					}
				} break;

				case 3:
				{
					if (prog_if == 0x0)
						return "Generic RTC Controller";
					else
						return "ISA RTC Controller";
				} break;

				case 4:
				{
					return "Generic PCI Hot-Plug Controller";
				} break;

				case 0x80:
				{
					return "Other System Peripheral";
				} break;

				default:
				{
					return "Unclassified System Peripheral";
				} break;
			}
		} break;

		/* Input Devices */
		case 9:
		{
			switch (sub_class)
			{
				case 0:
				{
					return "Keyboard Controller";
				} break;

				case 1:
				{
					return "Digitizer";
				} break;

				case 2:
				{
					return "Mouse Controller";
				} break;

				case 3:
				{
					return "Scanner Controller";
				} break;

				case 4:
				{
					if (prog_if == 0x0)
						return "Gameport Controller (Generic)";
					else
						return "Gameport Contrlller (Legacy)";
				} break;

				case 0x80:
				{
					return "Other Input Controller";
				} break;

				default:
				{
					return "Unclassified Input Controller";
				} break;
			}
		} break;

		/* Docking Stations */
		case 10:
		{
			if (sub_class == 0x0)
				return "Generic Docking Station";
			else
				return "Other Docking Station";
		} break;

		/* Processors */
		case 11:
		{
			switch (sub_class)
			{
				case 0:
				{
					return "386 Processor";
				} break;

				case 1:
				{
					return "486 Processor";
				} break;

				case 2:
				{
					return "Pentium Processor";
				} break;

				case 0x10:
				{
					return "Alpha Processor";
				} break;

				case 0x20:
				{
					return "PowerPC Processor";
				} break;

				case 0x30:
				{
					return "MIPS Processor";
				} break;

				case 0x40:
				{
					return "Co-Processor";
				} break;

				default:
				{
					return "Unclassified Processor";
				} break;
			}
		} break;

		/* Serial Bus Controllers */
		case 12:
		{
			switch (sub_class)
			{
				case 0:
				{
					if (prog_if == 0x0)
						return "IEEE 1394 Controller (FireWire)";
					else
						return "IEEE 1394 Controller (1394 OpenHCI Spec)";
				} break;

				case 1:
				{
					return "ACCESS.bus";
				} break;

				case 2:
				{
					return "SSA";
				} break;

				case 3:
				{
					switch (prog_if)
					{
						case 0:
						{
							return "USB UHCI (Universal Host Controller Spec)";
						} break;

						case 0x10:
						{
							return "USB OHCI (Open Host Controller Spec";
						} break;

						case 0x20:
						{
							return "USB2 Host Controller (Intel Enhanced Host Controller Interface)";
						} break;

						case 0x30:
						{
							return "USB3 Host Controller (Intel Enhanced Host Controller Interface)";
						} break;

						case 0x80:
						{
							return "Other USB Controller";
						} break;

						case 0xFE:
						{
							return "USB (Not Host Controller)";
						} break;

						default:
						{
							return "Unclassified USB Controller";
						} break;
					}
				} break;

				case 4:
				{
					return "Fibre Channel";
				} break;

				case 5:
				{
					return "SMBus";
				} break;

				case 6:
				{
					return "InfiniBand";
				} break;

				case 7:
				{
					switch (prog_if)
					{
						case 0:
						{
							return "IPMI SMIC Interface";
						} break;

						case 1:
						{
							return "IPMI Kybd Controller Style Interface";
						} break;

						case 2:
						{
							return "IPMI Block Transfer Interface";
						} break;

						default:
						{
							return "Unclassified IPMI Controller";
						} break;
					}
				} break;

				case 8:
				{
					return "SERCOS Interface Standard (IEC 61491)";
				} break;

				case 9:
				{
					return "CANbus";
				} break;

				case 0x80:
				{
					return "Other Serial Bus Controllers";
				} break;

				default:
				{
					return "Unclassified Serial Bus Controllers";
				} break;
			}
		} break;

		/* Wireless Controllers */
		case 13:
		{
			switch (sub_class)
			{
				case 0:
				{
					return "iRDA Compatible Controller";
				} break;

				case 1:
				{
					return "Consumer IR Controller";
				} break;

				case 0x10:
				{
					return "RF Controller";
				} break;

				case 0x11:
				{
					return "Bluetooth Controller";
				} break;

				case 0x12:
				{
					return "Broadband Controller";
				} break;

				case 0x20:
				{
					return "Ethernet Controller (802.11a)";
				} break;

				case 0x21:
				{
					return "Ethernet Controller (802.11b)";
				} break;

				case 0x80:
				{
					return "Other Wireless Controller";
				} break;

				default:
				{
					return "Unclassified Wireless Controller";
				} break;
			}
		} break;

		/* Intelligent I/O Controllers */
		case 14:
		{
			switch (sub_class)
			{
				case 0:
				{
					if (prog_if == 0x0)
						return "Message FIFO";
					else
						return "I20 Architecture";
				} break;

				case 0x80:
				{
					return "Other Intelligent I/O Controllers";
				} break;

				default:
				{
					return "Unclassified Intelligent I/O Controllers";
				} break;
			}
		} break;

		/* Satellite Communication Controllers */
		case 15:
		{
			switch (sub_class)
			{
				case 1:
				{
					return "TV Controller";
				} break;

				case 2:
				{
					return "Audio Controller";
				} break;

				case 3:
				{
					return "Voice Controller";
				} break;

				case 4:
				{
					return "Data Controller";
				} break;

				case 0x80:
				{
					return "Other Communication Controller";
				} break;

				default:
				{
					return "Unclassified Communication Controller";
				} break;
			}
		} break;

		/* Encryption/Decryption Controllers */
		case 16:
		{
			switch (sub_class)
			{
				case 0:
				{
					return "Network and Computing Encrpytion/Decryption";
				} break;

				case 0x10:
				{
					return "Entertainment Encryption/Decryption";
				} break;

				case 0x80:
				{
					return "Other Encryption/Decryption";
				} break;

				default:
				{
					return "Unclassified Encryption/Decryption";
				} break;
			}
		} break;

		/* Data Acquisition and Signal Processing Controllers */
		case 17:
		{
			switch (sub_class)
			{
				case 0:
				{
					return "DPIO Modules";
				} break;

				case 1:
				{
					return "Performance Counters";
				} break;

				case 0x10:
				{
					return "Communications Syncrhonization Plus Time and Frequency Test/Measurment";
				} break;

				case 0x20:
				{
					return "Management Card";
				} break;

				case 0x80:
				{
					return "Other Data Acquisition/Signal Processing Controller";
				} break;

				default:
				{
					return "Unclassified Data Acquisition/Signal Processing Controller";
				} break;
			}
		} break;

		default:
		{
			return "Unclassified Device";
		} break;

	}
}

/* PCI Interface I/O */
uint32_t pci_read_dword(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Read Data */
	return inl(X86_PCI_DATA + (reg & 3));
}

uint16_t pci_read_word(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Read Data */
	return inw(X86_PCI_DATA + (reg & 3));
}

uint8_t pci_read_byte(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Read Data */
	return inb(X86_PCI_DATA + (reg & 3));
}

void pci_write_dword(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg, uint32_t value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Write DATA */
	outl(X86_PCI_DATA + (reg & 3), value);
	return;
}

void pci_write_word(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg, uint16_t value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Write DATA */
	outw(X86_PCI_DATA + (reg & 3), value);
	return;
}

void pci_write_byte(const uint16_t bus, const uint16_t dev,
	const uint16_t func, const uint32_t reg, uint8_t value)
{
	/* Select Bus/Device/Function/Register */
	outl(X86_PCI_SELECT, 0x80000000L | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
		((uint32_t)func << 8) | (reg & ~3));

	/* Write DATA */
	outb(X86_PCI_DATA + (reg & 3), value);
	return;
}

/* Reads the vendor id at given location */
uint16_t pci_read_vendor_id(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint32_t tmp = pci_read_dword(bus, device, function, 0);
	uint16_t vendor = (tmp & 0xFFFF);

	return vendor;
}

/* Reads a PCI header at given location */
void pci_read_function(pci_device_header_t *pcs, const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);
	uint32_t i;

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so the config space is 256 bytes long
		* and we read in dwords: 64 reads should do it.
		*/

		for (i = 0; i < 64; i += 16)
		{
			*(uint32_t*)((size_t)pcs + i) = pci_read_dword(bus, device, function, i);
			*(uint32_t*)((size_t)pcs + i + 4) = pci_read_dword(bus, device, function, i + 4);
			*(uint32_t*)((size_t)pcs + i + 8) = pci_read_dword(bus, device, function, i + 8);
			*(uint32_t*)((size_t)pcs + i + 12) = pci_read_dword(bus, device, function, i + 12);
		}
	}
}

/* Reads the base class at given location */
uint8_t pci_read_base_class(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x08);
		return (uint8_t)((offset >> 24) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the sub class at given location */
uint8_t pci_read_sub_class(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x08);
		return (uint8_t)((offset >> 16) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the secondary bus number at given location */
uint8_t pci_read_secondary_bus_number(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x18);
		return (uint8_t)((offset >> 8) & 0xFF);
	}
	else
		return 0xFF;
}

/* Reads the sub class at given location */
/* Bit 7 - MultiFunction, Lower 4 bits is type. 
 * Type 0 is standard, Type 1 is PCI-PCI Bridge,
 * Type 2 is CardBus Bridge */
uint8_t pci_read_header_type(const uint16_t bus, const uint16_t device, const uint16_t function)
{
	/* Get the dword and parse the vendor and device ID */
	uint16_t vendor = pci_read_vendor_id(bus, device, function);

	if (vendor && vendor != 0xFFFF)
	{
		/* Valid device! Okay, so read the base_class
		*/
		uint32_t offset = pci_read_dword(bus, device, function, 0x0C);
		return (uint8_t)((offset >> 16) & 0xFF);
	}
	else
		return 0xFF;
}

/* Check a function */
/* For each function we create a 
 * pci_device and add it to the list */
void pci_check_function(uint8_t bus, uint8_t device, uint8_t function)
{
	uint8_t sec_bus;
	pci_device_header_t *pcs;
	pci_driver_t *pci_driver;

	/* Allocate */
	pcs = (pci_device_header_t*)kmalloc(sizeof(pci_device_header_t));
	pci_driver = (pci_driver_t*)kmalloc(sizeof(pci_driver_t));

	/* Get full information */
	pci_read_function(pcs, bus, device, function);

	/* Set information */
	pci_driver->header = pcs;
	pci_driver->bus = bus;
	pci_driver->device = device;
	pci_driver->function = function;
	pci_driver->children = NULL;

	/* Info */
	printf("    * [%d:%d:%d][%d:%d:%d] Vendor 0x%x, Device 0x%x : %s\n",
		pcs->class_code, pcs->subclass, pcs->ProgIF, 
		bus, device, function,
		pcs->vendor_id, pcs->device_id,
		pci_to_string(pcs->class_code, pcs->subclass, pcs->ProgIF));

	/* Add to list */
	if (bus == 0)
	{
		if (pcs->class_code == 0x06 && pcs->subclass == 0x04)
		{
			pci_driver->type = X86_PCI_TYPE_BRIDGE;
			list_append(glb_pci_devices, list_create_node(X86_PCI_TYPE_BRIDGE, pci_driver));
		}
		else
		{
			pci_driver->type = X86_PCI_TYPE_DEVICE;
			list_append(glb_pci_devices, list_create_node(X86_PCI_TYPE_DEVICE, pci_driver));
		}
	}
	else
	{
		/* Find correct bus */
	}

	/* Is this a secondary (PCI) bus */
	if ((pcs->class_code == 0x06) && (pcs->subclass == 0x04))
	{
		/* Uh oh, this dude has children */
		pci_driver->children = list_create(LIST_SAFE);

		sec_bus = pci_read_secondary_bus_number(bus, device, function);
		pci_check_bus(sec_bus);
	}
}

/* Check a device */
void pci_check_device(uint8_t bus, uint8_t device)
{
	uint8_t function = 0;
	uint16_t vendor_id = 0;
	uint8_t header_type = 0;

	/* Get vendor id */
	vendor_id = pci_read_vendor_id(bus, device, function);

	/* Sanity */
	if (vendor_id == 0xFFFF)
		return;

	/* Check function 0 */
	pci_check_function(bus, device, function);
	header_type = pci_read_header_type(bus, device, function);

	/* Multi-function or single? */
	if (header_type & 0x80) 
	{
		/* It is a multi-function device, so check remaining functions */
		for (function = 1; function < 8; function++) 
		{
			/* Only check if valid vendor */
			if (pci_read_vendor_id(bus, device, function) != 0xFFFF)
				pci_check_function(bus, device, function);
		}
	}
}

/* Check a bus */
void pci_check_bus(uint8_t bus) 
{
	uint8_t device;

	for (device = 0; device < 32; device++)
		pci_check_device(bus, device);
}

/* First of all, devices exists on TWO different
 * busses. PCI and PCI Express. */
void drivers_enumerate(void)
{
	uint8_t function;
	uint8_t bus;
	uint8_t header_type;

	header_type = pci_read_header_type(0, 0, 0);
	
	if ((header_type & 0x80) == 0) 
	{
		/* Single PCI host controller */
		printf("    * Single Bus Present\n");
		pci_check_bus(0);
	}
	else 
	{
		/* Multiple PCI host controllers */
		printf("    * Multi Bus Present\n");
		for (function = 0; function < 8; function++) 
		{
			if (pci_read_vendor_id(0, 0, function) != 0xFFFF)
				break;

			/* Check bus */
			bus = function;
			pci_check_bus(bus);
		}
	}
}

/* Get Status and Type */
ACPI_STATUS acpi_retrieve_status(ACPI_HANDLE ObjHandle, uint64_t *STA)
{
	ACPI_STATUS Status;
	ACPI_BUFFER aBuffer;

	aBuffer.Length = 8;
	aBuffer.Pointer = STA;

	/* Execute _STA method */
	Status = AcpiEvaluateObjectTyped(ObjHandle, "_STA", NULL, &aBuffer, ACPI_TYPE_INTEGER);
	if (ACPI_SUCCESS(Status))
		return AE_OK;

	/* Set it to pseudo ok */
	if (Status == AE_NOT_FOUND) 
	{
		*STA = ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
				ACPI_STA_DEVICE_UI | ACPI_STA_DEVICE_FUNCTIONING;
		return AE_OK;

	}

	return Status;
}

int acpi_retrieve_type_status(ACPI_HANDLE ObjHandle, int *ObjType, uint64_t *STA)
{
	ACPI_STATUS Status;
	ACPI_OBJECT_TYPE Type;

	/* Get Handle Type */
	Status = AcpiGetType(ObjHandle, &Type);
	if (ACPI_FAILURE(Status))
		return -1;

	switch (Type)
	{
		case ACPI_TYPE_ANY:             /* for ACPI_ROOT_OBJECT */
		case ACPI_TYPE_DEVICE:
			*ObjType = ACPI_BUS_TYPE_DEVICE;
			Status = acpi_retrieve_status(ObjHandle, STA);
			if (ACPI_FAILURE(Status))
				return -1;
			break;
		case ACPI_TYPE_PROCESSOR:
			*ObjType = ACPI_BUS_TYPE_PROCESSOR;
			Status = acpi_retrieve_status(ObjHandle, STA);
			if (ACPI_FAILURE(Status))
				return -1;
			break;
		case ACPI_TYPE_THERMAL:
			*ObjType = ACPI_BUS_TYPE_THERMAL;
			*STA = ACPI_STA_DEFAULT;
			break;
		case ACPI_TYPE_POWER:
			*ObjType = ACPI_BUS_TYPE_POWER;
			*STA = ACPI_STA_DEFAULT;
			break;
		default:
			return -1;
	}

	return 0;
}

/* Callback */

/* _SEG - Segment Number 
 * _BBN - Bus Number */
ACPI_STATUS acpi_walk_callback(ACPI_HANDLE ObjHandle, UINT32 Level, void *Context, void **ReturnValue)
{
	int Type;
	uint64_t STA;
	int Result;
	ACPI_STATUS Status;
	ACPI_DEVICE_INFO DevInfo;
	ACPI_DEVICE_INFO *DevInfoPtr = &DevInfo;

	/* Get type and status */
	Result = acpi_retrieve_type_status(ObjHandle, &Type, &STA);
	
	/* Sanity */
	if (Result)
		return AE_OK;

	/* Create an acpi object, fill with info */
	Status = AcpiGetObjectInfo(ObjHandle, &DevInfoPtr);

	/* Sanity */
	if (ACPI_FAILURE(Status))
		return AE_OK;

	
}

/* Retrives Object Data */
ACPI_STATUS acpi_get_info_callback(ACPI_RESOURCE *res, void *context)
{
	context = context;
	if (res->Type == ACPI_RESOURCE_TYPE_IRQ) 
	{
		ACPI_RESOURCE_IRQ *irq;

		irq = &res->Data.Irq;
		printf("Irq: %u\n", irq->Interrupts[0]);
		
	}
	else if (res->Type == ACPI_RESOURCE_TYPE_EXTENDED_IRQ)
	{
		ACPI_RESOURCE_EXTENDED_IRQ *irq;

		irq = &res->Data.ExtendedIrq;
		printf("Irq: %u\n", irq->Interrupts[0]);
	}

	return AE_OK;
}

ACPI_STATUS DisplayOneDevice(ACPI_HANDLE ObjHandle, UINT32 Level, void *Context, void **ReturnValue)
{
	ACPI_STATUS Status;
	ACPI_DEVICE_INFO Info;
	ACPI_DEVICE_INFO *pInfo = &Info;
	ACPI_BUFFER Path;
	char Buffer[256];
	uint32_t fn_num = 0;
	uint32_t dev_num = 0;

	Path.Length = sizeof(Buffer);
	Path.Pointer = Buffer;

	/* Get the full path of this device and print it */
	Status = AcpiGetName(ObjHandle, ACPI_FULL_PATHNAME, &Path);
	
	if (ACPI_SUCCESS(Status))
		printf("%s ", Path.Pointer);
	
	/* Get the device info for this device and print it */
	Status = AcpiGetObjectInfo(ObjHandle, &pInfo);
	if (ACPI_SUCCESS(Status))
	{
		fn_num = (Info.Address >> 16) & 0xFFFF;
		dev_num = Info.Address & 0xFFFF;
		
		printf("- HID: 0x%x, Status: 0x%x, Device: %u, Function: %u\n",
			Info.HardwareId, Info.CurrentStatus, dev_num, fn_num);
		
	}

	/* Get info...? :/ */
	//AcpiWalkResources(ObjHandle, METHOD_NAME__CRS, acpi_get_info_callback, NULL);

	/* Done */
	return (AE_OK);
}

/* Same as above, using ACPICA */
void drivers_enumerate_acpica(void)
{
	ACPI_STATUS status;

	/* Scan PCI */
	status = AcpiWalkNamespace(ACPI_TYPE_ANY, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX, DisplayOneDevice, NULL, NULL, NULL);

	/* Sanity */
	assert(ACPI_SUCCESS(status));
}

/* This installs a driver for each device present (if we have a driver!) */
void drivers_setup_device(void *data, int n)
{
	pci_driver_t *driver = (pci_driver_t*)data;
	list_t *sub_bus;
	n = n;

	switch (driver->type)
	{
		case X86_PCI_TYPE_BRIDGE:
		{
			/* Get bus list */
			sub_bus = (list_t*)driver->children;

			/* Install drivers on that bus */
			list_execute_all(sub_bus, drivers_setup_device);

		} break;

		case X86_PCI_TYPE_DEVICE:
		{
			/* Get driver */

			/* Serial Bus Comms */
			if (driver->header->class_code == 0x0C)
			{
				/* Usb? */
				if (driver->header->subclass == 0x03)
				{
					/* Controller Type? */

					/* UHCI -> 0. OHCI -> 0x10. EHCI -> 0x20. xHCI -> 0x30 */
					if (driver->header->ProgIF == 0x10)
					{
						/* Initialise Controller */
						ohci_init(driver);
					}
				}
			}

		} break;

		default:
			break;
	}
}

/* Initialises all available drivers in system */
void drivers_init(void)
{
	/* Init list, this is "bus 0" */
	glb_pci_devices = list_create(LIST_SAFE);
	glb_pci_acpica = list_create(LIST_SAFE);

	/* Start out by enumerating devices */
	drivers_enumerate();
	drivers_enumerate_acpica();

	/* Now, for each driver we have available install it */
	list_execute_all(glb_pci_devices, drivers_setup_device);
}